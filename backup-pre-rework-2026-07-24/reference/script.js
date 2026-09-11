'use strict';

const canvas = document.getElementsByTagName('canvas')[0];
canvas.width = canvas.clientWidth;
canvas.height = canvas.clientHeight;

Array.prototype.getRandom = function() {
    return this[Math.floor(Math.random() * this.length)];
};

let splatColors = [{ r: 0, g: 0.15, b: 0 }];

// slow color wheel position (0..1) -- must exist before the first splats fire
let globalHue = Math.random();

let idleSplats;

function idleSplatsFunction() {
    multipleSplats(config.RANDOM_AMOUNT);
}

let config = {
    SIM_RESOLUTION: 256,
    DYE_RESOLUTION: 1024,
    DENSITY_DISSIPATION: 0.97,
    VELOCITY_DISSIPATION: 0.98,
    PRESSURE_DISSIPATION: 0.8,
    PRESSURE_ITERATIONS: 20,
    CURL: 30,
    SPLAT_RADIUS: 0.3,
    SHADING: true,
    COLORFUL: true,
    PAUSED: false,
    BACK_COLOR: { r: 0, g: 0, b: 0 },
    TRANSPARENT: false,
    BLOOM: true,
    BLOOM_ITERATIONS: 8,
    BLOOM_RESOLUTION: 256,
    BLOOM_INTENSITY: 0.8,
    BLOOM_THRESHOLD: 0.6,
    BLOOM_SOFT_KNEE: 0.7,
    POINTER_COLOR: [{ r: 0, g: 0.15, b: 0 }],
    SOUND_SENSITIVITY: 0.25,
    AUDIO_RESPONSIVE: true,
    FREQ_RANGE: 8,
    FREQ_RANGE_START: 0,
    IDLE_SPLATS: false,
    RANDOM_AMOUNT: 10,
    RANDOM_INTERVAL: 1,
    SPLAT_ON_CLICK: true,
    SHOW_MOUSE_MOVEMENT: true,
    FRAME_INTERVAL_MS: 1000 / 60,
    STEP_SIZE_S: 0.016,
    IGNORE_FPS_LIMIT: false,
    // --- custom additions ---
    WANDERERS: true,            // autonomous roaming splats
    WANDERER_COUNT: 3,
    WANDERER_MODE: 'random',    // 'random' | 'circle' | 'figure8'
    WANDERER_SPEED: 300,        // px per second
    WANDERER_SCALE: 0.35,       // path size (fraction of screen) for circle/figure8
    WANDERER_RESUME_DELAY: 4,   // seconds of no user input before wanderers resume
    HOLD_TO_SPLAT: true,        // hold M1 = continuous splat (replaces click burst)
    COLOR_CYCLE_PERIOD: 30,     // seconds for one full trip around the color wheel
    DECAY_FAST: 0.90,           // decay applied to faint dye (the fast tail)
    DECAY_THRESHOLD: 0.08,      // brightness below which decay ramps from slow->fast
    MAX_BRIGHTNESS: 1.2,        // hue-preserving dye ceiling (stops white blowout)
    SAT_RESTORE: 0.5,           // fraction of lost saturation recovered per second
    HDR_MODE: 'auto',           // 'auto' | 'on' | 'off' -- compensation when Windows HDR is active
    HDR_SATURATION: 1.2,
    HDR_BRIGHTNESS: 1.08,
    HDR_CONTRAST: 1.0,
    WIDE_GAMUT: true,           // render buffer in Display-P3 when supported
    AUTO_PAUSE: true,           // pause wanderers when the screen gets too full
    DARK_FLOOR: 20,             // pause when dark area drops below this % of screen
    DARK_LEVEL: 0.02,           // a pixel counts as dark below this brightness (0..1)
    WANDERER_BRIGHTNESS: 0.5,   // dye intensity of wanderer splats (mouse stays full)
    SURV_DARK_FLOOR: 8,         // survivor wanderer's own pause floor (dark area %)
    CONTRAST_REQ: 30,           // % one tile must exceed the rest by, else keep painting (0 = off)
    DART_ENABLED: true,         // separating dart while the group is paused
    DART_INTERVAL: 6,           // seconds between darts
    DART_SPEED: 2500,           // dart speed, px/s
    HS_ENABLED: true,           // hue-shift cycler (post-process palette rotation)
    HS_STEP: 60,                // degrees per step
    HS_LINGER: 3,               // seconds held at each step
    HS_GLIDE: 2,                // seconds gliding between steps
    HS_BURST_STEPS: 3,          // steps per burst
    HS_OFF_TIME: 20             // dormant seconds between bursts (rotation at 0)
};

