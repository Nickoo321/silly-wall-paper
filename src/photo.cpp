// photo.cpp -- brief BY PHOTO-STAGE phase 1: folder scan, shuffle bag and the
// WIC decode worker. See photo.h for the contract; cycle.cpp drives the stage,
// fluid.cpp records the upload and draws kPhotoSrc.

#include "photo.h"
#include "app_state.h"
#include <wincodec.h>
#include <algorithm>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif

namespace {

void (*s_logger)(const char*) = nullptr;
std::wstring s_cliDir;                   // --photos <dir>
bool s_sync = false;                     // shot mode: join at the black point

// ---- scan + bag (director thread only) -----------------------------------------
const wchar_t* kExt[] = { L".jpg", L".jpeg", L".png", L".bmp", L".tif", L".tiff",
                          // tried; WIC may have no decoder on this machine (logged once per extension)
                          L".webp", L".heic", L".avif" };

struct FolderState {
    bool     scanned = false;
    unsigned serial = 0;
    std::vector<std::wstring> names;     // file names, sorted case-insensitively
    std::vector<std::wstring> shown;     // shown this round (names)
    std::wstring lastShown;
};
std::map<std::wstring, FolderState> s_folders;   // key: lower-case folder
std::vector<std::wstring> s_failed;              // lower-case full paths, failed this run
std::vector<std::wstring> s_placeholderLogged;   // lower-case full paths
uint64_t s_bagRng = 0;
bool     s_bagSeeded = false;

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}
std::wstring Key(std::wstring folder) {       // bag / scan key: lower case, no trailing slash
    while (folder.size() > 3 && (folder.back() == L'\\' || folder.back() == L'/')) folder.pop_back();
    return Lower(folder);
}
bool Contains(const std::vector<std::wstring>& v, const std::wstring& lowerKey) {
    for (const auto& x : v) if (_wcsicmp(x.c_str(), lowerKey.c_str()) == 0) return true;
    return false;
}
std::wstring Join(const std::wstring& folder, const std::wstring& name) {
    if (folder.empty()) return name;
    const wchar_t last = folder.back();
    return (last == L'\\' || last == L'/') ? folder + name : folder + L"\\" + name;
}
std::wstring FolderOfPath(const std::wstring& p) {
    const size_t sl = p.find_last_of(L"\\/");
    return sl == std::wstring::npos ? std::wstring() : p.substr(0, sl);
}
bool HasPhotoExt(const wchar_t* name) {
    const wchar_t* dot = wcsrchr(name, L'.');
    if (!dot) return false;
    for (const wchar_t* e : kExt) if (_wcsicmp(dot, e) == 0) return true;
    return false;
}

uint32_t BagNext() {                      // splitmix64: the bag's OWN stream
    if (!s_bagSeeded) PhotoBagSeed(0);
    s_bagRng += 0x9E3779B97F4A7C15ull;
    uint64_t z = s_bagRng;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t)((z ^ (z >> 31)) >> 32);
}

void Scan(FolderState& fs, const std::wstring& folder) {
    fs.names.clear();
    if (folder.empty()) return;          // shot mode without --photos: no folder at all
    const DWORD a = GetFileAttributesW(folder.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
        PhotoLog("[photo] scan %ls: folder missing\n", folder.c_str());
        return;
    }
    LARGE_INTEGER qf, q0, q1;
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&q0);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(Join(folder, L"*").c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;   // non-recursive
            if (!HasPhotoExt(fd.cFileName)) continue;
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_OFFLINE)) {
                const std::wstring key = Lower(Join(folder, fd.cFileName));
                if (!Contains(s_placeholderLogged, key)) {
                    s_placeholderLogged.push_back(key);
                    PhotoLog("[photo] skip %ls: cloud placeholder (not on this disk; opening it would "
                             "download it)\n", fd.cFileName);
                }
                continue;
            }
            fs.names.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(fs.names.begin(), fs.names.end(), [](const std::wstring& x, const std::wstring& y) {
        return _wcsicmp(x.c_str(), y.c_str()) < 0;
    });
    QueryPerformanceCounter(&q1);
    PhotoLog("[photo] scan %ls: %d files (%.2f ms)\n", folder.c_str(), (int)fs.names.size(),
             1000.0 * (double)(q1.QuadPart - q0.QuadPart) / (double)qf.QuadPart);
}

