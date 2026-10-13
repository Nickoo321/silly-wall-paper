// HDR analyzer window: false-color nits heat-map of the wallpaper's final
// scRGB output. Click to freeze, hover to read exact nits + color values,
// H toggles heat-map vs actual image. Opened from the tray menu.

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <vector>
#include <cstdio>
#include <cmath>
#include "app_state.h"

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static HWND s_wnd = nullptr;
static HFONT s_font = nullptr;
static bool s_paused = false;
static bool s_heatmap = true;
static std::vector<float> s_frame;      // kAnaW*kAnaH*4 scRGB floats
static std::vector<uint8_t> s_dib;      // BGRA, top-down
static int s_mouseX = -1, s_mouseY = -1;

static const int kImgW = 1280, kImgH = 720, kStatusH = 30;

// log-scale false color: 0.05 nits (OLED black-ish) .. 1500 nits
static void FalseColor(float nits, uint8_t* bgra) {
    float t = 0.0f;
    if (nits > 0.05f)
        t = logf(nits / 0.05f) / logf(1500.0f / 0.05f);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    static const float stops[7][3] = {   // RGB
        { 0, 0, 0 }, { 30, 30, 160 }, { 0, 190, 230 }, { 0, 190, 40 },
        { 240, 230, 0 }, { 255, 70, 0 }, { 255, 255, 255 },
    };
    float f = t * 6.0f;
    int i = (int)f;
    if (i > 5) i = 5;
    float fr = f - i;
    bgra[0] = (uint8_t)(stops[i][2] + (stops[i + 1][2] - stops[i][2]) * fr);
    bgra[1] = (uint8_t)(stops[i][1] + (stops[i + 1][1] - stops[i][1]) * fr);
    bgra[2] = (uint8_t)(stops[i][0] + (stops[i + 1][0] - stops[i][0]) * fr);
    bgra[3] = 255;
}

static uint8_t LinearToSrgb8(float v) {
    if (v <= 0) return 0;
    if (v >= 1) return 255;
    float s = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
    return (uint8_t)(s * 255.0f + 0.5f);
}

static float LumNits(const float* px) {
    // scRGB: 1.0 = 80 nits; Rec.709 luminance
    float y = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
    return y * 80.0f;
}

static void RebuildDib() {
    if (s_frame.empty()) return;
    const int W = FluidRenderer::kAnaW, H = FluidRenderer::kAnaH;
    s_dib.resize((size_t)W * H * 4);
    for (int i = 0; i < W * H; i++) {
        const float* px = s_frame.data() + (size_t)i * 4;
        uint8_t* d = s_dib.data() + (size_t)i * 4;
        if (s_heatmap) {
            FalseColor(LumNits(px), d);
        } else {
            d[0] = LinearToSrgb8(px[2]);
            d[1] = LinearToSrgb8(px[1]);
            d[2] = LinearToSrgb8(px[0]);
            d[3] = 255;
        }
    }
}

static void PaintWindow(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    const int W = FluidRenderer::kAnaW, H = FluidRenderer::kAnaH;

    if (!s_dib.empty()) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;   // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, 0, 0, kImgW, kImgH, 0, 0, W, H,
                      s_dib.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    } else {
        RECT r = { 0, 0, kImgW, kImgH };
        FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }

    // status strip (dark)
    static HBRUSH darkStrip = CreateSolidBrush(RGB(30, 30, 36));
    RECT sr = { 0, kImgH, kImgW, kImgH + kStatusH };
    FillRect(dc, &sr, darkStrip);
    SelectObject(dc, s_font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(220, 220, 228));

    wchar_t text[320];
    if (s_mouseX >= 0 && s_mouseY >= 0 && !s_frame.empty()) {
        int bx = s_mouseX * FluidRenderer::kAnaW / kImgW;
        int by = s_mouseY * FluidRenderer::kAnaH / kImgH;
        const float* px = s_frame.data() +
            ((size_t)by * FluidRenderer::kAnaW + bx) * 4;
        float lum = LumNits(px);
        float mxNits = fmaxf(px[0], fmaxf(px[1], px[2])) * 80.0f;
        // color swatch of the hovered pixel
        RECT sw = { 8, kImgH + 6, 26, kImgH + kStatusH - 6 };
        HBRUSH b = CreateSolidBrush(RGB(LinearToSrgb8(px[0]), LinearToSrgb8(px[1]), LinearToSrgb8(px[2])));
        FillRect(dc, &sw, b);
        DeleteObject(b);
        FrameRect(dc, &sw, (HBRUSH)GetStockObject(GRAY_BRUSH));
        swprintf_s(text,
            L"%4d,%4d   %7.1f nits (luminance)   %7.1f nits (max ch)   scRGB  R %+.3f  G %+.3f  B %+.3f      %s%s | %.0f fps   [click] freeze   [H] heat/image",
            bx, by, lum, mxNits, px[0], px[1], px[2],
            s_paused ? L"FROZEN" : L"live", s_heatmap ? L" | heat-map" : L" | image",
            g_currentFps);
        RECT tr = { 34, kImgH + 6, kImgW - 8, kImgH + kStatusH };
        DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    } else {
        swprintf_s(text,
            L"hover a pixel for readout      %s%s | %.0f fps   [click] freeze   [H] heat-map/image      scale: black 0 → blue 1 → cyan 10 → green 50 → yellow 240 → red 800 → white 1500 nits",
            s_paused ? L"FROZEN" : L"live", s_heatmap ? L" | heat-map" : L" | image",
            g_currentFps);
        RECT tr = { 8, kImgH + 6, kImgW - 8, kImgH + kStatusH };
        DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK AnalyzerWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 100, nullptr);
        return 0;
    case WM_TIMER: {
        if (s_paused || !g_renderer) return 0;
        std::vector<float> fresh;
        if (g_renderer->ReadAnalyzerFrame(fresh)) {
            s_frame = std::move(fresh);
            RebuildDib();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (x >= 0 && x < kImgW && y >= 0 && y < kImgH) {
            s_mouseX = x; s_mouseY = y;
        } else {
            s_mouseX = s_mouseY = -1;
        }
        RECT sr = { 0, kImgH, kImgW, kImgH + kStatusH };
        InvalidateRect(hwnd, &sr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
        s_paused = !s_paused;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_CHAR:
        if (wp == 'h' || wp == 'H') {
            s_heatmap = !s_heatmap;
            RebuildDib();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT:
        PaintWindow(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (g_renderer) g_renderer->EnableAnalyzer(false);
        s_wnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ShowAnalyzerWindow() {
    if (s_wnd) {
        ShowWindow(s_wnd, SW_SHOW);
        SetForegroundWindow(s_wnd);
        return;
    }
    if (!g_renderer) return;
    if (!s_font) s_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = AnalyzerWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"FluidWallpaperAnalyzer";
        wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        wc.hbrBackground = nullptr;
        RegisterClassW(&wc);
        registered = true;
    }

    g_renderer->EnableAnalyzer(true);
    s_paused = false;

    RECT r = { 0, 0, kImgW, kImgH + kStatusH };
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    s_wnd = CreateWindowExW(0, L"FluidWallpaperAnalyzer",
                            L"Fluid Wallpaper — HDR Analyzer (nits heat-map)",
                            WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top,
                            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(s_wnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    ShowWindow(s_wnd, SW_SHOW);
    SetForegroundWindow(s_wnd);
}