document.addEventListener("DOMContentLoaded", () => {   
    window.wallpaperPropertyListener = {
        applyUserProperties: (properties) => {
            if (properties.bloom_intensity) config.BLOOM_INTENSITY = properties.bloom_intensity.value;
            if (properties.bloom_threshold) config.BLOOM_THRESHOLD = properties.bloom_threshold.value;
            if (properties.colorful) config.COLORFUL = properties.colorful.value;
            if (properties.density_diffusion) config.DENSITY_DISSIPATION = properties.density_diffusion.value;
            if (properties.enable_bloom) config.BLOOM = properties.enable_bloom.value;
            if (properties.paused) config.PAUSED = properties.paused.value;
            if (properties.pressure_diffusion) config.PRESSURE_DISSIPATION = properties.pressure_diffusion.value;
            if (properties.shading) config.SHADING = properties.shading.value;
            if (properties.splat_radius) config.SPLAT_RADIUS = properties.splat_radius.value;
            if (properties.velocity_diffusion) config.VELOCITY_DISSIPATION = properties.velocity_diffusion.value;
            if (properties.vorticity) config.CURL = properties.vorticity.value;
            if (properties.sound_sensitivity) config.SOUND_SENSITIVITY = properties.sound_sensitivity.value;
            if (properties.audio_responsive) config.AUDIO_RESPONSIVE = properties.audio_responsive.value;
            if (properties.simulation_resolution) {
                config.SIM_RESOLUTION = properties.simulation_resolution.value;
                initFramebuffers();
            }
            if (properties.dye_resolution) {
                config.DYE_RESOLUTION = properties.dye_resolution.value;
                initFramebuffers();
            }
            if (properties.splat_color) {
                splatColors[0] = rgbToPointerColor(properties.splat_color.value);
                if (!config.COLORFUL) config.POINTER_COLOR = [splatColors[0]];
            }
            if (properties.splat_color_2) splatColors[1] = rgbToPointerColor(properties.splat_color_2.value);
            if (properties.splat_color_3) splatColors[2] = rgbToPointerColor(properties.splat_color_3.value);
            if (properties.splat_color_4) splatColors[3] = rgbToPointerColor(properties.splat_color_4.value);
            if (properties.splat_color_5) splatColors[4] = rgbToPointerColor(properties.splat_color_5.value);
            if (properties.background_color) {
                let c = properties.background_color.value.split(" "),
                r = Math.floor(c[0]*255),
                g = Math.floor(c[1]*255),
                b = Math.floor(c[2]*255);
                document.body.style.backgroundColor = `rgb(${r}, ${g}, ${b})`;
                config.BACK_COLOR.r = r;
                config.BACK_COLOR.g = g;
                config.BACK_COLOR.b = b;
            }
            if (properties.more_colors && !properties.more_colors.value) {
                config.POINTER_COLOR = [splatColors[0]];
            } else if (properties.more_colors && properties.more_colors.value) {
                config.POINTER_COLOR = splatColors;
            }
            if (properties.use_background_image) config.TRANSPARENT = properties.use_background_image.value;
            if (properties.background_image) canvas.style.backgroundImage = `url("file:///${properties.background_image.value}")`;
            if (properties.repeat_background) canvas.style.backgroundRepeat = properties.repeat_background.value ? "repeat" : "no-repeat";
            if (properties.background_image_size) canvas.style.backgroundSize = properties.background_image_size.value;
            if (properties.frequency_range) {
                config.FREQ_RANGE = properties.frequency_range.value;

                if (config.FREQ_RANGE + config.FREQ_RANGE_START > 61) {
                    config.FREQ_RANGE_START = 62 - config.FREQ_RANGE;
                }
            }
            if (properties.frequency_range_start) {
                if (config.FREQ_RANGE + properties.frequency_range_start.value > 61) {
                    config.FREQ_RANGE_START = 62 - config.FREQ_RANGE;
                } else {
                    config.FREQ_RANGE_START = properties.frequency_range_start.value;
                }
            }
            if (properties.idle_random_splats) {
                config.IDLE_SPLATS = properties.idle_random_splats.value;
                if (properties.idle_random_splats.value) {
                    idleSplats = setInterval(idleSplatsFunction, config.RANDOM_INTERVAL * 1000);
                } else {
                    clearInterval(idleSplats);
                }
            }
            if (properties.random_splat_interval) {
                config.RANDOM_INTERVAL = properties.random_splat_interval.value;
                if (config.IDLE_SPLATS) {
                    clearInterval(idleSplats);
                    idleSplats = setInterval(idleSplatsFunction, config.RANDOM_INTERVAL * 1000);
                }
            }
            if (properties.random_splat_amount) {
                config.RANDOM_AMOUNT = properties.random_splat_amount.value;
                if (config.IDLE_SPLATS) {
                    clearInterval(idleSplats);
                    idleSplats = setInterval(idleSplatsFunction, config.RANDOM_INTERVAL * 1000);
                }
            }
            if (properties.splat_on_click) config.SPLAT_ON_CLICK = properties.splat_on_click.value;
            if (properties.hold_to_splat) config.HOLD_TO_SPLAT = properties.hold_to_splat.value;
            if (properties.auto_splats) config.WANDERERS = properties.auto_splats.value;
            if (properties.wanderer_count) { config.WANDERER_COUNT = properties.wanderer_count.value; initWanderers(); }
            if (properties.wanderer_mode) { config.WANDERER_MODE = properties.wanderer_mode.value; initWanderers(); }
            if (properties.wanderer_speed) config.WANDERER_SPEED = properties.wanderer_speed.value;
            if (properties.wanderer_scale) { config.WANDERER_SCALE = properties.wanderer_scale.value; initWanderers(); }
            if (properties.wanderer_resume_delay) config.WANDERER_RESUME_DELAY = properties.wanderer_resume_delay.value;
            if (properties.color_cycle_period) config.COLOR_CYCLE_PERIOD = properties.color_cycle_period.value;
            if (properties.decay_fast) config.DECAY_FAST = properties.decay_fast.value;
            if (properties.decay_threshold) config.DECAY_THRESHOLD = properties.decay_threshold.value;
            if (properties.max_brightness) config.MAX_BRIGHTNESS = properties.max_brightness.value;
            if (properties.saturation_restore) config.SAT_RESTORE = properties.saturation_restore.value;
            if (properties.hdr_mode) { config.HDR_MODE = properties.hdr_mode.value; applyHDRFilter(); }
            if (properties.hdr_saturation) { config.HDR_SATURATION = properties.hdr_saturation.value; applyHDRFilter(); }
            if (properties.hdr_brightness) { config.HDR_BRIGHTNESS = properties.hdr_brightness.value; applyHDRFilter(); }
            if (properties.hdr_contrast) { config.HDR_CONTRAST = properties.hdr_contrast.value; applyHDRFilter(); }
            if (properties.wide_gamut) { config.WIDE_GAMUT = properties.wide_gamut.value; applyWideGamut(); }
            if (properties.auto_pause) config.AUTO_PAUSE = properties.auto_pause.value;
            if (properties.dark_floor) config.DARK_FLOOR = properties.dark_floor.value;
            if (properties.dark_level) config.DARK_LEVEL = properties.dark_level.value;
            if (properties.wanderer_brightness) config.WANDERER_BRIGHTNESS = properties.wanderer_brightness.value;
            if (properties.surv_dark_floor) config.SURV_DARK_FLOOR = properties.surv_dark_floor.value;
            if (properties.contrast_req) config.CONTRAST_REQ = properties.contrast_req.value;
            if (properties.dart_enabled) config.DART_ENABLED = properties.dart_enabled.value;
            if (properties.dart_interval) config.DART_INTERVAL = properties.dart_interval.value;
            if (properties.dart_speed) config.DART_SPEED = properties.dart_speed.value;
            if (properties.hueshift_enabled) config.HS_ENABLED = properties.hueshift_enabled.value;
            if (properties.hueshift_step) config.HS_STEP = properties.hueshift_step.value;
            if (properties.hueshift_linger) config.HS_LINGER = properties.hueshift_linger.value;
            if (properties.hueshift_glide) config.HS_GLIDE = properties.hueshift_glide.value;
            if (properties.hueshift_burst_steps) config.HS_BURST_STEPS = properties.hueshift_burst_steps.value;
            if (properties.hueshift_off_time) config.HS_OFF_TIME = properties.hueshift_off_time.value;
            // any settings interaction can rebuild the surface: re-assert color state
            applyWideGamut();
            applyHDRFilter();
            if (properties.show_mouse_movement) config.SHOW_MOUSE_MOVEMENT = properties.show_mouse_movement.value;
            if (properties.ignore_fps_limit) config.IGNORE_FPS_LIMIT = properties.ignore_fps_limit.value;
        },
        applyGeneralProperties: (properties) => {
            if (properties.fps) config.FRAME_INTERVAL_MS = 1000 / properties.fps;
	    }
    };

    window.wallpaperRegisterAudioListener((audioArray) => {
        if (!config.AUDIO_RESPONSIVE) return;
        if (audioArray[0] > 5) return;

        let bass = 0.0;
        let half = Math.floor(audioArray.length / 2);

        for (let i = 0; i <= config.FREQ_RANGE; i++) {
            bass += audioArray[i + config.FREQ_RANGE_START];
            bass += audioArray[half + (i + config.FREQ_RANGE_START)];
        }
        bass /= (config.FREQ_RANGE * 2);
        multipleSplats(Math.floor((bass * config.SOUND_SENSITIVITY) * 10));
    });
});

function indexOfMax(arr) {
    if (arr.length === 0) {
        return -1;
    }

    var max = arr[0];
    var maxIndex = 0;

    for (var i = 1; i < arr.length; i++) {
        if (arr[i] > max) {
            maxIndex = i;
            max = arr[i];
        }
    }

    return maxIndex;
}

class pointerPrototype {
    constructor() {
        this.id = -1;
        this.x = 0;
        this.y = 0;
        this.dx = 0;
        this.dy = 0;
        this.down = false;
        this.moved = false;
        this.color = config.COLORFUL ? generateColor() : config.POINTER_COLOR.getRandom();
    }
}

let pointers = [];
let splatStack = [];
let bloomFramebuffers = [];
pointers.push(new pointerPrototype());

const { gl, ext } = getWebGLContext(canvas);

// --- HDR handling ---------------------------------------------------------
// Web wallpapers can only output SDR; when Windows HDR is ON, the OS remaps
// SDR content and it loses punch. We detect the switch and compensate.
let hdrDisplayActive = false;

function detectHDR () {
    try {
        var mq = window.matchMedia && window.matchMedia('(dynamic-range: high)');
        return !!(mq && mq.matches);
    } catch (e) { return false; }
}

var hueAngle = 0; // current hue-rotate angle, degrees

function applyHDRFilter () {
    var on = config.HDR_MODE === 'on' || (config.HDR_MODE === 'auto' && hdrDisplayActive);
    var parts = [];
    if (on)
        parts.push('saturate(' + config.HDR_SATURATION + ') brightness(' + config.HDR_BRIGHTNESS + ') contrast(' + config.HDR_CONTRAST + ')');
    var a = Math.round(hueAngle * 10) / 10;
    if (a % 360 !== 0)
        parts.push('hue-rotate(' + a + 'deg)');
    var desired = parts.length ? parts.join(' ') : 'none';
    if (canvas.style.filter !== desired)
        canvas.style.filter = desired;
}

// --- hue-shift cycler: off for HS_OFF_TIME, then a burst of HS_BURST_STEPS ---
var hsPhase = 'off';   // 'off' | 'glide' | 'linger' | 'return'
var hsTimer = 0;
var hsStep = 0;
var hsFrom = 0;
var hsTo = 0;

function updateHueShift (dt) {
    if (!config.HS_ENABLED) {
        if (hueAngle !== 0) { hueAngle = 0; applyHDRFilter(); }
        hsPhase = 'off'; hsTimer = 0; hsStep = 0;
        return;
    }
    hsTimer += dt;
    if (hsPhase === 'off') {
        if (hsTimer >= config.HS_OFF_TIME) {
            hsPhase = 'glide'; hsTimer = 0; hsStep = 1;
            hsFrom = 0; hsTo = config.HS_STEP;
        }
    } else if (hsPhase === 'linger') {
        if (hsTimer >= config.HS_LINGER) {
            hsTimer = 0;
            if (hsStep >= config.HS_BURST_STEPS) {
                // rotate forward to the next full turn so the palette lands home
                hsPhase = 'return'; hsFrom = hueAngle;
                hsTo = Math.ceil((hueAngle + 0.001) / 360) * 360;
            } else {
                hsStep++; hsPhase = 'glide';
                hsFrom = hueAngle; hsTo = hsStep * config.HS_STEP;
            }
        }
    } else { // glide or return
        var t = Math.min(1, hsTimer / Math.max(0.05, config.HS_GLIDE));
        t = t * t * (3 - 2 * t);
        hueAngle = hsFrom + (hsTo - hsFrom) * t;
        if (t >= 1) {
            hsTimer = 0;
            if (hsPhase === 'glide') hsPhase = 'linger';
            else { hsPhase = 'off'; hueAngle = 0; hsStep = 0; }
        }
        applyHDRFilter();
    }
}

var hdrPendingState = null;
var hdrPendingCount = 0;

function refreshHDRState () {
    // WE's Chromium can report the HDR media query unstably; debounce it so
    // the compensation filter can't flicker on/off with each bad reading.
    var raw = detectHDR();
    if (raw === hdrDisplayActive) {
        hdrPendingState = null;
        hdrPendingCount = 0;
    } else if (raw === hdrPendingState) {
        hdrPendingCount++;
        if (hdrPendingCount >= 2) {
            hdrDisplayActive = raw;
            hdrPendingState = null;
            hdrPendingCount = 0;
        }
    } else {
        hdrPendingState = raw;
        hdrPendingCount = 1;
    }
    // re-assert (no-ops unless something was actually dropped)
    applyWideGamut();
    applyHDRFilter();
}