FolderState& Folder(const std::wstring& folder, unsigned serial) {
    FolderState& fs = s_folders[Key(folder)];
    if (!fs.scanned || fs.serial != serial) {
        Scan(fs, folder);
        fs.scanned = true;
        fs.serial = serial;
        // deleted files leave the round's shown list
        std::vector<std::wstring> keep;
        for (const auto& n : fs.shown)
            for (const auto& m : fs.names)
                if (_wcsicmp(n.c_str(), m.c_str()) == 0) { keep.push_back(n); break; }
        fs.shown.swap(keep);
    }
    return fs;
}

std::vector<std::wstring> Usable(const FolderState& fs, const std::wstring& folder) {
    std::vector<std::wstring> u;
    for (const auto& n : fs.names)
        if (!Contains(s_failed, Lower(Join(folder, n)))) u.push_back(n);
    return u;
}

// ---- the worker ------------------------------------------------------------------
struct Job {
    uint64_t id = 0;
    std::wstring path;
    ComPtr<ID3D12Device> dev;
    int  outW = 0, outH = 0;
    bool fill = false;
};
std::mutex s_mx;
std::condition_variable s_cv;
std::thread* s_thread = nullptr;         // heap: never destroyed joinable at exit
bool     s_stop = false;
bool     s_haveJob = false;
Job      s_job;                          // queued (not yet running)
uint64_t s_running = 0;                  // id being decoded
uint64_t s_nextId = 1;
std::vector<uint64_t> s_cancelled;
std::map<uint64_t, PhotoResult> s_done;
std::vector<std::wstring> s_extLogged;   // worker thread only

double Ms(const LARGE_INTEGER& a, const LARGE_INTEGER& b) {
    static LARGE_INTEGER f = {};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    return 1000.0 * (double)(b.QuadPart - a.QuadPart) / (double)f.QuadPart;
}

bool IsCancelled(uint64_t id) {
    std::lock_guard<std::mutex> lk(s_mx);
    return s_stop || std::find(s_cancelled.begin(), s_cancelled.end(), id) != s_cancelled.end();
}

void Fail(PhotoResult& r, const char* what, HRESULT hr) {
    char b[256];
    if (hr == WINCODEC_ERR_COMPONENTNOTFOUND)
        _snprintf_s(b, _TRUNCATE, "%s: no WIC decoder for this file (not an image, or no codec "
                    "for its format; hr 0x%08lX)", what, (unsigned long)hr);
    else
        _snprintf_s(b, _TRUNCATE, "%s failed (hr 0x%08lX)", what, (unsigned long)hr);
    r.ok = false;
    r.why = b;
}

