#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

// Defaults mirror reference/project.json (the shipped Wallpaper Engine values),
// falling back to reference/script.js config for values project.json doesn't set.
struct FluidConfig {
    int   simRes = 256;
    int   dyeRes = 1024;            // project.json ships 4096; raise after perf pass
    float densityDissipation = 0.999f;
    float velocityDissipation = 0.999f;
    float pressureDissipation = 0.85f;
    int   pressureIterations = 20;
    float curl = 48.0f;
    float splatRadius = 0.64f;      // percent, /100 like reference
    bool  shading = true;
    float decayFast = 1.0f;         // 1.0 = WE-original balance. 0.90 starved the field to black:
                                    // wanderer dye (0.1 * 0.15 intensity) died before accumulating.
                                    // Tail snappiness is a settings slider now — user taste, not a default.
    float decayThreshold = 0.29f;
    // M4 HDR output mapping (controlled live from the tray menu)
    int   gamutMode = 2;            // 0 sRGB, 1 Display-P3 (WE parity), 2 BT.2020 (QD-OLED)
    float hdrPeakNits = 0.0f;       // resolved target nits for hot spots; 0 = off (match SDR)
    float hdrKnee = 0.6f;           // dye brightness where highlight expansion starts
    float maxBrightness = 1.35f;
    float satRestore = 0.93f;
    float colorCyclePeriod = 19.0f;
    bool  idleSplats = true;
    float idleInterval = 9.6f;
    int   idleAmount = 8;
    // post color filter — the equivalent of Wallpaper Engine's right-panel
    // color controls the user ran the original with (1/1/1/0 = neutral)
    float postSaturation = 1.0f;
    float postContrast = 1.0f;
    float postBrightness = 1.0f;
    float postHue = 0.0f;           // degrees
    // hue band: constrain the color wheel to a slice around hueCenter.
    // range 180 = the classic full wheel; smaller = themed (e.g. only oranges)
    float hueCenter = 0.0f;         // degrees
    float hueRange = 180.0f;        // degrees half-width
    // color source: random wheel (colorful) vs fixed palette
    bool  colorful = true;
    bool  moreColors = true;
    float splatColors[15] = { 0,1,0,  0,1,1,  0,0,1,  1,0,0,  0.9412f,1,0 };
    bool  splatOnClick = true;     // click burst when holdToSplat is off
    // perf
    float fpsLimit = 60.0f;
    // second monitor: mirror the fluid there (same field, own HDR mapping)
    bool  mirrorSecond = false;
    // wanderers (autonomous roaming splats)
    bool  wanderers = true;
    int   wandererCount = 2;
    int   wandererMode = 0;          // 0 random, 1 circle, 2 figure8
    float wandererSpeed = 246.0f;    // px/s
    float wandererScale = 0.1f;      // path size for circle/figure8
    float wandererResumeDelay = 4.5f;// s of no user input before wanderers resume
    float wandererBrightness = 0.1f;
    // screen-fullness governor
    bool  autoPause = true;
    float darkFloor = 9.0f;          // pause when dark area < this %
    float darkLevel = 0.07f;         // pixel counts as dark below this brightness
    float survDarkFloor = 8.0f;      // survivor wanderer's own floor
    float contrastReq = 30.0f;       // % brightest tile must beat the rest (0 = off)
    // separating dart
    bool  dartEnabled = true;
    float dartInterval = 7.0f;       // s between darts (while group paused)
    float dartSpeed = 967.0f;        // px/s
    // hue-shift cycler (post-process palette rotation)
    bool  hsEnabled = true;
    float hsStep = 83.0f;            // degrees per step
    float hsLinger = 6.5f;
    float hsGlide = 7.2f;
    int   hsBurstSteps = 2;
    float hsOffTime = 10.0f;
    // HDR compensation (the WE original's CSS filter, applied when HDR is on)
    bool  hdrCompensation = true;
    float hdrSaturation = 1.2f;
    float hdrBrightness = 1.08f;
    float hdrContrast = 1.0f;
    // mouse
    bool  showMouse = true;          // cursor movement splats
    bool  holdToSplat = true;        // hold LMB on desktop = continuous splat
    // debug / test switches
    bool  gradientMode = false;     // render the M1 HDR test gradient instead
    bool  stats = false;            // periodic dye-field readback stats to stdout
};