function applyWideGamut () {
    try {
        if ('drawingBufferColorSpace' in gl) {
            var want = config.WIDE_GAMUT ? 'display-p3' : 'srgb';
            if (gl.drawingBufferColorSpace !== want)
                gl.drawingBufferColorSpace = want;
        }
    } catch (e) { /* unsupported: stays sRGB */ }
}

// react to live HDR toggles: event listener where supported + polling fallback
try {
    var hdrMq = window.matchMedia && window.matchMedia('(dynamic-range: high)');
    if (hdrMq && hdrMq.addEventListener)
        hdrMq.addEventListener('change', refreshHDRState);
} catch (e) {}
setInterval(refreshHDRState, 2000);
document.addEventListener('visibilitychange', function () {
    if (!document.hidden) refreshHDRState();
});

hdrDisplayActive = detectHDR();
applyWideGamut();
applyHDRFilter();
// ---------------------------------------------------------------------------

if (isMobile())
    config.SHADING = false;
if (!ext.supportLinearFiltering)
{
    config.SHADING = false;
    config.BLOOM = false;
}

function getWebGLContext (canvas) {
    const params = { alpha: true, depth: false, stencil: false, antialias: false, preserveDrawingBuffer: false };

    let gl = canvas.getContext('webgl2', params);
    const isWebGL2 = !!gl;
    if (!isWebGL2)
        gl = canvas.getContext('webgl', params) || canvas.getContext('experimental-webgl', params);

    let halfFloat;
    let supportLinearFiltering;
    if (isWebGL2) {
        gl.getExtension('EXT_color_buffer_float');
        supportLinearFiltering = gl.getExtension('OES_texture_float_linear');
    } else {
        halfFloat = gl.getExtension('OES_texture_half_float');
        supportLinearFiltering = gl.getExtension('OES_texture_half_float_linear');
    }

    gl.clearColor(0.0, 0.0, 0.0, 1.0);

    const halfFloatTexType = isWebGL2 ? gl.HALF_FLOAT : halfFloat.HALF_FLOAT_OES;
    let formatRGBA;
    let formatRG;
    let formatR;

    if (isWebGL2)
    {
        formatRGBA = getSupportedFormat(gl, gl.RGBA16F, gl.RGBA, halfFloatTexType);
        formatRG = getSupportedFormat(gl, gl.RG16F, gl.RG, halfFloatTexType);
        formatR = getSupportedFormat(gl, gl.R16F, gl.RED, halfFloatTexType);
    }
    else
    {
        formatRGBA = getSupportedFormat(gl, gl.RGBA, gl.RGBA, halfFloatTexType);
        formatRG = getSupportedFormat(gl, gl.RGBA, gl.RGBA, halfFloatTexType);
        formatR = getSupportedFormat(gl, gl.RGBA, gl.RGBA, halfFloatTexType);
    }

    return {
        gl,
        ext: {
            formatRGBA,
            formatRG,
            formatR,
            halfFloatTexType,
            supportLinearFiltering
        }
    };
}

function getSupportedFormat (gl, internalFormat, format, type)
{
    if (!supportRenderTextureFormat(gl, internalFormat, format, type))
    {
        switch (internalFormat)
        {
            case gl.R16F:
                return getSupportedFormat(gl, gl.RG16F, gl.RG, type);
            case gl.RG16F:
                return getSupportedFormat(gl, gl.RGBA16F, gl.RGBA, type);
            default:
                return null;
        }
    }

    return {
        internalFormat,
        format
    }
}

function supportRenderTextureFormat (gl, internalFormat, format, type) {
    let texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, internalFormat, 4, 4, 0, format, type, null);

    let fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);

    const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
    if (status != gl.FRAMEBUFFER_COMPLETE)
        return false;
    return true;
}

function isMobile () {
    return /Mobi|Android/i.test(navigator.userAgent);
}

class GLProgram {
    constructor (vertexShader, fragmentShader) {
        this.uniforms = {};
        this.program = gl.createProgram();

        gl.attachShader(this.program, vertexShader);
        gl.attachShader(this.program, fragmentShader);
        gl.linkProgram(this.program);

        if (!gl.getProgramParameter(this.program, gl.LINK_STATUS))
            throw gl.getProgramInfoLog(this.program);

        const uniformCount = gl.getProgramParameter(this.program, gl.ACTIVE_UNIFORMS);
        for (let i = 0; i < uniformCount; i++) {
            const uniformName = gl.getActiveUniform(this.program, i).name;
            this.uniforms[uniformName] = gl.getUniformLocation(this.program, uniformName);
        }
    }

    bind () {
        gl.useProgram(this.program);
    }
}

function compileShader (type, source) {
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);

    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
        throw gl.getShaderInfoLog(shader);

    return shader;
};

const baseVertexShader = compileShader(gl.VERTEX_SHADER, `
    precision highp float;

    attribute vec2 aPosition;
    varying vec2 vUv;
    varying vec2 vL;
    varying vec2 vR;
    varying vec2 vT;
    varying vec2 vB;
    uniform vec2 texelSize;

    void main () {
        vUv = aPosition * 0.5 + 0.5;
        vL = vUv - vec2(texelSize.x, 0.0);
        vR = vUv + vec2(texelSize.x, 0.0);
        vT = vUv + vec2(0.0, texelSize.y);
        vB = vUv - vec2(0.0, texelSize.y);
        gl_Position = vec4(aPosition, 0.0, 1.0);
    }
`);

const clearShader = compileShader(gl.FRAGMENT_SHADER, `
    precision mediump float;
    precision mediump sampler2D;

    varying highp vec2 vUv;
    uniform sampler2D uTexture;
    uniform float value;

    void main () {
        gl_FragColor = value * texture2D(uTexture, vUv);
    }
`);

const colorShader = compileShader(gl.FRAGMENT_SHADER, `
    precision mediump float;

    uniform vec4 color;

    void main () {
        gl_FragColor = color;
    }
`);

const backgroundShader = compileShader(gl.FRAGMENT_SHADER, `
    void main () {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
    }
`);

const displayShader = compileShader(gl.FRAGMENT_SHADER, `
    precision highp float;
    precision highp sampler2D;

    varying vec2 vUv;
    uniform sampler2D uTexture;

    void main () {
        vec3 C = texture2D(uTexture, vUv).rgb;
        float a = max(C.r, max(C.g, C.b));
        gl_FragColor = vec4(C, a);
    }
`);

const displayBloomShader = compileShader(gl.FRAGMENT_SHADER, `
    precision highp float;
    precision highp sampler2D;

    varying vec2 vUv;
    uniform sampler2D uTexture;
    uniform sampler2D uBloom;
    uniform sampler2D uDithering;
    uniform vec2 ditherScale;

    void main () {
        vec3 C = texture2D(uTexture, vUv).rgb;
        vec3 bloom = texture2D(uBloom, vUv).rgb;
        bloom = pow(bloom.rgb, vec3(1.0 / 2.2));
        C += bloom;
        float a = max(C.r, max(C.g, C.b));
        gl_FragColor = vec4(C, a);
    }
`);

const displayShadingShader = compileShader(gl.FRAGMENT_SHADER, `
    precision highp float;
    precision highp sampler2D;

    varying vec2 vUv;
    varying vec2 vL;
    varying vec2 vR;
    varying vec2 vT;
    varying vec2 vB;
    uniform sampler2D uTexture;
    uniform vec2 texelSize;

    void main () {
        vec3 L = texture2D(uTexture, vL).rgb;
        vec3 R = texture2D(uTexture, vR).rgb;
        vec3 T = texture2D(uTexture, vT).rgb;
        vec3 B = texture2D(uTexture, vB).rgb;
        vec3 C = texture2D(uTexture, vUv).rgb;

        float dx = length(R) - length(L);
        float dy = length(T) - length(B);

        vec3 n = normalize(vec3(dx, dy, length(texelSize)));
        vec3 l = vec3(0.0, 0.0, 1.0);

        float diffuse = clamp(dot(n, l) + 0.7, 0.7, 1.0);
        C.rgb *= diffuse;

        float a = max(C.r, max(C.g, C.b));
        gl_FragColor = vec4(C, a);
    }
`);

const displayBloomShadingShader = compileShader(gl.FRAGMENT_SHADER, `
    precision highp float;
    precision highp sampler2D;

    varying vec2 vUv;
    varying vec2 vL;
    varying vec2 vR;
    varying vec2 vT;
    varying vec2 vB;
    uniform sampler2D uTexture;
    uniform sampler2D uBloom;
    uniform sampler2D uDithering;
    uniform vec2 ditherScale;
    uniform vec2 texelSize;

    void main () {
        vec3 L = texture2D(uTexture, vL).rgb;
        vec3 R = texture2D(uTexture, vR).rgb;
        vec3 T = texture2D(uTexture, vT).rgb;
        vec3 B = texture2D(uTexture, vB).rgb;
        vec3 C = texture2D(uTexture, vUv).rgb;

        float dx = length(R) - length(L);
        float dy = length(T) - length(B);

        vec3 n = normalize(vec3(dx, dy, length(texelSize)));
        vec3 l = vec3(0.0, 0.0, 1.0);

        float diffuse = clamp(dot(n, l) + 0.7, 0.7, 1.0);
        C *= diffuse;

        vec3 bloom = texture2D(uBloom, vUv).rgb;
        bloom = pow(bloom.rgb, vec3(1.0 / 2.2));
        C += bloom;

        float a = max(C.r, max(C.g, C.b));
        gl_FragColor = vec4(C, a);
    }
`);