// The WIC chain (pre-flight 18): decoder -> frame 0 -> EXIF orientation
// (IWICBitmapFlipRotator over a cached copy) -> IWICFormatConverter (32bppBGRA,
// or 64bppRGBA above 8 bits per channel) -> IWICBitmapScaler (Fant) to the fit
// rect in OUTPUT pixels (skipped at 1:1) -> fill: IWICBitmapClipper centred ->
// CopyPixels straight into the mapped upload buffer (pre-flight 20).
void Load(IWICImagingFactory* fac, const Job& j, PhotoResult& r) {
    LARGE_INTEGER t0, t1, t2, t3, t4;
    QueryPerformanceCounter(&t0);
    HRESULT hr;
    ComPtr<IWICBitmapDecoder> dec;
    hr = fac->CreateDecoderFromFilename(j.path.c_str(), nullptr, GENERIC_READ,
                                        WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr)) {
        const wchar_t* dot = wcsrchr(j.path.c_str(), L'.');
        if (hr == WINCODEC_ERR_COMPONENTNOTFOUND && dot &&
            (!_wcsicmp(dot, L".webp") || !_wcsicmp(dot, L".heic") || !_wcsicmp(dot, L".avif")) &&
            !Contains(s_extLogged, Lower(dot))) {
            s_extLogged.push_back(Lower(dot));
            PhotoLog("[photo] no WIC decoder for %ls on this machine: every %ls file is skipped\n", dot, dot);
        }
        Fail(r, "open", hr);
        return;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(hr = dec->GetFrame(0, &frame))) { Fail(r, "frame 0", hr); return; }
    UINT sw = 0, sh = 0;
    frame->GetSize(&sw, &sh);
    r.srcW = (int)sw;
    r.srcH = (int)sh;
    if (sw == 0 || sh == 0) { r.why = "empty image"; return; }
    // EXIF orientation (a phone portrait is stored sideways)
    {
        ComPtr<IWICMetadataQueryReader> q;
        if (SUCCEEDED(frame->GetMetadataQueryReader(&q))) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(q->GetMetadataByName(L"System.Photo.Orientation", &pv)) && pv.vt == VT_UI2 &&
                pv.uiVal >= 1 && pv.uiVal <= 8)
                r.orient = pv.uiVal;
            PropVariantClear(&pv);
        }
    }
    // an embedded ICC profile is treated as sRGB in phase 1 (logged)
    {
        UINT n = 0;
        if (SUCCEEDED(frame->GetColorContexts(0, nullptr, &n)) && n > 0 && n < 16) {
            std::vector<IWICColorContext*> ctx(n, nullptr);
            UINT made = 0;
            for (; made < n; made++) if (FAILED(fac->CreateColorContext(&ctx[made]))) break;
            UINT got = 0;
            if (made == n && SUCCEEDED(frame->GetColorContexts(n, ctx.data(), &got)))
                for (UINT k = 0; k < got; k++) {
                    WICColorContextType ty;
                    if (SUCCEEDED(ctx[k]->GetType(&ty)) && ty == WICColorContextProfile) r.icc = true;
                }
            for (UINT k = 0; k < made; k++) if (ctx[k]) ctx[k]->Release();
        }
    }
    // bits per channel / alpha from the source pixel format
    bool is16 = false;
    {
        WICPixelFormatGUID pf;
        ComPtr<IWICComponentInfo> ci;
        ComPtr<IWICPixelFormatInfo2> pfi;
        if (SUCCEEDED(frame->GetPixelFormat(&pf)) && SUCCEEDED(fac->CreateComponentInfo(pf, &ci)) &&
            SUCCEEDED(ci.As(&pfi))) {
            UINT bpp = 0, ch = 0;
            BOOL tr = FALSE;
            pfi->GetBitsPerPixel(&bpp);
            pfi->GetChannelCount(&ch);
            pfi->SupportsTransparency(&tr);
            r.bpc = ch ? (int)(bpp / ch) : 8;
            is16 = r.bpc >= 16;          // 16bppGray, 48bppRGB, 64bppRGBA, float formats
            r.alpha = tr != FALSE;
        }
    }
    ComPtr<IWICBitmapSource> src = frame;
    int ow = (int)sw, oh = (int)sh;
    WICBitmapTransformOptions opt = WICBitmapTransformRotate0;
    switch (r.orient) {
    case 2: opt = WICBitmapTransformFlipHorizontal; break;
    case 3: opt = WICBitmapTransformRotate180; break;
    case 4: opt = WICBitmapTransformFlipVertical; break;
    case 5: opt = (WICBitmapTransformOptions)(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal); break;
    case 6: opt = WICBitmapTransformRotate90; break;
    case 7: opt = (WICBitmapTransformOptions)(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal); break;
    case 8: opt = WICBitmapTransformRotate270; break;
    default: break;
    }
    if (opt != WICBitmapTransformRotate0) {
        // the rotator reads its source out of order: decode once into memory
        ComPtr<IWICBitmap> cached;
        ComPtr<IWICBitmapFlipRotator> fr;
        if (FAILED(hr = fac->CreateBitmapFromSource(src.Get(), WICBitmapCacheOnLoad, &cached)) ||
            FAILED(hr = fac->CreateBitmapFlipRotator(&fr)) || FAILED(hr = fr->Initialize(cached.Get(), opt))) {
            Fail(r, "EXIF orientation", hr);
            return;
        }
        src = fr;
        if (r.orient >= 5) { ow = (int)sh; oh = (int)sw; }
    }
    {
        ComPtr<IWICFormatConverter> cv;
        const WICPixelFormatGUID want = is16 ? GUID_WICPixelFormat64bppRGBA : GUID_WICPixelFormat32bppBGRA;
        if (FAILED(hr = fac->CreateFormatConverter(&cv)) ||
            FAILED(hr = cv->Initialize(src.Get(), want, WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeMedianCut))) {
            Fail(r, is16 ? "convert to 64bppRGBA" : "convert to 32bppBGRA", hr);
            return;
        }
        src = cv;
        r.fmt = is16 ? DXGI_FORMAT_R16G16B16A16_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    }
    // the fit rect in OUTPUT pixels: 1:1 sampling on the panel (pre-flight 18)
    const double sx = (double)j.outW / ow, sy = (double)j.outH / oh;
    int scW, scH;
    if (!j.fill) {
        const double s = sx < sy ? sx : sy;
        scW = (int)llround(ow * s); scH = (int)llround(oh * s);
        if (scW > j.outW) scW = j.outW;
        if (scH > j.outH) scH = j.outH;
        if (scW < 1) scW = 1;
        if (scH < 1) scH = 1;
        r.texW = scW; r.texH = scH;
    } else {
        const double s = sx > sy ? sx : sy;
        scW = (int)llround(ow * s); scH = (int)llround(oh * s);
        if (scW < j.outW) scW = j.outW;
        if (scH < j.outH) scH = j.outH;
        r.texW = j.outW; r.texH = j.outH;
    }
    if (scW != ow || scH != oh) {
        ComPtr<IWICBitmapScaler> sc;
        if (FAILED(hr = fac->CreateBitmapScaler(&sc)) ||
            FAILED(hr = sc->Initialize(src.Get(), (UINT)scW, (UINT)scH, WICBitmapInterpolationModeFant))) {
            Fail(r, "scale", hr);
            return;
        }
        src = sc;
        r.scaled = true;
    }
    if (scW != r.texW || scH != r.texH) {     // fill: the centred crop of the cover
        ComPtr<IWICBitmapClipper> cl;
        const WICRect rc = { (scW - r.texW) / 2, (scH - r.texH) / 2, r.texW, r.texH };
        if (FAILED(hr = fac->CreateBitmapClipper(&cl)) || FAILED(hr = cl->Initialize(src.Get(), &rc))) {
            Fail(r, "clip", hr);
            return;
        }
        src = cl;
    }
    QueryPerformanceCounter(&t1);
    if (IsCancelled(j.id)) { r.why = "cancelled"; return; }
    // D3D12 objects on this thread (the device is free-threaded, pre-flight 20)
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = (UINT64)r.texW;
        td.Height = (UINT)r.texH;
        td.DepthOrArraySize = 1;
        td.MipLevels = 1;
        td.Format = r.fmt;
        td.SampleDesc.Count = 1;
        if (FAILED(hr = j.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
                                                       D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                       IID_PPV_ARGS(&r.tex)))) {
            Fail(r, "create texture", hr);
            return;
        }
        UINT rows = 0;
        UINT64 rowBytes = 0, total = 0;
        j.dev->GetCopyableFootprints(&td, 0, 1, 0, &r.fp, &rows, &rowBytes, &total);
        D3D12_HEAP_PROPERTIES up = {};
        up.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd = {};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = total;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(hr = j.dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd,
                                                       D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                       IID_PPV_ARGS(&r.upload)))) {
            Fail(r, "create upload buffer", hr);
            return;
        }
        uint8_t* p = nullptr;
        const D3D12_RANGE none = { 0, 0 };
        if (FAILED(hr = r.upload->Map(0, &none, (void**)&p))) { Fail(r, "map", hr); return; }
        QueryPerformanceCounter(&t2);
        hr = src->CopyPixels(nullptr, r.fp.Footprint.RowPitch, (UINT)(total - r.fp.Offset), p + r.fp.Offset);
        QueryPerformanceCounter(&t3);
        r.upload->Unmap(0, nullptr);
        if (FAILED(hr)) { Fail(r, "decode (CopyPixels)", hr); return; }
    }
    QueryPerformanceCounter(&t4);
    r.decodeMs = Ms(t0, t1) + Ms(t2, t3);
    r.createMs = Ms(t1, t2) + Ms(t3, t4);
    r.ok = true;
}

