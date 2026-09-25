
cbuffer Consts : register(b0) {
    float2 res;
    float  time;
    float  hdrOn;
    float  page;    // 0 = M1 gradient; 1..3 = calibration quiz pages
};

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float3 HueToRGB(float h) {
    float r = abs(h * 6.0 - 3.0) - 1.0;
    float g = 2.0 - abs(h * 6.0 - 2.0);
    float b = 2.0 - abs(h * 6.0 - 4.0);
    return saturate(float3(r, g, b));
}

// Quiz page 1 — black level. Ten vertical strips at near-black gray levels
// (scRGB linear; 1.0 = 80 nits). Bottom edge of each strip shows strip-number
// notches. User reports the lowest-numbered strip visible -> shadow_floor.
float4 PageBlackLevel(float2 uv) {
    float lv[10] = { 0.002, 0.004, 0.006, 0.008, 0.010,
                     0.015, 0.020, 0.030, 0.050, 0.080 };
    int i = min(9, (int)(uv.x * 10.0));
    float fx = frac(uv.x * 10.0);
    float v = (fx > 0.04 && fx < 0.96) ? lv[i] : 0.0;
    if (uv.y < 0.04) {   // notch row: strip index+1 white cells out of 10
        int cell = (int)(frac(uv.x * 10.0) * 10.0);
        v = (cell < i + 1) ? 0.5 : 0.0;
    }
    return float4(v, v, v, 1.0);
}

// Quiz page 2 — saturation. Rows = hues, columns = CSS-saturate factor
// applied in gamma space (like the app's post filter), then gamma-decoded.
// User reports the most pleasing column (1-6, left to right).
float4 PageSaturation(float2 uv) {
    float hues[4] = { 0.0, 0.08, 0.33, 0.60 };
    float sats[6] = { 0.8, 1.0, 1.2, 1.4, 1.6, 2.0 };
    int r = min(3, (int)(uv.y * 4.0));
    int c = min(5, (int)(uv.x * 6.0));
    float3 col = HueToRGB(hues[r]) * 0.6;
    float l = dot(col, float3(0.2126, 0.7152, 0.0722));
    col = max(l + (col - l) * sats[c], 0.0);
    if (frac(uv.x * 6.0) < 0.02 || frac(uv.y * 4.0) < 0.04) col = 0.0;
    return float4(pow(col, 2.2), 1.0);
}

// Quiz page 3 — blowout ladder. Horizontal bands of near-white at rising
// nits (scRGB: 1.0 = 80 nits). Band 1 is at the BOTTOM. User reports the
// "almost blown but not painful" band -> peak_nits / max_brightness.
float4 PageBlowout(float2 uv) {
    float nits[10] = { 80, 150, 240, 300, 400, 500, 600, 800, 1000, 1500 };
    int i = min(9, (int)(uv.y * 10.0));
    float v = nits[i] / 80.0;
    if (hdrOn < 0.5) v = min(v, 1.0);
    if (frac(uv.y * 10.0) < 0.03) v = 0.0;
    return float4(v, v * 0.98, v * 0.95, 1.0);
}

float4 PSMain(float4 pos : SV_Position) : SV_Target {
    float2 uv = pos.xy / res;
    int p = (int)(page + 0.5);
    if (p == 1) return PageBlackLevel(uv);
    if (p == 2) return PageSaturation(uv);
    if (p == 3) return PageBlowout(uv);
    if (uv.y > 0.88) {
        float levels[5] = { 1.0, 2.0, 4.0, 8.0, 12.5 };
        int i = min(4, (int)(uv.x * 5.0));
        float fx = frac(uv.x * 5.0);
        float v = (fx > 0.02 && fx < 0.98) ? levels[i] : 0.0;
        if (hdrOn < 0.5) v /= 12.5;
        return float4(v, v, v, 1.0);
    }
    float y = uv.y / 0.88;
    float hue = frac(uv.x + time * 0.03);
    float3 c = HueToRGB(hue);
    float ramp = lerp(4.0, 0.02, pow(y, 1.5));
    float band = 0.5 + 0.5 * sin(6.28318 * (y * 2.0 - time * 0.25));
    float brightness = ramp * (0.6 + 0.8 * band * band);
    if (hdrOn < 0.5) brightness /= 5.6;
    return float4(c * brightness, 1.0);
}