// Per-frame input from the app shell (global cursor, desktop focus).
struct FrameInput {
    float mouseX = 0, mouseY = 0;    // px on our monitor
    float mouseDx = 0, mouseDy = 0;  // px delta * 5, reference scaling
    bool  mouseMoved = false;
    bool  mouseDown = false;         // LMB held with desktop focused
    bool  userInteracted = false;    // resets the wanderer resume timer
};

class FluidRenderer {
public:
    void Init(HWND hwnd, int width, int height, const FluidConfig& cfg);
    void Frame(float dtSec, float sdrScale, bool hdrActive, const FrameInput& input);
    void ReassertColorSpace();
    void Shutdown();
    // live tray-menu controls (peakNits already resolved: actual nits, 0 = off)
    void SetHdrOptions(float peakNits, int gamutMode) {
        m_cfg.hdrPeakNits = peakNits;
        m_cfg.gamutMode = gamutMode;
    }
    // live settings-window access; fields are read per frame, so edits apply
    // immediately (resolution fields excluded from the UI — they need recreate)
    FluidConfig& Config() { return m_cfg; }
    void ReinitWanderers() { InitWanderers(); }
    void SetResolutions(int simRes, int dyeRes);   // recreates sim textures live
    void Reattach(HWND hwnd);   // new swapchain after Explorer restart; sim state survives
    bool PresentBroken() const { return m_presentBroken; }   // window died mid-frame
    // second-monitor mirror
    void EnableMirror(HWND hwnd, int width, int height);
    void DisableMirror();
    bool MirrorActive() const { return m_mirrorChain != nullptr; }
    bool MirrorBroken() const { return m_mirrorBroken; }
    void SetMirrorHdr(float sdrScale, float peakNits) {
        m_mirrorSdrScale = sdrScale;
        m_mirrorPeakNits = peakNits;
    }

    // HDR analyzer: parallel low-res render of the FINAL scRGB output
    // (post gamut/peak mapping), read back ~10x/s for the analyzer window.
    static const int kAnaW = 640, kAnaH = 360;
    void EnableAnalyzer(bool on) { m_anaEnabled = on; }
    bool ReadAnalyzerFrame(std::vector<float>& outRgba);  // true if a new frame landed
    IDXGISwapChain3* SwapChain() { return m_swapChain.Get(); }

private:
    struct Tex {
        Microsoft::WRL::ComPtr<ID3D12Resource> res;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        D3D12_GPU_DESCRIPTOR_HANDLE srv = {};
        D3D12_GPU_DESCRIPTOR_HANDLE uav = {};
        int w = 0, h = 0;
    };
    struct DoubleTex { Tex a, b; Tex* read = &a; Tex* write = &b; void Swap() { Tex* t = read; read = write; write = t; } };

    void CreateDevice(HWND hwnd, int width, int height);
    void CreateSimResources();
    Tex  CreateTex(int w, int h, DXGI_FORMAT fmt, int heapSlot);
    void Transition(Tex& t, D3D12_RESOURCE_STATES to);
    void UavBarrier(ID3D12Resource* res);
    void BeginFrame();
    void EndFrameAndPresent();
    void SimStep(float dt);
    void Splat(float x, float y, float dx, float dy, float r, float g, float b);
    void MultipleSplats(int amount);
    void RenderDisplay();
    void RenderMirror();
    void BuildDisplayConstants(float out[24]);
    void BuildDisplayConstantsEx(float out[24], int w, int h, float sdrScale, float peakNits);
    void MaybeRenderAnalyzer();
    void CreateAnalyzerResources();
    void RenderGradient(float timeSec);
    void ReportStats();
    void WaitForGpuIdle();
    // M3 behaviors (ports of the reference's custom features)
    void InitWanderers();
    float WheelHue(float offset);
    void PickSplatColor(float hueOffset, float out[3]);
    void UpdateWanderers(float dt);
    void UpdateDart(float dt);
    void UpdateHueShift(float dt);
    void UpdateCoverage();          // schedule/process the 48x27 governor readback
    void ProcessCoverage(const uint8_t* data, UINT pitch);
    void HandleInput(const FrameInput& in);