void WorkerMain() {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // own MTA (pre-flight 17)
    {
        ComPtr<IWICImagingFactory> fac;
        const HRESULT fhr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                             IID_PPV_ARGS(&fac));
        for (;;) {
            Job j;
            {
                std::unique_lock<std::mutex> lk(s_mx);
                s_cv.wait(lk, [] { return s_stop || s_haveJob; });
                if (s_stop) break;
                j = std::move(s_job);
                s_job = Job{};
                s_haveJob = false;
                s_running = j.id;
            }
            PhotoResult r;
            r.id = j.id;
            r.path = j.path;
            r.fill = j.fill;
            if (fac) Load(fac.Get(), j, r);
            else     Fail(r, "WIC factory", fhr);
            j.dev.Reset();
            bool drop = false;
            {
                std::lock_guard<std::mutex> lk(s_mx);
                s_running = 0;
                auto it = std::find(s_cancelled.begin(), s_cancelled.end(), j.id);
                if (it != s_cancelled.end()) { s_cancelled.erase(it); drop = true; }
                else if (!s_stop) s_done[j.id] = std::move(r);
                else drop = true;
            }
            s_cv.notify_all();
            (void)drop;                  // a dropped result releases its objects here, on this thread
        }
    }
    if (SUCCEEDED(co)) CoUninitialize();
}

} // namespace