const bloomPrefilterShader = compileShader(gl.FRAGMENT_SHADER, `
    precision mediump float;
    precision mediump sampler2D;

    varying vec2 vUv;
    uniform sampler2D uTexture;
    uniform vec3 curve;
    uniform float threshold;

    void main () {
        vec3 c = texture2D(uTexture, vUv).rgb;
        float br = max(c.r, max(c.g, c.b));
        float rq = clamp(br - curve.x, 0.0, curve.y);
        rq = curve.z * rq * rq;
        c *= max(rq, br - threshold) / max(br, 0.0001);
        gl_FragColor = vec4(c, 0.0);
    }
`);

const bloomBlurShader = compileShader(gl.FRAGMENT_SHADER, `
    precision mediump float;
    precision mediump sampler2D;

    varying vec2 vL;
    varying vec2 vR;
    varying vec2 vT;
    varying vec2 vB;
    uniform sampler2D uTexture;

    void main () {
        vec4 sum = vec4(0.0);
        sum += texture2D(uTexture, vL);
        sum += texture2D(uTexture, vR);
        sum += texture2D(uTexture, vT);
        sum += texture2D(uTexture, vB);
        sum *= 0.25;
        gl_FragColor = sum;
    }
`);

const bloomFinalShader = compileShader(gl.FRAGMENT_SHADER, `
    precision mediump float;
    precision mediump sampler2D;

    varying vec2 vL;
    varying vec2 vR;
    varying vec2 vT;
    varying vec2 vB;
    uniform sampler2D uTexture;
    uniform float intensity;

    void main () {
        vec4 sum = vec4(0.0);
        sum += texture2D(uTexture, vL);
        sum += texture2D(uTexture, vR);
        sum += texture2D(uTexture, vT);
        sum += texture2D(uTexture, vB);
        sum *= 0.25;
        gl_FragColor = sum * intensity;
    }
`);

const splatShader = compileShader(gl.FRAGMENT_SHADER, `
    precision highp float;
    precision highp sampler2D;

    varying vec2 vUv;
    uniform sampler2D uTarget;
    uniform float aspectRatio;
    uniform vec3 color;
    uniform vec2 point;
    uniform float radius;
    uniform float uCap;

    void main () {
        vec2 p = vUv - point.xy;
        p.x *= aspectRatio;
        vec3 splat = exp(-dot(p, p) / radius) * color;
        vec3 base = texture2D(uTarget, vUv).xyz;
        vec3 c = base + splat;
        float m = max(c.x, max(c.y, c.z));
        c *= uCap / max(m, uCap); // proportional cap: brightness limited, hue kept
        gl_FragColor = vec4(c, 1.0);
    }
`);

const advectionManualFilteringShader = compileShader(gl.FRAGMENT_SHADER, `
    precision highp float;
    precision highp sampler2D;

    varying vec2 vUv;
    uniform sampler2D uVelocity;
    uniform sampler2D uSource;
    uniform vec2 texelSize;
    uniform vec2 dyeTexelSize;
    uniform float dt;
    uniform float dissipation;
    uniform float dissipationFast;
    uniform float decayThreshold;
    uniform float satRestore;

    vec4 bilerp (sampler2D sam, vec2 uv, vec2 tsize) {
        vec2 st = uv / tsize - 0.5;

        vec2 iuv = floor(st);
        vec2 fuv = fract(st);

        vec4 a = texture2D(sam, (iuv + vec2(0.5, 0.5)) * tsize);
        vec4 b = texture2D(sam, (iuv + vec2(1.5, 0.5)) * tsize);
        vec4 c = texture2D(sam, (iuv + vec2(0.5, 1.5)) * tsize);
        vec4 d = texture2D(sam, (iuv + vec2(1.5, 1.5)) * tsize);

        return mix(mix(a, b, fuv.x), mix(c, d, fuv.x), fuv.y);
    }

    void main () {
        vec2 coord = vUv - dt * bilerp(uVelocity, vUv, texelSize).xy * texelSize;
        vec4 c = bilerp(uSource, coord, dyeTexelSize);
        float lum = max(c.r, max(c.g, c.b));
        float k = mix(dissipationFast, dissipation, smoothstep(0.0, max(decayThreshold, 0.0001), lum));
        c *= k;
        float mx = max(c.r, max(c.g, c.b));
        float mn = min(c.r, min(c.g, c.b));
        if (satRestore > 0.0 && mx > 0.001) {
            vec3 satC = (c.rgb - mn) * (mx / max(mx - mn, 0.0001));
            c.rgb = mix(c.rgb, satC, satRestore * smoothstep(0.0, 0.05, mx));
        }
        gl_FragColor = c;
        gl_FragColor.a = 1.0;
    }
`);

const advectionShader = compileShader(gl.FRAGMENT_SHADER, `
    precision highp float;
    precision highp sampler2D;

    varying vec2 vUv;
    uniform sampler2D uVelocity;
    uniform sampler2D uSource;
    uniform vec2 texelSize;
    uniform float dt;
    uniform float dissipation;
    uniform float dissipationFast;
    uniform float decayThreshold;
    uniform float satRestore;

    void main () {
        vec2 coord = vUv - dt * texture2D(uVelocity, vUv).xy * texelSize;
        vec4 c = texture2D(uSource, coord);
        float lum = max(c.r, max(c.g, c.b));
        float k = mix(dissipationFast, dissipation, smoothstep(0.0, max(decayThreshold, 0.0001), lum));
        c *= k;
        float mx = max(c.r, max(c.g, c.b));
        float mn = min(c.r, min(c.g, c.b));
        if (satRestore > 0.0 && mx > 0.001) {
            vec3 satC = (c.rgb - mn) * (mx / max(mx - mn, 0.0001));
            c.rgb = mix(c.rgb, satC, satRestore * smoothstep(0.0, 0.05, mx));
        }
        gl_FragColor = c;
        gl_FragColor.a = 1.0;
    }
`);

const divergenceShader = compileShader(gl.FRAGMENT_SHADER, `
    precision mediump float;
    precision mediump sampler2D;

    varying highp vec2 vUv;
    varying highp vec2 vL;
    varying highp vec2 vR;
    varying highp vec2 vT;
    varying highp vec2 vB;
    uniform sampler2D uVelocity;

    void main () {
        float L = texture2D(uVelocity, vL).x;
        float R = texture2D(uVelocity, vR).x;
        float T = texture2D(uVelocity, vT).y;
        float B = texture2D(uVelocity, vB).y;

        vec2 C = texture2D(uVelocity, vUv).xy;
        if (vL.x < 0.0) { L = -C.x; }
        if (vR.x > 1.0) { R = -C.x; }
        if (vT.y > 1.0) { T = -C.y; }
        if (vB.y < 0.0) { B = -C.y; }

        float div = 0.5 * (R - L + T - B);
        gl_FragColor = vec4(div, 0.0, 0.0, 1.0);
    }
`);

const curlShader = compileShader(gl.FRAGMENT_SHADER, `
    precision mediump float;
    precision mediump sampler2D;

    varying highp vec2 vUv;
    varying highp vec2 vL;
    varying highp vec2 vR;
    varying highp vec2 vT;
    varying highp vec2 vB;
    uniform sampler2D uVelocity;

    void main () {
        float L = texture2D(uVelocity, vL).y;
        float R = texture2D(uVelocity, vR).y;
        float T = texture2D(uVelocity, vT).x;
        float B = texture2D(uVelocity, vB).x;
        float vorticity = R - L - T + B;
        gl_FragColor = vec4(0.5 * vorticity, 0.0, 0.0, 1.0);
    }
`);

const vorticityShader = compileShader(gl.FRAGMENT_SHADER, `
    precision highp float;
    precision highp sampler2D;

    varying vec2 vUv;
    varying vec2 vL;
    varying vec2 vR;
    varying vec2 vT;
    varying vec2 vB;
    uniform sampler2D uVelocity;
    uniform sampler2D uCurl;
    uniform float curl;
    uniform float dt;

    void main () {
        float L = texture2D(uCurl, vL).x;
        float R = texture2D(uCurl, vR).x;
        float T = texture2D(uCurl, vT).x;
        float B = texture2D(uCurl, vB).x;
        float C = texture2D(uCurl, vUv).x;

        vec2 force = 0.5 * vec2(abs(T) - abs(B), abs(R) - abs(L));
        force /= length(force) + 0.0001;
        force *= curl * C;
        force.y *= -1.0;

        vec2 vel = texture2D(uVelocity, vUv).xy;
        gl_FragColor = vec4(vel + force * dt, 0.0, 1.0);
    }
`);

