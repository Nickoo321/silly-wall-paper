// photo.h -- brief BY PHOTO-STAGE, phase 1 (reference/briefs/PHOTO-STAGE.md +
// the binding auditor pre-flight items 1-34 + Fable decisions).
//
// A photo stage is a cycle stage whose FILE has a [photo] section (or whose
// [cycle] entry carries stage_K_photo=1): it shows ONE still from a folder for
// its dwell, entered and left through black like any look change. This file
// holds everything that is not the director or the renderer:
//   - the folder scan (draw time, non-recursive, extension filter, sorted,
//     cached once per director tick; cloud placeholders skipped),
//   - the shuffle bag (its OWN splitmix stream -- never the director's
//     NextRand, so the cycle walk is untouched),
//   - the WIC worker: one persistent thread, its own MTA + IWICImagingFactory,
//     one load at a time. It decodes (EXIF orientation, 8-bit BGRA or 16-bit
//     RGBA64, Fant-scaled to the fit rect in OUTPUT pixels, fill = cover +
//     centred clip) straight into a D3D12 upload buffer it creates, next to
//     the default texture (COPY_DEST) it also creates (the device is
//     free-threaded). The render thread only records the copy.
// Nothing here runs unless a photo stage is reached: no photo stage in the
// [cycle] list = no scan, no thread, no GPU object (parity + identity).
#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>

// ---- logging / shell ---------------------------------------------------------
void PhotoSetLogger(void (*fn)(const char*));
void PhotoLog(const char* fmt, ...);
// --photos <dir> (shot mode): the folder an EMPTY [photo] folder= means under
// --shot. Without it a --shot run has NO default folder (pre-flight 22: the
// shot's g_iniPath is the real %APPDATA%, identity must not depend on the
// user's photos).
void PhotoSetCliDir(const wchar_t* dir);
// Shot mode: the director JOINS the worker at the black point (pre-flight 27),
// so the timeline stays frame-deterministic. Live: black holds up to 5 s.
void PhotoSetSynchronous(bool on);
bool PhotoSynchronous();
// The folder an empty [photo] folder= resolves to right now: live =
// GetPhotosDirectory() (dirname(settings.ini)\photos), shot = --photos or ""
// (no folder).
std::wstring PhotoDefaultFolder();

// ---- scan + bag (cycle.cpp) --------------------------------------------------
// Usable files in `folder` (sorted, filtered, minus the ones that failed this
// run). `serial` = the director's tick serial: one scan per tick per folder.
int  PhotoCount(const std::wstring& folder, unsigned serial);
// Draw one file from the folder's shuffle bag: every photo once before any
// repeat, never the last-shown one right after a refill while more than one
// exists. false = nothing drawable.
bool PhotoDraw(const std::wstring& folder, unsigned serial, std::wstring& outPath);
// A file counts as SHOWN only when its fade-in starts (pre-flight 25).
void PhotoMarkShown(const std::wstring& path);
// Out of the bag for the rest of the run (decode failure, timeout).
void PhotoMarkFailed(const std::wstring& path, const char* why);
// The bag's own stream: (director seed) ^ a constant; 0 = wall clock.
void PhotoBagSeed(unsigned seed);
const wchar_t* PhotoNameOf(const std::wstring& path);   // file name part

// ---- the worker (fluid.cpp) --------------------------------------------------
enum PhotoState { PHOTO_NONE = 0, PHOTO_LOADING, PHOTO_SHOWN, PHOTO_FAILED };

struct PhotoResult {
    uint64_t id = 0;
    bool     ok = false;
    std::string why;                                    // failure reason
    std::wstring path;
    Microsoft::WRL::ComPtr<ID3D12Resource> tex;          // DEFAULT heap, COPY_DEST
    Microsoft::WRL::ComPtr<ID3D12Resource> upload;       // UPLOAD heap, filled + unmapped
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    int  texW = 0, texH = 0;                             // the fit rect (fill: the output)
    int  srcW = 0, srcH = 0;                             // the file, before orientation
    int  orient = 1;                                     // EXIF 1..8
    int  bpc = 8;                                        // source bits per channel
    bool alpha = false;                                  // composite the alpha over black
    bool fill = false;
    bool icc = false;                                    // embedded profile (treated as sRGB)
    bool scaled = false;                                 // the Fant scaler ran
    double decodeMs = 0.0;                               // WIC chain incl. CopyPixels
    double createMs = 0.0;                               // texture + upload buffer + map
};

// Submit a load (replaces any queued one). Returns its id.
uint64_t PhotoWorkerSubmit(const std::wstring& path, ID3D12Device* dev, int outW, int outH, bool fill);
// true once the job `id` finished (ok or not); its result moves to `out`.
bool PhotoWorkerTake(uint64_t id, PhotoResult& out);
// Discard job `id` (its result is dropped when it lands).
void PhotoWorkerCancel(uint64_t id);
// Block until job `id` has finished (shot mode). Returns the ms waited.
double PhotoWorkerWait(uint64_t id);
// Cancel everything and join the thread (renderer Shutdown, before the device
// goes). The next submit restarts it.
void PhotoWorkerStop();