// ---------------------------------------------------------------------------------

void PhotoSetLogger(void (*fn)(const char*)) { s_logger = fn; }

void PhotoLog(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (s_logger) s_logger(buf);
    else fputs(buf, stdout);
}

void PhotoSetCliDir(const wchar_t* dir) {
    s_cliDir.clear();
    if (!dir || !dir[0]) return;
    wchar_t full[MAX_PATH * 2] = {};
    s_cliDir = GetFullPathNameW(dir, MAX_PATH * 2, full, nullptr) ? full : dir;
}
void PhotoSetSynchronous(bool on) { s_sync = on; }
bool PhotoSynchronous() { return s_sync; }

std::wstring PhotoDefaultFolder() {
    if (g_configReadOnly) return s_cliDir;       // --shot: --photos or no folder (pre-flight 22)
    wchar_t d[MAX_PATH] = {};
    GetPhotosDirectory(d);
    return d;
}

int PhotoCount(const std::wstring& folder, unsigned serial) {
    FolderState& fs = Folder(folder, serial);
    return (int)Usable(fs, folder).size();
}

bool PhotoDraw(const std::wstring& folder, unsigned serial, std::wstring& outPath) {
    FolderState& fs = Folder(folder, serial);
    const std::vector<std::wstring> usable = Usable(fs, folder);
    if (usable.empty()) return false;
    std::vector<std::wstring> bag;
    for (const auto& n : usable)
        if (!Contains(fs.shown, n) && !(usable.size() > 1 && _wcsicmp(n.c_str(), fs.lastShown.c_str()) == 0))
            bag.push_back(n);
    if (bag.empty()) {                   // refill: every photo seen once; hold back the last one
        fs.shown.clear();
        for (const auto& n : usable)
            if (!(usable.size() > 1 && _wcsicmp(n.c_str(), fs.lastShown.c_str()) == 0)) bag.push_back(n);
        PhotoLog("[photo] bag refilled: %d of %d files%s\n", (int)bag.size(), (int)usable.size(),
                 usable.size() > 1 && !fs.lastShown.empty() ? " (the last shown one held back)" : "");
    }
    const uint32_t u = BagNext();
    const size_t k = (size_t)(((uint64_t)u * (uint64_t)bag.size()) >> 32);
    outPath = Join(folder, bag[k]);
    PhotoLog("[photo] draw %ls (bag %d of %d usable)\n", bag[k].c_str(), (int)bag.size(), (int)usable.size());
    return true;
}