const pressureShader = compileShader(gl.FRAGMENT_SHADER, `
    precision mediump float;
    precision mediump sampler2D;

    varying highp vec2 vUv;
    varying highp vec2 vL;
    varying highp vec2 vR;
    varying highp vec2 vT;
    varying highp vec2 vB;
    uniform sampler2D uPressure;
    uniform sampler2D uDivergence;

    vec2 boundary (vec2 uv) {
        return uv;
        // uncomment if you use wrap or repeat texture mode
        // uv = min(max(uv, 0.0), 1.0);
        // return uv;
    }

    void main () {
        float L = texture2D(uPressure, boundary(vL)).x;
        float R = texture2D(uPressure, boundary(vR)).x;
        float T = texture2D(uPressure, boundary(vT)).x;
        float B = texture2D(uPressure, boundary(vB)).x;
        float C = texture2D(uPressure, vUv).x;
        float divergence = texture2D(uDivergence, vUv).x;
        float pressure = (L + R + B + T - divergence) * 0.25;
        gl_FragColor = vec4(pressure, 0.0, 0.0, 1.0);
    }
`);

const gradientSubtractShader = compileShader(gl.FRAGMENT_SHADER, `
    precision mediump float;
    precision mediump sampler2D;

    varying highp vec2 vUv;
    varying highp vec2 vL;
    varying highp vec2 vR;
    varying highp vec2 vT;
    varying highp vec2 vB;
    uniform sampler2D uPressure;
    uniform sampler2D uVelocity;

    vec2 boundary (vec2 uv) {
        return uv;
        // uv = min(max(uv, 0.0), 1.0);
        // return uv;
    }

    void main () {
        float L = texture2D(uPressure, boundary(vL)).x;
        float R = texture2D(uPressure, boundary(vR)).x;
        float T = texture2D(uPressure, boundary(vT)).x;
        float B = texture2D(uPressure, boundary(vB)).x;
        vec2 velocity = texture2D(uVelocity, vUv).xy;
        velocity.xy -= vec2(R - L, T - B);
        gl_FragColor = vec4(velocity, 0.0, 1.0);
    }
`);

const blit = (() => {
    gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer());
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, -1, 1, 1, 1, 1, -1]), gl.STATIC_DRAW);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, gl.createBuffer());
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array([0, 1, 2, 0, 2, 3]), gl.STATIC_DRAW);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    gl.enableVertexAttribArray(0);

    return (destination) => {
        gl.bindFramebuffer(gl.FRAMEBUFFER, destination);
        gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);
    }
})();

let simWidth;
let simHeight;
let dyeWidth;
let dyeHeight;
let density;
let velocity;
let divergence;
let curl;
let pressure;
let bloom;

let ditheringTexture = createTextureAsync('LDR_RGB1_0.png');

const clearProgram               = new GLProgram(baseVertexShader, clearShader);
const colorProgram               = new GLProgram(baseVertexShader, colorShader);
const backgroundProgram          = new GLProgram(baseVertexShader, backgroundShader);
const displayProgram             = new GLProgram(baseVertexShader, displayShader);
const displayBloomProgram        = new GLProgram(baseVertexShader, displayBloomShader);
const displayShadingProgram      = new GLProgram(baseVertexShader, displayShadingShader);
const displayBloomShadingProgram = new GLProgram(baseVertexShader, displayBloomShadingShader);
const bloomPrefilterProgram      = new GLProgram(baseVertexShader, bloomPrefilterShader);
const bloomBlurProgram           = new GLProgram(baseVertexShader, bloomBlurShader);
const bloomFinalProgram          = new GLProgram(baseVertexShader, bloomFinalShader);
const splatProgram               = new GLProgram(baseVertexShader, splatShader);
const advectionProgram           = new GLProgram(baseVertexShader, ext.supportLinearFiltering ? advectionShader : advectionManualFilteringShader);
const divergenceProgram          = new GLProgram(baseVertexShader, divergenceShader);
const curlProgram                = new GLProgram(baseVertexShader, curlShader);
const vorticityProgram           = new GLProgram(baseVertexShader, vorticityShader);
const pressureProgram            = new GLProgram(baseVertexShader, pressureShader);
const gradienSubtractProgram     = new GLProgram(baseVertexShader, gradientSubtractShader);

function initFramebuffers () {
    let simRes = getResolution(config.SIM_RESOLUTION);
    let dyeRes = getResolution(config.DYE_RESOLUTION);

    simWidth  = simRes.width;
    simHeight = simRes.height;
    dyeWidth  = dyeRes.width;
    dyeHeight = dyeRes.height;

    const texType = ext.halfFloatTexType;
    const rgba    = ext.formatRGBA;
    const rg      = ext.formatRG;
    const r       = ext.formatR;
    const filtering = ext.supportLinearFiltering ? gl.LINEAR : gl.NEAREST;

    if (density == null)
        density = createDoubleFBO(dyeWidth, dyeHeight, rgba.internalFormat, rgba.format, texType, filtering);
    else
        density = resizeDoubleFBO(density, dyeWidth, dyeHeight, rgba.internalFormat, rgba.format, texType, filtering);

    if (velocity == null)
        velocity = createDoubleFBO(simWidth, simHeight, rg.internalFormat, rg.format, texType, filtering);
    else
        velocity = resizeDoubleFBO(velocity, simWidth, simHeight, rg.internalFormat, rg.format, texType, filtering);

    divergence = createFBO      (simWidth, simHeight, r.internalFormat, r.format, texType, gl.NEAREST);
    curl       = createFBO      (simWidth, simHeight, r.internalFormat, r.format, texType, gl.NEAREST);
    pressure   = createDoubleFBO(simWidth, simHeight, r.internalFormat, r.format, texType, gl.NEAREST);

    initBloomFramebuffers();
}

function initBloomFramebuffers () {
    let res = getResolution(config.BLOOM_RESOLUTION);

    const texType = ext.halfFloatTexType;
    const rgba = ext.formatRGBA;
    const filtering = ext.supportLinearFiltering ? gl.LINEAR : gl.NEAREST;

    bloom = createFBO(res.width, res.height, rgba.internalFormat, rgba.format, texType, filtering);

    bloomFramebuffers.length = 0;
    for (let i = 0; i < config.BLOOM_ITERATIONS; i++)
    {
        let width = res.width >> (i + 1);
        let height = res.height >> (i + 1);

        if (width < 2 || height < 2) break;

        let fbo = createFBO(width, height, rgba.internalFormat, rgba.format, texType, filtering);
        bloomFramebuffers.push(fbo);
    }
}

function createFBO (w, h, internalFormat, format, type, param) {
    gl.activeTexture(gl.TEXTURE0);
    let texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, param);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, param);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, null);

    let fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
    gl.viewport(0, 0, w, h);
    gl.clear(gl.COLOR_BUFFER_BIT);

    return {
        texture,
        fbo,
        width: w,
        height: h,
        attach (id) {
            gl.activeTexture(gl.TEXTURE0 + id);
            gl.bindTexture(gl.TEXTURE_2D, texture);
            return id;
        }
    };
}

function createDoubleFBO (w, h, internalFormat, format, type, param) {
    let fbo1 = createFBO(w, h, internalFormat, format, type, param);
    let fbo2 = createFBO(w, h, internalFormat, format, type, param);

    return {
        get read () {
            return fbo1;
        },
        set read (value) {
            fbo1 = value;
        },
        get write () {
            return fbo2;
        },
        set write (value) {
            fbo2 = value;
        },
        swap () {
            let temp = fbo1;
            fbo1 = fbo2;
            fbo2 = temp;
        }
    }
}

function resizeFBO (target, w, h, internalFormat, format, type, param) {
    let newFBO = createFBO(w, h, internalFormat, format, type, param);
    clearProgram.bind();
    gl.uniform1i(clearProgram.uniforms.uTexture, target.attach(0));
    gl.uniform1f(clearProgram.uniforms.value, 1);
    blit(newFBO.fbo);
    return newFBO;
}

function resizeDoubleFBO (target, w, h, internalFormat, format, type, param) {
    target.read = resizeFBO(target.read, w, h, internalFormat, format, type, param);
    target.write = createFBO(w, h, internalFormat, format, type, param);
    return target;
}

function createTextureAsync (url) {
    let texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGB, 1, 1, 0, gl.RGB, gl.UNSIGNED_BYTE, new Uint8Array([255, 255, 255]));

    let obj = {
        texture,
        width: 1,
        height: 1,
        attach (id) {
            gl.activeTexture(gl.TEXTURE0 + id);
            gl.bindTexture(gl.TEXTURE_2D, texture);
            return id;
        }
    };

    let image = new Image();
    image.onload = () => {
        obj.width = image.width;
        obj.height = image.height;
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGB, gl.RGB, gl.UNSIGNED_BYTE, image);
    };
    image.src = url;

    return obj;
}

initFramebuffers();
multipleSplats(parseInt(Math.random() * 20) + 3);

let lastColorChangeTime = Date.now();

function cycledColor (offset) {
    let c = HSVtoRGB((globalHue + (offset || 0)) % 1.0, 1.0, 1.0);
    c.r *= 0.15;
    c.g *= 0.15;
    c.b *= 0.15;
    return c;
}

// --- autonomous wanderer splats ---
let wanderers = [];
let lastInteraction = 0;

function userInteracted () {
    lastInteraction = Date.now();
}