    FluidConfig m_cfg;
    int m_width = 0, m_height = 0;
    int m_simW = 0, m_simH = 0, m_dyeW = 0, m_dyeH = 0;
    float m_time = 0.0f;
    float m_emitScale = 1.0f;   // dt / (1/60): keeps per-second dye emission fps-independent
    float m_sdrScale = 1.0f;
    float m_globalHue = 0.0f;
    float m_idleTimer = 0.0f;
    float m_statsTimer = 0.0f;
    bool  m_hdrActive = false;
    bool  m_firstFrame = true;

    static const UINT kFrames = 3;
    Microsoft::WRL::ComPtr<IDXGIFactory6> m_factory;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_backBuffers[kFrames];
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_allocators[kFrames];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_cmd;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    UINT64 m_fenceValues[kFrames] = {};
    UINT64 m_nextFence = 1;
    UINT m_rtvStride = 0;
    UINT m_frameIndex = 0;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;   // shader visible
    UINT m_srvStride = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_computeRS;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_graphicsRS;
    // compute PSOs
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoClearV, m_psoClear4, m_psoClear1,
        m_psoCurl, m_psoVorticity, m_psoDivergence, m_psoClearPressure, m_psoPressure,
        m_psoGradSub, m_psoAdvectVel, m_psoAdvectDye, m_psoSplatVel, m_psoSplatDye,
        m_psoDownsample;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoDisplay;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoGradient;

    DoubleTex m_velocity, m_dye, m_pressure;
    Tex m_divergence, m_curl;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_readback;  // stats
    UINT m_readbackPitch = 0;
    bool m_readbackPending = false;

    // --- M3 state ---
    bool m_prevMouseDown = false;
    bool m_presentBroken = false;

    // second-monitor mirror
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_mirrorChain;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_mirrorBuffers[3];
    int   m_mirrorW = 0, m_mirrorH = 0;
    float m_mirrorSdrScale = 1.0f, m_mirrorPeakNits = 0.0f;
    bool  m_mirrorBroken = false;
    struct Wanderer {
        float x, y, heading, turn;   // random-wander state
        float cx, cy, R, phase;      // circle/figure8 state
        int   dir;
        float hueOffset;
    };
    std::vector<Wanderer> m_wanderers;
    float m_lastInteraction = -1000.0f;

    // coverage governor (48x27 downsample, async readback, 1 Hz)
    static const int kCovW = 48, kCovH = 27;
    Tex m_coverage;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_covReadback;
    UINT   m_covPitch = 0;
    UINT64 m_covFence = 0;
    bool   m_covPending = false;
    float  m_lastCovTime = -10.0f;
    bool   m_screenTooFull = false;
    bool   m_survivorTooFull = false;

    struct { float x, y, ux, uy, left; bool active = false; } m_dart;
    float m_lastDartTime = -1000.0f;

    // hue-shift cycler: 0 off, 1 glide, 2 linger, 3 return
    int   m_hsPhase = 0;
    float m_hsTimer = 0, m_hsFrom = 0, m_hsTo = 0;
    int   m_hsStep = 0;
    float m_hueAngle = 0;

    // HDR analyzer
    bool   m_anaEnabled = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_anaTex;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_anaReadback;
    D3D12_RESOURCE_STATES m_anaState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    UINT   m_anaPitch = 0;
    UINT64 m_anaFence = 0;
    bool   m_anaPending = false;
    float  m_lastAnaTime = -10.0f;
};