void PhotoMarkShown(const std::wstring& path) {
    const std::wstring folder = FolderOfPath(path);
    FolderState& fs = s_folders[Key(folder)];
    const std::wstring name = PhotoNameOf(path);
    if (!Contains(fs.shown, name)) fs.shown.push_back(name);
    fs.lastShown = name;
}

void PhotoMarkFailed(const std::wstring& path, const char* why) {
    const std::wstring key = Lower(path);
    if (!Contains(s_failed, key)) s_failed.push_back(key);
    PhotoLog("[photo] skip %ls: %s\n", PhotoNameOf(path), why ? why : "failed");
}

void PhotoBagSeed(unsigned seed) {
    s_bagRng = seed ? ((uint64_t)seed * 0x2545F4914F6CDD1Dull) ^ 0x50484F544F424147ull   // "PHOTOBAG"
                    : (GetTickCount64() ^ ((uint64_t)GetCurrentProcessId() << 32) ^ 0x50484F544F424147ull);
    s_bagSeeded = true;
}

const wchar_t* PhotoNameOf(const std::wstring& path) {
    const size_t sl = path.find_last_of(L"\\/");
    return path.c_str() + (sl == std::wstring::npos ? 0 : sl + 1);
}

uint64_t PhotoWorkerSubmit(const std::wstring& path, ID3D12Device* dev, int outW, int outH, bool fill) {
    std::lock_guard<std::mutex> lk(s_mx);
    if (!s_thread) {
        s_stop = false;
        s_thread = new std::thread(WorkerMain);
    }
    s_job = Job{};
    s_job.id = s_nextId++;
    s_job.path = path;
    s_job.dev = dev;
    s_job.outW = outW;
    s_job.outH = outH;
    s_job.fill = fill;
    s_haveJob = true;                     // replaces any queued (not running) job
    s_cv.notify_all();
    return s_job.id;
}

bool PhotoWorkerTake(uint64_t id, PhotoResult& out) {
    std::lock_guard<std::mutex> lk(s_mx);
    auto it = s_done.find(id);
    if (it != s_done.end()) {
        out = std::move(it->second);
        s_done.erase(it);
        return true;
    }
    if ((s_haveJob && s_job.id == id) || s_running == id) return false;   // still coming
    out = PhotoResult{};
    out.id = id;
    out.why = "the load was lost (worker stopped)";
    return true;
}

void PhotoWorkerCancel(uint64_t id) {
    PhotoResult drop;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        if (s_haveJob && s_job.id == id) { s_haveJob = false; s_job = Job{}; }
        else if (s_running == id) s_cancelled.push_back(id);
        else {
            auto it = s_done.find(id);
            if (it != s_done.end()) { drop = std::move(it->second); s_done.erase(it); }
        }
    }
}

double PhotoWorkerWait(uint64_t id) {
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    {
        std::unique_lock<std::mutex> lk(s_mx);
        s_cv.wait(lk, [id] { return !(s_haveJob && s_job.id == id) && s_running != id; });
    }
    QueryPerformanceCounter(&t1);
    return Ms(t0, t1);
}

void PhotoWorkerStop() {
    std::thread* t = nullptr;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        if (!s_thread) return;
        s_stop = true;
        s_haveJob = false;
        s_job = Job{};
        t = s_thread;
        s_thread = nullptr;
    }
    s_cv.notify_all();
    t->join();
    delete t;
    std::map<uint64_t, PhotoResult> drop;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        drop.swap(s_done);
        s_cancelled.clear();
        s_stop = false;
    }
}