function wanderersActive () {
    return config.WANDERERS && (Date.now() - lastInteraction > config.WANDERER_RESUME_DELAY * 1000);
}

function initWanderers () {
    wanderers = [];
    const n = Math.max(1, Math.round(config.WANDERER_COUNT));
    for (let i = 0; i < n; i++) {
        const R = Math.min(canvas.width, canvas.height) * config.WANDERER_SCALE * (0.6 + Math.random() * 0.8) / 2;
        wanderers.push({
            x: canvas.width * (0.2 + Math.random() * 0.6),
            y: canvas.height * (0.2 + Math.random() * 0.6),
            heading: Math.random() * Math.PI * 2,
            turn: 0,
            cx: canvas.width * (0.25 + Math.random() * 0.5),
            cy: canvas.height * (0.3 + Math.random() * 0.4),
            R: R,
            phase: Math.random() * Math.PI * 2,
            dir: Math.random() < 0.5 ? 1 : -1,
            hueOffset: i * 0.13
        });
    }
}

function updateWanderers (dt) {
    const margin = 40;
    for (let i = 0; i < wanderers.length; i++) {
        // wanderer 0 is the survivor: it obeys its own floor, the rest obey the group floor
        if (i === 0 ? survivorTooFull : screenTooFull) continue;
        const w = wanderers[i];
        let nx, ny;
        if (config.WANDERER_MODE === 'circle') {
            w.phase += w.dir * (config.WANDERER_SPEED * dt) / w.R;
            nx = w.cx + Math.cos(w.phase) * w.R;
            ny = w.cy + Math.sin(w.phase) * w.R;
        } else if (config.WANDERER_MODE === 'figure8') {
            w.phase += w.dir * (config.WANDERER_SPEED * dt) / w.R;
            nx = w.cx + Math.sin(w.phase) * w.R * 1.6;
            ny = w.cy + Math.sin(w.phase * 2.0) * w.R * 0.8;
        } else { // random smooth wander
            w.turn += (Math.random() - 0.5) * 12.0 * dt;
            w.turn = Math.max(-2.5, Math.min(2.5, w.turn)) * 0.98;
            w.heading += w.turn * dt * 4.0;
            nx = w.x + Math.cos(w.heading) * config.WANDERER_SPEED * dt;
            ny = w.y + Math.sin(w.heading) * config.WANDERER_SPEED * dt;
            if (nx < margin || nx > canvas.width - margin) {
                w.heading = Math.PI - w.heading;
                nx = Math.max(margin, Math.min(canvas.width - margin, nx));
            }
            if (ny < margin || ny > canvas.height - margin) {
                w.heading = -w.heading;
                ny = Math.max(margin, Math.min(canvas.height - margin, ny));
            }
        }
        const dx = (nx - w.x) * 5.0;
        const dy = (ny - w.y) * 5.0;
        w.x = nx;
        w.y = ny;
        const color = config.COLORFUL ? cycledColor(w.hueOffset) : config.POINTER_COLOR.getRandom();
        const wb = config.WANDERER_BRIGHTNESS;
        splat(w.x, w.y, dx, dy, { r: color.r * wb, g: color.g * wb, b: color.b * wb });
    }
}

// --- screen-fullness governor -------------------------------------------
// Once per second, downsample the dye field to 48x27, count near-black pixels,
// and pause the wanderers while free dark space is below DARK_FLOOR percent.
const COVERAGE_W = 48, COVERAGE_H = 27;
let coverageFBO = null;
let coveragePixels = new Uint8Array(COVERAGE_W * COVERAGE_H * 4);
let screenTooFull = false;
let survivorTooFull = false;
let lastCoverageCheck = 0;
let dart = null;
let lastDartTime = 0;

function measureCoverage () {
    if (coverageFBO == null)
        coverageFBO = createFBO(COVERAGE_W, COVERAGE_H, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, gl.NEAREST);
    clearProgram.bind();
    gl.uniform1f(clearProgram.uniforms.value, 1.0);
    gl.uniform1i(clearProgram.uniforms.uTexture, density.read.attach(0));
    gl.viewport(0, 0, COVERAGE_W, COVERAGE_H);
    blit(coverageFBO.fbo);
    gl.readPixels(0, 0, COVERAGE_W, COVERAGE_H, gl.RGBA, gl.UNSIGNED_BYTE, coveragePixels);
    var cut = Math.round(255 * config.DARK_LEVEL);
    var dark = 0, total = COVERAGE_W * COVERAGE_H;
    for (var i = 0; i < total; i++) {
        var o = i * 4;
        var m = Math.max(coveragePixels[o], Math.max(coveragePixels[o + 1], coveragePixels[o + 2]));
        if (m <= cut) dark++;
    }
    var darkPct = 100.0 * dark / total;

    // contrast check: brightest tile must beat the average of the rest by CONTRAST_REQ %
    // 6x3 tiles of 8x9 coverage pixels
    var TX = 6, TY = 3, tw = COVERAGE_W / TX, th = COVERAGE_H / TY;
    var tiles = new Array(TX * TY).fill(0);
    for (var p = 0; p < total; p++) {
        var px = p % COVERAGE_W, py = (p / COVERAGE_W) | 0;
        var ti = Math.min(TX - 1, (px / tw) | 0) + TX * Math.min(TY - 1, (py / th) | 0);
        var q = p * 4;
        tiles[ti] += Math.max(coveragePixels[q], Math.max(coveragePixels[q + 1], coveragePixels[q + 2]));
    }
    var maxTile = 0, sumTiles = 0;
    for (var t = 0; t < tiles.length; t++) {
        tiles[t] /= (tw * th * 255);
        sumTiles += tiles[t];
        if (tiles[t] > maxTile) maxTile = tiles[t];
    }
    var avgRest = (sumTiles - maxTile) / (tiles.length - 1);
    var contrastOK = config.CONTRAST_REQ <= 0 || maxTile >= avgRest * (1.0 + config.CONTRAST_REQ / 100.0);

    // hysteresis: pause below the floor, resume only once 10 points above it
    if (!screenTooFull && darkPct < config.DARK_FLOOR) screenTooFull = true;
    else if (screenTooFull && darkPct > Math.min(config.DARK_FLOOR + 10, 95)) screenTooFull = false;
    if (!survivorTooFull && darkPct < config.SURV_DARK_FLOOR) survivorTooFull = true;
    else if (survivorTooFull && darkPct > Math.min(config.SURV_DARK_FLOOR + 10, 95)) survivorTooFull = false;

    // flat dim mush (no standout region): cancel pauses, keep painting until contrast returns
    if (!contrastOK) {
        screenTooFull = false;
        survivorTooFull = false;
    }
}

// --- separating dart: streaks across while the wanderer group is paused ----
function updateDart (dt) {
    if (dart == null) {
        if (!config.DART_ENABLED || !screenTooFull || !config.WANDERERS) return;
        var now = Date.now();
        if (now - lastDartTime < config.DART_INTERVAL * 1000) return;
        lastDartTime = now;
        // start on a random edge, aim at a random point on the opposite edge
        var m = 10, W = canvas.width, H = canvas.height;
        var side = (Math.random() * 4) | 0, sx, sy, exx, ey;
        if (side === 0)      { sx = m;     sy = Math.random() * H; exx = W - m; ey = Math.random() * H; }
        else if (side === 1) { sx = W - m; sy = Math.random() * H; exx = m;     ey = Math.random() * H; }
        else if (side === 2) { sx = Math.random() * W; sy = m;     exx = Math.random() * W; ey = H - m; }
        else                 { sx = Math.random() * W; sy = H - m; exx = Math.random() * W; ey = m;     }
        var ddx = exx - sx, ddy = ey - sy, len = Math.sqrt(ddx * ddx + ddy * ddy);
        dart = { x: sx, y: sy, ux: ddx / len, uy: ddy / len, left: len };
        return;
    }
    var step = Math.min(config.DART_SPEED * dt, dart.left);
    var nx = dart.x + dart.ux * step;
    var ny = dart.y + dart.uy * step;
    // complementary hue to the current wheel position, at wanderer brightness
    var c = config.COLORFUL ? cycledColor(0.5) : config.POINTER_COLOR.getRandom();
    var wb = config.WANDERER_BRIGHTNESS;
    splat(nx, ny, (nx - dart.x) * 5.0, (ny - dart.y) * 5.0, { r: c.r * wb, g: c.g * wb, b: c.b * wb });
    dart.x = nx;
    dart.y = ny;
    dart.left -= step;
    if (dart.left <= 0.5) dart = null;
}

function maybeMeasureCoverage () {
    if (!config.AUTO_PAUSE || !config.WANDERERS) { screenTooFull = false; return; }
    var now = Date.now();
    if (now - lastCoverageCheck < 1000) return;
    lastCoverageCheck = now;
    measureCoverage();
}
// ---------------------------------------------------------------------------

initWanderers();

update();

function update() {
    if (config.IGNORE_FPS_LIMIT) {
        resizeCanvas();
        globalHue = (globalHue + 0.016 / Math.max(1, config.COLOR_CYCLE_PERIOD)) % 1.0;
        maybeMeasureCoverage();
        if (wanderersActive() && !config.PAUSED) updateWanderers(0.016);
        if (!config.PAUSED) updateDart(0.016);
        if (!config.PAUSED) updateHueShift(0.016);
        input();
        if (!config.PAUSED)
            step(0.016);
        render(null);
        requestAnimationFrame(update);
    } else {
        setTimeout(update, config.FRAME_INTERVAL_MS);

        resizeCanvas();
        var frameDtS = config.FRAME_INTERVAL_MS / 1000.0;
        globalHue = (globalHue + frameDtS / Math.max(1, config.COLOR_CYCLE_PERIOD)) % 1.0;
        maybeMeasureCoverage();
        if (wanderersActive() && !config.PAUSED) updateWanderers(frameDtS);
        if (!config.PAUSED) updateDart(frameDtS);
        if (!config.PAUSED) updateHueShift(frameDtS);
        input();

        if (!config.PAUSED) {
            var remainingTimeS = config.FRAME_INTERVAL_MS / 1000.0;
            while (remainingTimeS > config.STEP_SIZE_S) {
                step(config.STEP_SIZE_S);
                remainingTimeS -= config.STEP_SIZE_S;
            }
            step(remainingTimeS);
        }

        render(null);
    }
}

function input () {
    if (splatStack.length > 0)
        multipleSplats(splatStack.pop());

    // hold M1 = continuous splat at cursor
    if (config.HOLD_TO_SPLAT && pointers[0].down) {
        const p = pointers[0];
        const jx = (Math.random() - 0.5) * 200;
        const jy = (Math.random() - 0.5) * 200;
        splat(p.x, p.y, p.dx * 0.5 + jx, p.dy * 0.5 + jy, p.color);
    }

    for (let i = 0; i < pointers.length; i++) {
        const p = pointers[i];
        if (p.moved) {
            splat(p.x, p.y, p.dx, p.dy, p.color);
            p.moved = false;
        }
    }

    if (config.COLORFUL) {
        // pointer follows the slow color wheel instead of random hue every 100ms
        for (let i = 0; i < pointers.length; i++)
            pointers[i].color = cycledColor(i * 0.05);
    } else if (lastColorChangeTime + 100 < Date.now()) {
        lastColorChangeTime = Date.now();
        for (let i = 0; i < pointers.length; i++) {
            const p = pointers[i];
            p.color = config.POINTER_COLOR.getRandom();
        }
    }
}

function step (dt) {
    gl.disable(gl.BLEND);
    gl.viewport(0, 0, simWidth, simHeight);

    curlProgram.bind();
    gl.uniform2f(curlProgram.uniforms.texelSize, 1.0 / simWidth, 1.0 / simHeight);
    gl.uniform1i(curlProgram.uniforms.uVelocity, velocity.read.attach(0));
    blit(curl.fbo);

    vorticityProgram.bind();
    gl.uniform2f(vorticityProgram.uniforms.texelSize, 1.0 / simWidth, 1.0 / simHeight);
    gl.uniform1i(vorticityProgram.uniforms.uVelocity, velocity.read.attach(0));
    gl.uniform1i(vorticityProgram.uniforms.uCurl, curl.attach(1));
    gl.uniform1f(vorticityProgram.uniforms.curl, config.CURL);
    gl.uniform1f(vorticityProgram.uniforms.dt, dt);
    blit(velocity.write.fbo);
    velocity.swap();

    divergenceProgram.bind();
    gl.uniform2f(divergenceProgram.uniforms.texelSize, 1.0 / simWidth, 1.0 / simHeight);
    gl.uniform1i(divergenceProgram.uniforms.uVelocity, velocity.read.attach(0));
    blit(divergence.fbo);

    clearProgram.bind();
    gl.uniform1i(clearProgram.uniforms.uTexture, pressure.read.attach(0));
    gl.uniform1f(clearProgram.uniforms.value, config.PRESSURE_DISSIPATION);
    blit(pressure.write.fbo);
    pressure.swap();

    pressureProgram.bind();
    gl.uniform2f(pressureProgram.uniforms.texelSize, 1.0 / simWidth, 1.0 / simHeight);
    gl.uniform1i(pressureProgram.uniforms.uDivergence, divergence.attach(0));
    for (let i = 0; i < config.PRESSURE_ITERATIONS; i++) {
        gl.uniform1i(pressureProgram.uniforms.uPressure, pressure.read.attach(1));
        blit(pressure.write.fbo);
        pressure.swap();
    }

    gradienSubtractProgram.bind();
    gl.uniform2f(gradienSubtractProgram.uniforms.texelSize, 1.0 / simWidth, 1.0 / simHeight);
    gl.uniform1i(gradienSubtractProgram.uniforms.uPressure, pressure.read.attach(0));
    gl.uniform1i(gradienSubtractProgram.uniforms.uVelocity, velocity.read.attach(1));
    blit(velocity.write.fbo);
    velocity.swap();

    advectionProgram.bind();
    gl.uniform2f(advectionProgram.uniforms.texelSize, 1.0 / simWidth, 1.0 / simHeight);
    if (!ext.supportLinearFiltering)
        gl.uniform2f(advectionProgram.uniforms.dyeTexelSize, 1.0 / simWidth, 1.0 / simHeight);
    let velocityId = velocity.read.attach(0);
    gl.uniform1i(advectionProgram.uniforms.uVelocity, velocityId);
    gl.uniform1i(advectionProgram.uniforms.uSource, velocityId);
    gl.uniform1f(advectionProgram.uniforms.dt, dt);
    gl.uniform1f(advectionProgram.uniforms.dissipation, config.VELOCITY_DISSIPATION);
    gl.uniform1f(advectionProgram.uniforms.dissipationFast, config.VELOCITY_DISSIPATION);
    gl.uniform1f(advectionProgram.uniforms.decayThreshold, 0.0001);
    gl.uniform1f(advectionProgram.uniforms.satRestore, 0.0); // never touch velocity
    blit(velocity.write.fbo);
    velocity.swap();

    gl.viewport(0, 0, dyeWidth, dyeHeight);

    if (!ext.supportLinearFiltering)
        gl.uniform2f(advectionProgram.uniforms.dyeTexelSize, 1.0 / dyeWidth, 1.0 / dyeHeight);
    gl.uniform1i(advectionProgram.uniforms.uVelocity, velocity.read.attach(0));
    gl.uniform1i(advectionProgram.uniforms.uSource, density.read.attach(1));
    gl.uniform1f(advectionProgram.uniforms.dissipation, config.DENSITY_DISSIPATION);
    gl.uniform1f(advectionProgram.uniforms.dissipationFast, config.DECAY_FAST);
    gl.uniform1f(advectionProgram.uniforms.decayThreshold, config.DECAY_THRESHOLD);
    gl.uniform1f(advectionProgram.uniforms.satRestore, 1.0 - Math.pow(1.0 - Math.min(config.SAT_RESTORE, 0.9999), dt));
    blit(density.write.fbo);
    density.swap();
}

function render (target) {
    if (config.BLOOM)
        applyBloom(density.read, bloom);

    if (target == null || !config.TRANSPARENT) {
        gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
        gl.enable(gl.BLEND);
    }
    else {
        gl.disable(gl.BLEND);
    }

    let width  = target == null ? gl.drawingBufferWidth : dyeWidth;
    let height = target == null ? gl.drawingBufferHeight : dyeHeight;

    gl.viewport(0, 0, width, height);

    if (!config.TRANSPARENT) {
        colorProgram.bind();
        let bc = config.BACK_COLOR;
        gl.uniform4f(colorProgram.uniforms.color, bc.r / 255, bc.g / 255, bc.b / 255, 1);
        blit(target);
    }

    if (target == null && config.TRANSPARENT) {
        backgroundProgram.bind();
        gl.uniform1f(backgroundProgram.uniforms.aspectRatio, canvas.width / canvas.height);
        blit(null);
    }

    if (config.SHADING) {
        let program = config.BLOOM ? displayBloomShadingProgram : displayShadingProgram;
        program.bind();
        gl.uniform2f(program.uniforms.texelSize, 1.0 / width, 1.0 / height);
        gl.uniform1i(program.uniforms.uTexture, density.read.attach(0));
        if (config.BLOOM) {
            gl.uniform1i(program.uniforms.uBloom, bloom.attach(1));
            gl.uniform1i(program.uniforms.uDithering, ditheringTexture.attach(2));
            let scale = getTextureScale(ditheringTexture, width, height);
            gl.uniform2f(program.uniforms.ditherScale, scale.x, scale.y);
        }
    }
    else {
        let program = config.BLOOM ? displayBloomProgram : displayProgram;
        program.bind();
        gl.uniform1i(program.uniforms.uTexture, density.read.attach(0));
        if (config.BLOOM) {
            gl.uniform1i(program.uniforms.uBloom, bloom.attach(1));
            gl.uniform1i(program.uniforms.uDithering, ditheringTexture.attach(2));
            let scale = getTextureScale(ditheringTexture, width, height);
            gl.uniform2f(program.uniforms.ditherScale, scale.x, scale.y);
        }
    }

    blit(target);
}

function applyBloom (source, destination) {
    if (bloomFramebuffers.length < 2)
        return;

    let last = destination;

    gl.disable(gl.BLEND);
    bloomPrefilterProgram.bind();
    let knee = config.BLOOM_THRESHOLD * config.BLOOM_SOFT_KNEE + 0.0001;
    let curve0 = config.BLOOM_THRESHOLD - knee;
    let curve1 = knee * 2;
    let curve2 = 0.25 / knee;
    gl.uniform3f(bloomPrefilterProgram.uniforms.curve, curve0, curve1, curve2);
    gl.uniform1f(bloomPrefilterProgram.uniforms.threshold, config.BLOOM_THRESHOLD);
    gl.uniform1i(bloomPrefilterProgram.uniforms.uTexture, source.attach(0));
    gl.viewport(0, 0, last.width, last.height);
    blit(last.fbo);

    bloomBlurProgram.bind();
    for (let i = 0; i < bloomFramebuffers.length; i++) {
        let dest = bloomFramebuffers[i];
        gl.uniform2f(bloomBlurProgram.uniforms.texelSize, 1.0 / last.width, 1.0 / last.height);
        gl.uniform1i(bloomBlurProgram.uniforms.uTexture, last.attach(0));
        gl.viewport(0, 0, dest.width, dest.height);
        blit(dest.fbo);
        last = dest;
    }

    gl.blendFunc(gl.ONE, gl.ONE);
    gl.enable(gl.BLEND);

    for (let i = bloomFramebuffers.length - 2; i >= 0; i--) {
        let baseTex = bloomFramebuffers[i];
        gl.uniform2f(bloomBlurProgram.uniforms.texelSize, 1.0 / last.width, 1.0 / last.height);
        gl.uniform1i(bloomBlurProgram.uniforms.uTexture, last.attach(0));
        gl.viewport(0, 0, baseTex.width, baseTex.height);
        blit(baseTex.fbo);
        last = baseTex;
    }

    gl.disable(gl.BLEND);
    bloomFinalProgram.bind();
    gl.uniform2f(bloomFinalProgram.uniforms.texelSize, 1.0 / last.width, 1.0 / last.height);
    gl.uniform1i(bloomFinalProgram.uniforms.uTexture, last.attach(0));
    gl.uniform1f(bloomFinalProgram.uniforms.intensity, config.BLOOM_INTENSITY);
    gl.viewport(0, 0, destination.width, destination.height);
    blit(destination.fbo);
}

function splat (x, y, dx, dy, color) {
    gl.viewport(0, 0, simWidth, simHeight);
    splatProgram.bind();
    gl.uniform1i(splatProgram.uniforms.uTarget, velocity.read.attach(0));
    gl.uniform1f(splatProgram.uniforms.aspectRatio, canvas.width / canvas.height);
    gl.uniform2f(splatProgram.uniforms.point, x / canvas.width, 1.0 - y / canvas.height);
    gl.uniform3f(splatProgram.uniforms.color, dx, -dy, 1.0);
    gl.uniform1f(splatProgram.uniforms.radius, config.SPLAT_RADIUS / 100.0);
    gl.uniform1f(splatProgram.uniforms.uCap, 1000000.0); // never cap velocity
    blit(velocity.write.fbo);
    velocity.swap();

    gl.viewport(0, 0, dyeWidth, dyeHeight);
    gl.uniform1i(splatProgram.uniforms.uTarget, density.read.attach(0));
    gl.uniform3f(splatProgram.uniforms.color, color.r, color.g, color.b);
    gl.uniform1f(splatProgram.uniforms.uCap, config.MAX_BRIGHTNESS);
    blit(density.write.fbo);
    density.swap();
}

function multipleSplats (amount) {
    for (let i = 0; i < amount; i++) {
        const color = config.COLORFUL ? generateColor() : Object.assign({}, config.POINTER_COLOR.getRandom());
        color.r *= 10.0;
        color.g *= 10.0;
        color.b *= 10.0;
        const x = canvas.width * Math.random();
        const y = canvas.height * Math.random();
        const dx = 1000 * (Math.random() - 0.5);
        const dy = 1000 * (Math.random() - 0.5);
        splat(x, y, dx, dy, color);
    }
}

function resizeCanvas () {
    if (canvas.width != canvas.clientWidth || canvas.height != canvas.clientHeight) {
        canvas.width = canvas.clientWidth;
        canvas.height = canvas.clientHeight;
        initFramebuffers();
        initWanderers();
    }
}

canvas.addEventListener('mousemove', e => {
    userInteracted();
    if (!config.SHOW_MOUSE_MOVEMENT) return;
    pointers[0].moved = true;
    pointers[0].dx = (e.offsetX - pointers[0].x) * 5.0;
    pointers[0].dy = (e.offsetY - pointers[0].y) * 5.0;
    pointers[0].x = e.offsetX;
    pointers[0].y = e.offsetY;
});

canvas.addEventListener('touchmove', e => {
    userInteracted();
    e.preventDefault();
    const touches = e.targetTouches;
    for (let i = 0; i < touches.length; i++) {
        let pointer = pointers[i];
        pointer.moved = pointer.down;
        pointer.dx = (touches[i].pageX - pointer.x) * 8.0;
        pointer.dy = (touches[i].pageY - pointer.y) * 8.0;
        pointer.x = touches[i].pageX;
        pointer.y = touches[i].pageY;
    }
}, false);

canvas.addEventListener('mouseenter', () => {
    pointers[0].color = config.COLORFUL ? cycledColor(0) : config.POINTER_COLOR.getRandom();
});

canvas.addEventListener('touchstart', e => {
    userInteracted();
    if (!config.SPLAT_ON_CLICK) return;
    e.preventDefault();
    const touches = e.targetTouches;
    for (let i = 0; i < touches.length; i++) {
        if (i >= pointers.length)
            pointers.push(new pointerPrototype());

        pointers[i].id = touches[i].identifier;
        pointers[i].down = true;
        pointers[i].x = touches[i].pageX;
        pointers[i].y = touches[i].pageY;
        pointers[i].color = config.POINTER_COLOR.getRandom();
    }
});

canvas.addEventListener("mousedown", () => {
    userInteracted();
    pointers[0].down = true;
    if (config.HOLD_TO_SPLAT) return; // continuous splat handled in input()
    if (!config.SPLAT_ON_CLICK) return;
    multipleSplats(parseInt(Math.random() * 20) + 5);
});

window.addEventListener("mouseup", () => {
    pointers[0].down = false;
});

window.addEventListener('mouseleave', () => {
    pointers[0].down = false;
});

window.addEventListener('touchend', e => {
    const touches = e.changedTouches;
    for (let i = 0; i < touches.length; i++)
        for (let j = 0; j < pointers.length; j++)
            if (touches[i].identifier == pointers[j].id)
                pointers[j].down = false;
});

window.addEventListener('keydown', e => {
    if (e.code === 'KeyP')
        config.PAUSED = !config.PAUSED;
    if (e.key === ' ')
        splatStack.push(parseInt(Math.random() * 20) + 5);
});

function generateColor () {
    // stay near the current wheel position so bursts don't mush into brown
    let h = config.COLORFUL ? (globalHue + (Math.random() - 0.5) * 0.12 + 1.0) % 1.0 : Math.random();
    let c = HSVtoRGB(h, 1.0, 1.0);
    c.r *= 0.15;
    c.g *= 0.15;
    c.b *= 0.15;
    return c;
}

function HSVtoRGB (h, s, v) {
    let r, g, b, i, f, p, q, t;
    i = Math.floor(h * 6);
    f = h * 6 - i;
    p = v * (1 - s);
    q = v * (1 - f * s);
    t = v * (1 - (1 - f) * s);

    switch (i % 6) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        case 5: r = v, g = p, b = q; break;
    }

    return {
        r,
        g,
        b
    };
}

function RGBToHue(r, g, b) {
  // Find greatest and smallest channel values
  let cmin = Math.min(r,g,b),
      cmax = Math.max(r,g,b),
      delta = cmax - cmin,
      h = 0,
      s = 0,
      l = 0;

  // Calculate hue
  // No difference
  if (delta == 0)
    h = 0;
  // Red is max
  else if (cmax == r)
    h = ((g - b) / delta) % 6;
  // Green is max
  else if (cmax == g)
    h = (b - r) / delta + 2;
  // Blue is max
  else
    h = (r - g) / delta + 4;

  h = Math.round(h * 60);
    
  // Make negative hues positive behind 360°
  if (h < 0)
      h += 360;

  return h;
}

function getResolution (resolution) {
    let aspectRatio = gl.drawingBufferWidth / gl.drawingBufferHeight;
    if (aspectRatio < 1)
        aspectRatio = 1.0 / aspectRatio;

    let max = Math.round(resolution * aspectRatio);
    let min = Math.round(resolution);

    if (gl.drawingBufferWidth > gl.drawingBufferHeight)
        return { width: max, height: min };
    else
        return { width: min, height: max };
}

function getTextureScale (texture, width, height) {
    return {
        x: width / texture.width,
        y: height / texture.height
    };
}

function rgbToPointerColor(color) {
    let c = color.split(" ");
    // let hue = RGBToHue(c[0], c[1], c[2]);
    // let c2 = HSVtoRGB(hue/360, 1.0, 1.0);
    // c2.r *= 0.15;
    // c2.g *= 0.15;
    // c2.b *= 0.15;
    // return c2;
    return {
        r: c[0] * 0.15,
        g: c[1] * 0.15,
        b: c[2] * 0.15
    }
}
