// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "render/gl.h"

namespace {

// Development aid: GD01_GPULOG logs the GPU time of the frame's parts
// (before the scene, the 3D scene, its post-processing / upscaling, the rest)
// averaged over 120 frames, from GL timestamps read three frames later.
struct GpuTimer {
    enum Mark { FrameStart, SceneStart, SceneEnd, PostEnd, FrameEnd, kMarks };
    static constexpr int kRing = 4;
    bool on = false, loaded = false;
    void(APIENTRY* genQueries)(GLsizei, GLuint*) = nullptr;
    void(APIENTRY* queryCounter)(GLuint, GLenum) = nullptr;
    void(APIENTRY* getQueryObjectui64v)(GLuint, GLenum, GLuint64*) = nullptr;
    unsigned q[kRing][kMarks] = {};
    bool used[kRing][kMarks] = {};
    int frame = 0, n = 0;
    double sum[kMarks] = {};
    double cpuPostNs = 0;  // CPU time inside endScene (the post chain's calls, upscaler waits)
    void mark(Mark m) {
        if (!on) return;
        queryCounter(q[frame % kRing][m], GL_TIMESTAMP);
        used[frame % kRing][m] = true;
    }
    void begin() {
        if (!loaded) {
            loaded = true;
            on = SDL_getenv("GD01_GPULOG") != nullptr;
            genQueries = reinterpret_cast<void(APIENTRY*)(GLsizei, GLuint*)>(SDL_GL_GetProcAddress("glGenQueries"));
            queryCounter = reinterpret_cast<void(APIENTRY*)(GLuint, GLenum)>(SDL_GL_GetProcAddress("glQueryCounter"));
            getQueryObjectui64v = reinterpret_cast<void(APIENTRY*)(GLuint, GLenum, GLuint64*)>(SDL_GL_GetProcAddress("glGetQueryObjectui64v"));
            on = on && genQueries && queryCounter && getQueryObjectui64v;
            if (on) genQueries(kRing * kMarks, &q[0][0]);
        }
        if (!on) return;
        ++frame;
        // The ring slot about to be reused holds the frame from kRing frames ago.
        int slot = frame % kRing;
        if (used[slot][FrameStart] && used[slot][FrameEnd]) {
            GLuint64 t[kMarks] = {};
            for (int k = 0; k < kMarks; ++k)
                if (used[slot][k]) getQueryObjectui64v(q[slot][k], GL_QUERY_RESULT, &t[k]);
            const bool scene = used[slot][SceneStart] && used[slot][PostEnd];
            double parts[kMarks] = {};
            parts[0] = double(t[FrameEnd] - t[FrameStart]);
            if (scene) {
                parts[1] = double(t[SceneStart] - t[FrameStart]);
                parts[2] = double(t[SceneEnd] - t[SceneStart]);
                parts[3] = double(t[PostEnd] - t[SceneEnd]);
                parts[4] = double(t[FrameEnd] - t[PostEnd]);
            }
            for (int k = 0; k < kMarks; ++k) sum[k] += parts[k];
            if (++n == 120) {
                SDL_Log("gpu ms: frame %.2f | before scene %.2f, scene %.2f, post/upscale %.2f, after %.2f | cpu post %.2f", sum[0] / n / 1e6,
                        sum[1] / n / 1e6, sum[2] / n / 1e6, sum[3] / n / 1e6, sum[4] / n / 1e6, cpuPostNs / n / 1e6);
                n = 0;
                cpuPostNs = 0;
                for (double& v : sum) v = 0;
            }
        }
        for (bool& u : used[slot]) u = false;
    }
} gpuTimer;

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec3 aNormal;
layout(location = 4) in vec4 aWeights;
uniform mat4 uMvp;
uniform int uSkin;
uniform mat4 uBones[4];
uniform vec2 uUvOffset;
uniform vec4 uTint;      // material colour
uniform int uLit;        // an environment is selected (PS2 lighting programs)
uniform vec3 uMatFlags;  // use_environ, vert_amb, vert_dyn
uniform mat4 uModel;
uniform vec3 uAmbient;
uniform int uLightCount;
uniform vec4 uLightDir[8];
uniform vec4 uLightColor[8];
uniform vec3 uFog;       // enabled, start, end
uniform int uTexgen;     // 5 Environ, 2 Sphere; else the vertex UV (texgen 1/4 baked)
uniform mat3 uTexU;      // Environ: columns U0..U2 (docs/material_stages.md §4.3)
uniform vec2 uTexO;
uniform vec3 uEye;       // camera world position
uniform vec3 uCamRight, uCamUp;
uniform vec2 uJitter;    // NDC
out vec2 vUv;
out vec4 vColor;
out float vFog;
void main() {
    vUv = aUv + uUvOffset;
    vec4 pos = vec4(aPos, 1.0);
    vec3 normal = aNormal;
    if (uSkin != 0) {  // linear blend skinning into world space (uModel is identity)
        pos = vec4(0.0);
        normal = vec3(0.0);
        for (int i = 0; i < 4; ++i) {
            pos += aWeights[i] * (uBones[i] * vec4(aPos, 1.0));
            normal += aWeights[i] * (mat3(uBones[i]) * aNormal);
        }
        pos.w = 1.0;
    }
    if (uTexgen == 5) {  // VU 0x139: reflection of the eye vector through the (unnormalised) world normal
        vec3 wp = (uModel * pos).xyz;
        vec3 N = uTexU * (mat3(uModel) * normal);
        vec3 V = uTexU * (uEye - wp);
        vec3 R = 2.0 * dot(N, V) * N - V;
        vUv = uTexO + R.xy * (0.5 * inversesqrt(max(dot(V, V), 1e-12)));
    } else if (uTexgen == 2) {  // Sphere: camera-relative normal map [I]
        vec3 n = normalize(mat3(uModel) * normal);
        vUv = vec2(0.5) + 0.5 * vec2(dot(n, uCamRight), -dot(n, uCamUp));
    }
    if (uLit != 0) {
        // VU1 lighting (docs/lighting.md): min((vert_amb ? vtx : mat) *
        // (use_environ ? ambient : 1) + (vert_dyn ? vtx : mat) * sum, 1), with
        // n = normal * world (not renormalised); directional lights shine
        // along their +Y (uLightDir = -Y), a point light falls off linearly.
        vec3 n = mat3(uModel) * normal;
        vec3 wp = (uModel * pos).xyz;
        vec3 sum = vec3(0.0);
        for (int i = 0; i < uLightCount; ++i) {
            if (uLightDir[i].w > 0.5) {
                sum += uLightColor[i].rgb * max(dot(n, uLightDir[i].xyz), 0.0);
            } else {
                vec3 d = uLightDir[i].xyz - wp;
                float dist = max(length(d), 1e-4);
                float att = clamp(1.0 - dist / max(uLightColor[i].a, 1e-4), 0.0, 1.0);
                sum += uLightColor[i].rgb * att * max(dot(n, d / dist), 0.0);
            }
        }
        vec4 vtx = uSkin != 0 ? vec4(1.0) : aColor;  // skin programs set the vertex colour to 1
        vec4 amb = uMatFlags.y > 0.5 ? vtx : uTint;
        vec4 dyn = uMatFlags.z > 0.5 ? vtx : uTint;
        vec3 ambientLight = uMatFlags.x > 0.5 ? uAmbient : vec3(1.0);
        vColor = min(vec4(amb.rgb * ambientLight + dyn.rgb * sum, amb.a), vec4(1.0));
    } else {
        // Unlit, !use_environ && !vert_amb: the material colour replaces the
        // vertex colour (VU program 0x614 sends it only with vert_amb).
        // The GS colour is min(colour, 1) (docs/material_stages.md §3), e.g. score_digit.mat (0, .3, 2.7).
        vColor = min(uMatFlags.x < 0.5 && uMatFlags.y < 0.5 ? uTint : aColor * uTint, vec4(1.0));
    }
    gl_Position = uMvp * pos;
    // Fog by view depth: F = clamp((end - w) / (end - start)).
    vFog = uFog.x > 0.5 ? clamp((uFog.z - gl_Position.w) / max(uFog.z - uFog.y, 1e-4), 0.0, 1.0) : 1.0;
    gl_Position.xy += uJitter * gl_Position.w;  // TAA's sub-pixel offset (0 outside the scene pass)
}
)";

const char* kFragmentShader = R"(#version 330 core
in vec2 vUv;
in vec4 vColor;
in float vFog;
uniform sampler2D uTex;
uniform int uUseTex;
uniform float uCutout;
uniform float uColorScale;  // 2 for "Multiply2" stages (colour sent to the GS at x255, not x128)
uniform vec3 uFogColor;
uniform int uTexDecal;  // TFX DECAL: 1 later passes of Multitexture / MultipassSrc mats (texture only), 2 stage-0 blend Src
out vec4 fragColor;
void main() {
    vec4 c = vColor;
    if (uUseTex != 0) {
        vec4 t = texture(uTex, vUv);
        c = uTexDecal == 1 ? t
          : uTexDecal == 2 ? vec4(t.rgb, t.a * c.a)  // pass-0 DECAL; keeps the panel fade alpha
                           : vec4(c.rgb * uColorScale, c.a) * t;
    }
    if (c.a <= uCutout) discard;
    c.rgb = mix(uFogColor, c.rgb, vFog);
    fragColor = c;
}
)";

unsigned compile(GLenum type, const char* src, std::string& error) {
    unsigned s = gl::glCreateShader(type);
    gl::glShaderSource(s, 1, &src, nullptr);
    gl::glCompileShader(s);
    GLint ok = 0;
    gl::glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl::glGetShaderInfoLog(s, sizeof log, nullptr, log);
        error = std::string("shader: ") + log;
        gl::glDeleteShader(s);
        return 0;
    }
    return s;
}

// The debug font covers ASCII 32..126 as 8x8 glyphs in a 16x6 grid.
constexpr int kGlyph = 8, kFontCols = 16, kFontRows = 6;

}  // namespace

bool Renderer::init(SDL_Window* window, std::string& error) {
    window_ = window;
    context_ = SDL_GL_CreateContext(window);
    if (!context_) {
        error = SDL_GetError();
        return false;
    }
    SDL_GL_MakeCurrent(window, context_);
    setFrameCap(60);
    if (const char* missing = gl::load()) {
        error = std::string("OpenGL function missing: ") + missing;
        return false;
    }
    using GetString = const unsigned char*(APIENTRY*)(GLenum);
    if (auto getString = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"))) {
        const char* name = reinterpret_cast<const char*>(getString(GL_RENDERER));
        gpuName_ = name ? name : "";
        SDL_Log("OpenGL: %s (%s)", gpuName_.c_str(), reinterpret_cast<const char*>(getString(GL_VERSION)));
    }

    unsigned vs = compile(GL_VERTEX_SHADER, kVertexShader, error);
    unsigned fs = vs ? compile(GL_FRAGMENT_SHADER, kFragmentShader, error) : 0;
    if (!fs) return false;
    program_ = gl::glCreateProgram();
    gl::glAttachShader(program_, vs);
    gl::glAttachShader(program_, fs);
    gl::glLinkProgram(program_);
    gl::glDeleteShader(vs);
    gl::glDeleteShader(fs);
    GLint ok = 0;
    gl::glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl::glGetProgramInfoLog(program_, sizeof log, nullptr, log);
        error = std::string("shader link: ") + log;
        return false;
    }
    uMvp_ = gl::glGetUniformLocation(program_, "uMvp");
    uJitter_ = gl::glGetUniformLocation(program_, "uJitter");
    uTex_ = gl::glGetUniformLocation(program_, "uTex");
    uUseTex_ = gl::glGetUniformLocation(program_, "uUseTex");
    uCutout_ = gl::glGetUniformLocation(program_, "uCutout");
    uUvOffset_ = gl::glGetUniformLocation(program_, "uUvOffset");
    uTint_ = gl::glGetUniformLocation(program_, "uTint");
    uSkin_ = gl::glGetUniformLocation(program_, "uSkin");
    uBones_ = gl::glGetUniformLocation(program_, "uBones");
    uLit_ = gl::glGetUniformLocation(program_, "uLit");
    uMatFlags_ = gl::glGetUniformLocation(program_, "uMatFlags");
    uColorScale_ = gl::glGetUniformLocation(program_, "uColorScale");
    uFog_ = gl::glGetUniformLocation(program_, "uFog");
    uFogColor_ = gl::glGetUniformLocation(program_, "uFogColor");
    uModel_ = gl::glGetUniformLocation(program_, "uModel");
    uTexgen_ = gl::glGetUniformLocation(program_, "uTexgen");
    uTexU_ = gl::glGetUniformLocation(program_, "uTexU");
    uTexO_ = gl::glGetUniformLocation(program_, "uTexO");
    uEye_ = gl::glGetUniformLocation(program_, "uEye");
    uCamRight_ = gl::glGetUniformLocation(program_, "uCamRight");
    uCamUp_ = gl::glGetUniformLocation(program_, "uCamUp");
    uTexDecal_ = gl::glGetUniformLocation(program_, "uTexDecal");
    uAmbient_ = gl::glGetUniformLocation(program_, "uAmbient");
    uLightCount_ = gl::glGetUniformLocation(program_, "uLightCount");
    uLightDir_ = gl::glGetUniformLocation(program_, "uLightDir");
    uLightColor_ = gl::glGetUniformLocation(program_, "uLightColor");

    gl::glGenVertexArrays(1, &vao_);
    gl::glGenBuffers(1, &vbo_);
    gl::glBindVertexArray(vao_);
    gl::glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
    gl::glEnableVertexAttribArray(1);
    gl::glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
    gl::glEnableVertexAttribArray(2);
    gl::glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));

    createFontTexture();
    return true;
}

void Renderer::shutdown() {
    if (!context_) return;
    for (unsigned t : textures_)
        if (t) gl::glDeleteTextures(1, &t);
    for (GpuMesh& m : meshes_) {
        if (!m.vao) continue;
        gl::glDeleteBuffers(1, &m.vbo);
        gl::glDeleteBuffers(1, &m.ibo);
        gl::glDeleteVertexArrays(1, &m.vao);
    }
    gl::glDeleteBuffers(1, &vbo_);
    gl::glDeleteVertexArrays(1, &vao_);
    gl::glDeleteProgram(program_);
    SDL_GL_DestroyContext(context_);
    context_ = nullptr;
}

int Renderer::createTexture(const Image& image, bool repeat) {
    unsigned id = 0;
    gl::glGenTextures(1, &id);
    gl::glBindTexture(GL_TEXTURE_2D, id);
    gl::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    gl::glGenerateMipmap(GL_TEXTURE_2D);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    textures_.push_back(id);
    return static_cast<int>(textures_.size()) - 1;
}

void Renderer::updateTexture(int texture, const Image& image) {
    if (texture <= 0 || size_t(texture) >= textures_.size()) return;
    gl::glBindTexture(GL_TEXTURE_2D, textures_[size_t(texture)]);
    gl::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
}

void Renderer::createFontTexture() {
    SDL_Surface* surface = SDL_CreateSurface(kFontCols * kGlyph, kFontRows * kGlyph, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* soft = SDL_CreateSoftwareRenderer(surface);
    SDL_SetRenderDrawColor(soft, 0, 0, 0, 0);
    SDL_RenderClear(soft);
    SDL_SetRenderDrawColor(soft, 255, 255, 255, 255);
    for (int c = 32; c < 127; ++c) {
        char s[2] = {static_cast<char>(c), 0};
        int i = c - 32;
        SDL_RenderDebugText(soft, float(i % kFontCols * kGlyph), float(i / kFontCols * kGlyph), s);
    }
    SDL_RenderPresent(soft);
    Image img;
    img.width = surface->w;
    img.height = surface->h;
    img.rgba.resize(size_t(img.width) * img.height * 4);
    for (int y = 0; y < img.height; ++y)
        std::copy_n(static_cast<uint8_t*>(surface->pixels) + y * surface->pitch, img.width * 4, &img.rgba[size_t(y) * img.width * 4]);
    SDL_DestroyRenderer(soft);
    SDL_DestroySurface(surface);

    font_ = createTexture(img);
    gl::glBindTexture(GL_TEXTURE_2D, textures_[font_]);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

// The frame cap (60 as on the PS2; Enhanced Settings: 120, 240) is a cap,
// not the display's rate. When the refresh rate is the cap or a multiple of
// it, vsync stays on (interval 1: many drivers and compositors ignore larger
// intervals, so a 60 cap on a 120 Hz display ran at 120) and endFrame paces
// to the cap, each swap landing on a refresh. Otherwise (a 60 Hz display
// with a 120 cap, 144 Hz with 120) vsync is off and endFrame paces alone.
// Headless drivers are not paced.
void Renderer::setFrameCap(int fps) {
    fps = std::max(fps, 30);
    int interval = 0;
    if (const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_)); m && m->refresh_rate > 0) {
        float n = m->refresh_rate / float(fps);
        if (n >= 0.95f && std::fabs(n - std::round(n)) < 0.05f) interval = int(std::round(n));
    }
    bool headless = SDL_GetCurrentVideoDriver() && std::string(SDL_GetCurrentVideoDriver()) == "offscreen";
    bool vsync = interval > 0 && SDL_GL_SetSwapInterval(1);
    if (!vsync) SDL_GL_SetSwapInterval(0);
    paceNs_ = headless || (vsync && interval == 1) ? 0 : 1000000000ull / uint64_t(fps);
    nextFrameNs_ = 0;
}

// The frame's height when it is not the window's (0: the window's). With an
// upscaler the frame is the screen's resolution, the upscaler's output, so
// it always upscales from below it; a higher Internal Resolution still
// supersamples.
int Renderer::internalHeight(int windowH) const {
    if (post_.upscaler != PostSettings::Upscaler::None && internalH_ <= windowH) return 0;
    return internalH_;
}

std::string Renderer::upscalerStatus() const {
    std::string s = postFx_.upscalerStatus();
    if (float ms = postFx_.upscalerMs(); ms > 0) {
        char cost[40];
        std::snprintf(cost, sizeof cost, ", %.1f ms a frame", double(ms));
        s += cost;
    }
    return s;
}

void Renderer::frameSize(int& w, int& h) const {
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (const int internalH = internalHeight(h); internalH > 0 && h > 0) {
        w = std::max(1, int(std::lround(double(w) * internalH / h)));
        h = internalH;
    }
}

void Renderer::bindMain() { gl::glBindFramebuffer(GL_FRAMEBUFFER, sceneActive_ ? scene_.fbo : mainBound_ ? main_.fbo : 0); }

void Renderer::beginFrame(SDL_Color clear) {
    gpuTimer.begin();
    gpuTimer.mark(GpuTimer::FrameStart);
    int w = 0, h = 0;
    frameSize(w, h);
    // The internal resolution: an off-screen colour + depth target of that size.
    int windowW = 0, windowH = 0;
    SDL_GetWindowSizeInPixels(window_, &windowW, &windowH);
    mainBound_ = internalHeight(windowH) > 0;
    if (mainBound_ && (main_.w != w || main_.h != h || !main_.fbo)) {
        if (main_.fbo) gl::glDeleteFramebuffers(1, &main_.fbo), gl::glDeleteTextures(1, &main_.color), gl::glDeleteRenderbuffers(1, &main_.depth);
        main_ = {};
        gl::glGenTextures(1, &main_.color);
        gl::glBindTexture(GL_TEXTURE_2D, main_.color);
        gl::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl::glGenFramebuffers(1, &main_.fbo);
        gl::glBindFramebuffer(GL_FRAMEBUFFER, main_.fbo);
        gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, main_.color, 0);
        gl::glGenRenderbuffers(1, &main_.depth);
        gl::glBindRenderbuffer(GL_RENDERBUFFER, main_.depth);
        gl::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        gl::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, main_.depth);
        if (gl::glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            SDL_Log("internal resolution %dx%d: framebuffer incomplete, drawing to the window", w, h);
            internalH_ = 0;
            mainBound_ = false;
            SDL_GetWindowSizeInPixels(window_, &w, &h);
        } else {
            main_.w = w, main_.h = h;
        }
    }
    bindMain();
    float scale = std::min(w / kScreenW, h / kScreenH);
    int vw = int(kScreenW * scale), vh = int(kScreenH * scale);
    viewport_[0] = (w - vw) / 2;
    viewport_[1] = (h - vh) / 2;
    viewport_[2] = vw;
    viewport_[3] = vh;

    gl::glDisable(GL_SCISSOR_TEST);
    gl::glDepthMask(GL_TRUE);  // glClear only clears depth while depth writes are enabled
    gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::glViewport(0, 0, w, h);
    gl::glClearColor(0, 0, 0, 1);
    gl::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl::glEnable(GL_SCISSOR_TEST);
    gl::glScissor(viewport_[0], viewport_[1], vw, vh);
    gl::glViewport(viewport_[0], viewport_[1], vw, vh);
    gl::glClearColor(clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, 1);
    gl::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // The window stays opaque: draws keep the cleared alpha of 1 (render
    // targets write alpha, for the crowd impostors).
    gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

    // Column-major orthographic projection: x 0..1280 -> -1..1, y 0..720 -> 1..-1.
    const float m[16] = {2 / kScreenW, 0, 0, 0, 0, -2 / kScreenH, 0, 0, 0, 0, -1, 0, -1, 1, 0, 1};
    std::copy_n(m, 16, screenMvp_);
}

// At most two frames in flight: without this the driver can queue many
// frames (seconds of GPU work with no vsync, e.g. offscreen), and the next
// texture upload then waits for all of them.
void Renderer::endFrame() {
    gpuTimer.mark(GpuTimer::FrameEnd);
    if (mainBound_) {  // the internal resolution, scaled to the window
        int ww = 0, wh = 0;
        SDL_GetWindowSizeInPixels(window_, &ww, &wh);
        gl::glBindFramebuffer(GL_READ_FRAMEBUFFER, main_.fbo);
        gl::glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl::glDisable(GL_SCISSOR_TEST);
        gl::glBlitFramebuffer(0, 0, main_.w, main_.h, 0, 0, ww, wh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gl::glEnable(GL_SCISSOR_TEST);
    }
    if (paceNs_) {
        uint64_t now = SDL_GetTicksNS();
        if (nextFrameNs_ > now && nextFrameNs_ - now <= paceNs_) SDL_DelayPrecise(nextFrameNs_ - now);
        // The next deadline keeps the 60 Hz grid unless a slow frame fell behind it.
        now = SDL_GetTicksNS();
        nextFrameNs_ = nextFrameNs_ + paceNs_ >= now ? nextFrameNs_ + paceNs_ : now + paceNs_;
    }
    SDL_GL_SwapWindow(window_);
    if (fences_[1]) {
        gl::glClientWaitSync(static_cast<GLsync>(fences_[1]), GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        gl::glDeleteSync(static_cast<GLsync>(fences_[1]));
    }
    fences_[1] = fences_[0];
    fences_[0] = gl::glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

int Renderer::createRenderTarget(int width, int height) {
    unsigned tex = 0, fbo = 0, depth = 0;
    gl::glGenTextures(1, &tex);
    gl::glBindTexture(GL_TEXTURE_2D, tex);
    gl::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::glGenFramebuffers(1, &fbo);
    gl::glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    gl::glGenRenderbuffers(1, &depth);
    gl::glBindRenderbuffer(GL_RENDERBUFFER, depth);
    gl::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    gl::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    bool ok = gl::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    bindMain();
    if (!ok) return 0;
    textures_.push_back(tex);
    int handle = static_cast<int>(textures_.size()) - 1;
    targets_[handle] = {fbo, width, height};
    return handle;
}

void Renderer::beginRenderTarget(int texture) {
    auto it = targets_.find(texture);
    if (it == targets_.end()) return;
    setJitter(0, 0);  // impostors and other targets are not part of the TAA frame
    gl::glBindFramebuffer(GL_FRAMEBUFFER, it->second.fbo);
    gl::glDisable(GL_SCISSOR_TEST);
    gl::glViewport(0, 0, it->second.width, it->second.height);
    gl::glDepthMask(GL_TRUE);
    gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::glClearColor(0, 0, 0, 0);
    gl::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::endRenderTarget() {
    bindMain();
    if (sceneActive_) setJitter(sceneJitter_[0], sceneJitter_[1]);
    gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    gl::glEnable(GL_SCISSOR_TEST);
    gl::glScissor(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
    gl::glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
}

namespace {

const char* kBlurVertexShader = R"(#version 330 core
uniform float uDepth;
void main() {
    vec2 p = vec2((gl_VertexID & 1) * 2 - 1, (gl_VertexID >> 1) * 2 - 1);
    gl_Position = vec4(p, uDepth * 2.0 - 1.0, 1.0);
}
)";

const char* kBlurFragmentShader = R"(#version 330 core
uniform sampler2D uTex;
uniform vec2 uSize;
uniform vec2 uOrigin;
uniform float uTap;
out vec4 fragColor;
void main() {
    // The PS2 averages 4 taps (0 and +3 px each way, re-centred by the
    // composite's -1.5 px) on a 448-line screen, where the TV softens them.
    // At PC resolutions point taps that far apart show as a doubled image, so
    // the same spread (±1.5 px, std ≈ 0.43 tap) is a 5x5 tent instead.
    vec2 uv = (gl_FragCoord.xy - uOrigin) / uSize;
    vec2 t = vec2(uTap) / uSize;
    vec3 c = vec3(0.0);
    float wsum = 0.0;
    for (int j = -2; j <= 2; ++j)
        for (int i = -2; i <= 2; ++i) {
            float w = float(3 - abs(i)) * float(3 - abs(j));
            c += w * texture(uTex, uv + vec2(float(i), float(j)) * 0.375 * t).rgb;
            wsum += w;
        }
    fragColor = vec4(c / wsum, 1.0);
}
)";

}  // namespace

bool Renderer::ensureBlurProgram() {
    if (blurProgram_) return blurProgram_ != ~0u;
    std::string error;
    unsigned vs = compile(GL_VERTEX_SHADER, kBlurVertexShader, error);
    unsigned fs = vs ? compile(GL_FRAGMENT_SHADER, kBlurFragmentShader, error) : 0;
    if (!fs) {
        SDL_Log("depth of field: %s", error.c_str());
        if (vs) gl::glDeleteShader(vs);
        blurProgram_ = ~0u;
        return false;
    }
    blurProgram_ = gl::glCreateProgram();
    gl::glAttachShader(blurProgram_, vs);
    gl::glAttachShader(blurProgram_, fs);
    gl::glLinkProgram(blurProgram_);
    gl::glDeleteShader(vs);
    gl::glDeleteShader(fs);
    GLint ok = 0;
    gl::glGetProgramiv(blurProgram_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl::glGetProgramInfoLog(blurProgram_, sizeof log, nullptr, log);
        SDL_Log("depth of field: shader link: %s", log);
        gl::glDeleteProgram(blurProgram_);
        blurProgram_ = ~0u;
        return false;
    }
    uBlurTex_ = gl::glGetUniformLocation(blurProgram_, "uTex");
    uBlurDepth_ = gl::glGetUniformLocation(blurProgram_, "uDepth");
    uBlurTap_ = gl::glGetUniformLocation(blurProgram_, "uTap");
    uBlurSize_ = gl::glGetUniformLocation(blurProgram_, "uSize");
    uBlurOrigin_ = gl::glGetUniformLocation(blurProgram_, "uOrigin");
    return true;
}

std::vector<std::string> Renderer::compileAllShaders(int& programs) {
    std::vector<std::string> failed;
    programs = 2;  // the main program (init) and depth of field
    if (!ensureBlurProgram()) failed.push_back("depth of field");
    bool postOk = postFx_.ready();
    programs += PostFx::kPrograms;
    if (!postOk) failed.insert(failed.end(), postFx_.failedPrograms().begin(), postFx_.failedPrograms().end());
    return failed;
}

void Renderer::depthBlur(float depth, float tapPx) {
    if (depth <= 0 || depth >= 1) return;
    if (!ensureBlurProgram()) return;
    const int x = viewport_[0], y = viewport_[1], w = viewport_[2], h = viewport_[3];
    if (!blurTex_) gl::glGenTextures(1, &blurTex_);
    gl::glActiveTexture(GL_TEXTURE0);
    gl::glBindTexture(GL_TEXTURE_2D, blurTex_);
    if (blurSize_[0] != w || blurSize_[1] != h) {
        gl::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        blurSize_[0] = w, blurSize_[1] = h;
    }
    gl::glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, w, h);
    // The composite sprite sits at the focus depth with ZTST GEQUAL (PS2 Z grows
    // toward the viewer): it covers only pixels at or beyond the focus plane.
    gl::glUseProgram(blurProgram_);
    gl::glUniform1i(uBlurTex_, 0);
    gl::glUniform1f(uBlurDepth_, depth);
    gl::glUniform1f(uBlurTap_, tapPx);
    gl::glUniform2f(uBlurSize_, float(w), float(h));
    gl::glUniform2f(uBlurOrigin_, float(x), float(y));
    gl::glDisable(GL_BLEND);
    gl::glDisable(GL_CULL_FACE);
    gl::glEnable(GL_DEPTH_TEST);
    gl::glDepthFunc(GL_LEQUAL);
    gl::glDepthMask(GL_FALSE);
    gl::glBindVertexArray(vao_);
    gl::glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

float Renderer::readDepth(int x, int y) {
    float d = 1;
    gl::glReadPixels(viewport_[0] + x, viewport_[1] + y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &d);
    return d;
}

void Renderer::readDepthsAsync(unsigned& buffer, size_t& capacity, const std::vector<std::array<int, 2>>& points) {
    if (points.empty()) return;
    if (!buffer) gl::glGenBuffers(1, &buffer);
    gl::glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
    if (points.size() > capacity) {
        capacity = std::max<size_t>(points.size(), 64);
        gl::glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(capacity * sizeof(float)), nullptr, GL_STREAM_READ);
    }
    for (size_t i = 0; i < points.size(); ++i)
        gl::glReadPixels(viewport_[0] + points[i][0], viewport_[1] + points[i][1], 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT,
                         reinterpret_cast<void*>(i * sizeof(float)));
    gl::glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

std::vector<float> Renderer::mapDepths(unsigned buffer, size_t count) {
    std::vector<float> out;
    if (!buffer || !count) return out;
    gl::glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
    if (const void* p = gl::glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, GLsizeiptr(count * sizeof(float)), GL_MAP_READ_BIT)) {
        const float* f = static_cast<const float*>(p);
        out.assign(f, f + count);
        gl::glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    gl::glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    return out;
}

float Renderer::beginWide(float maxAspect) {
    int w = 0, h = 0;
    frameSize(w, h);
    saved_[0] = viewport_[0], saved_[1] = viewport_[1], saved_[2] = viewport_[2], saved_[3] = viewport_[3];
    const int vh = viewport_[3];
    int vw = std::min(w, int(float(vh) * maxAspect));
    vw = std::max(vw, viewport_[2]);
    viewport_[0] = (w - vw) / 2;
    viewport_[2] = vw;
    gl::glScissor(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
    gl::glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
    return float(vw) / float(vh);
}

void Renderer::endWide() {
    for (int k = 0; k < 4; ++k) viewport_[k] = saved_[k];
    gl::glScissor(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
    gl::glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
}

void Renderer::clip43() {
    int w = std::min(viewport_[2], viewport_[3] * 4 / 3);
    gl::glScissor(viewport_[0] + (viewport_[2] - w) / 2, viewport_[1], w, viewport_[3]);
}

void Renderer::clipEnd() { gl::glScissor(viewport_[0], viewport_[1], viewport_[2], viewport_[3]); }

void Renderer::clearDepth() {
    gl::glDepthMask(GL_TRUE);  // glClear only clears depth while depth writes are enabled
    gl::glClear(GL_DEPTH_BUFFER_BIT);
}

void Renderer::applyState(int texture, Blend blend, bool depthTest, bool depthWrite, const float* mvp) {
    gl::glUseProgram(program_);
    gl::glUniformMatrix4fv(uMvp_, 1, GL_FALSE, mvp ? mvp : screenMvp_);
    gl::glActiveTexture(GL_TEXTURE0);
    gl::glUniform1i(uTex_, 0);
    gl::glUniform1i(uUseTex_, texture > 0);
    if (texture > 0) gl::glBindTexture(GL_TEXTURE_2D, textures_[texture]);
    if (depthTest) gl::glEnable(GL_DEPTH_TEST);
    else gl::glDisable(GL_DEPTH_TEST);
    gl::glDepthFunc(GL_LEQUAL);
    gl::glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
    gl::glBlendEquation(GL_FUNC_ADD);
    switch (blend) {
    case Blend::Opaque: gl::glDisable(GL_BLEND); break;
    case Blend::Alpha: gl::glEnable(GL_BLEND); gl::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
    case Blend::Add: gl::glEnable(GL_BLEND); gl::glBlendFunc(GL_ONE, GL_ONE); break;
    case Blend::AddAlpha: gl::glEnable(GL_BLEND); gl::glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
    case Blend::Multiply: gl::glEnable(GL_BLEND); gl::glBlendFunc(GL_DST_COLOR, GL_ZERO); break;
    case Blend::Subtract:
        gl::glEnable(GL_BLEND);
        gl::glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        gl::glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        break;
    case Blend::None: gl::glEnable(GL_BLEND); gl::glBlendFunc(GL_ZERO, GL_ONE); break;
    }
}

void Renderer::draw(const Vertex* v, size_t count, int texture, Blend blend, const float* mvp, bool depthTest,
                    bool depthWrite) {
    if (count == 0) return;
    applyState(texture, blend, depthTest, depthWrite, mvp);
    gl::glUniform1f(uCutout_, 0.0f);  // 2D: drop fully transparent texels
    gl::glUniform2f(uUvOffset_, 0, 0);
    const float white[4] = {1, 1, 1, 1};
    gl::glUniform4fv(uTint_, 1, white);
    gl::glUniform1i(uLit_, 0);
    gl::glUniform1i(uSkin_, 0);
    gl::glUniform1f(uColorScale_, 1.0f);
    gl::glUniform3f(uFog_, 0, 0, 0);
    gl::glUniform3f(uMatFlags_, 1, 0, 0);  // the vertex colour
    gl::glUniform1i(uTexgen_, 0);
    gl::glUniform1i(uTexDecal_, 0);
    gl::glDisable(GL_CULL_FACE);
    gl::glBindVertexArray(vao_);
    gl::glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::glBufferData(GL_ARRAY_BUFFER, count * sizeof(Vertex), v, GL_STREAM_DRAW);
    gl::glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count));
}

int Renderer::createMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    GpuMesh m;
    gl::glGenVertexArrays(1, &m.vao);
    gl::glGenBuffers(1, &m.vbo);
    gl::glGenBuffers(1, &m.ibo);
    gl::glBindVertexArray(m.vao);
    gl::glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    gl::glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
    gl::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    gl::glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
    gl::glEnableVertexAttribArray(1);
    gl::glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
    gl::glEnableVertexAttribArray(2);
    gl::glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));
    gl::glEnableVertexAttribArray(3);
    gl::glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, nx)));
    gl::glEnableVertexAttribArray(4);
    gl::glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, w)));
    m.count = static_cast<int>(indices.size());
    meshes_.push_back(m);
    return static_cast<int>(meshes_.size()) - 1;
}

void Renderer::updateMesh(int mesh, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    if (mesh <= 0 || size_t(mesh) >= meshes_.size()) return;
    GpuMesh& m = meshes_[size_t(mesh)];
    gl::glBindVertexArray(m.vao);
    gl::glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    gl::glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
    gl::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    gl::glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
    m.count = static_cast<int>(indices.size());
}

void Renderer::drawMesh(int mesh, const float* mvp, const RenderState& st) {
    if (mesh <= 0 || meshes_[mesh].count == 0) return;
    applyState(st.texture, st.blend, st.depthTest, st.depthWrite, mvp);
    gl::glUniform1f(uCutout_, st.alphaCutout ? 0.5f : -1.0f);
    gl::glUniform2f(uUvOffset_, st.uvOffset[0], st.uvOffset[1]);
    gl::glUniform4fv(uTint_, 1, st.tint);
    if (st.bones) {
        gl::glUniform1i(uSkin_, 1);
        gl::glUniformMatrix4fv(uBones_, 4, GL_FALSE, st.bones);
    } else {
        gl::glUniform1i(uSkin_, 0);
    }
    gl::glUniform1f(uColorScale_, st.colorScale);
    gl::glUniform1i(uTexDecal_, st.texDecal);
    gl::glUniform1i(uTexgen_, st.model && (st.texgen == 5 || st.texgen == 2) ? st.texgen : 0);
    if (st.model && (st.texgen == 5 || st.texgen == 2)) {
        gl::glUniformMatrix4fv(uModel_, 1, GL_FALSE, st.model);
        gl::glUniformMatrix3fv(uTexU_, 1, GL_FALSE, st.texU);
        gl::glUniform2fv(uTexO_, 1, st.texO);
        gl::glUniform3fv(uEye_, 1, st.eye);
        gl::glUniform3fv(uCamRight_, 1, st.camRight);
        gl::glUniform3fv(uCamUp_, 1, st.camUp);
    }
    gl::glUniform3f(uMatFlags_, st.useEnviron ? 1.0f : 0.0f, st.vertAmb ? 1.0f : 0.0f, st.vertDyn ? 1.0f : 0.0f);
    if (st.fog) {
        gl::glUniform3f(uFog_, 1, st.fogStart, st.fogEnd);
        gl::glUniform3fv(uFogColor_, 1, st.fogColor);
    } else {
        gl::glUniform3f(uFog_, 0, 0, 0);
    }
    if (st.lighting && st.model) {
        const Lighting& l = *st.lighting;
        gl::glUniform1i(uLit_, 1);
        gl::glUniformMatrix4fv(uModel_, 1, GL_FALSE, st.model);
        gl::glUniform3fv(uAmbient_, 1, l.ambient);
        gl::glUniform1i(uLightCount_, l.count);
        if (l.count > 0) {
            gl::glUniform4fv(uLightDir_, l.count, &l.dir[0][0]);
            gl::glUniform4fv(uLightColor_, l.count, &l.color[0][0]);
        }
    } else {
        gl::glUniform1i(uLit_, 0);
    }
    if (st.cull) {
        gl::glEnable(GL_CULL_FACE);
        gl::glCullFace(st.mirrored ? GL_FRONT : GL_BACK);
    } else {
        gl::glDisable(GL_CULL_FACE);
    }
    if (st.decal) {
        gl::glEnable(GL_POLYGON_OFFSET_FILL);
        gl::glPolygonOffset(-1, -1);
    }
    gl::glBindVertexArray(meshes_[mesh].vao);
    gl::glDrawElements(GL_TRIANGLES, meshes_[mesh].count, GL_UNSIGNED_INT, nullptr);
    if (st.decal) gl::glDisable(GL_POLYGON_OFFSET_FILL);
}

void Renderer::grabNdc(int slot, float x0, float y0, float x1, float y1) {
    Grab& g = grabs_[slot & 1];
    g.valid = false;
    int px0 = viewport_[0] + int(std::floor((x0 + 1) * 0.5f * float(viewport_[2])));
    int px1 = viewport_[0] + int(std::ceil((x1 + 1) * 0.5f * float(viewport_[2])));
    int py0 = viewport_[1] + int(std::floor((y1 + 1) * 0.5f * float(viewport_[3])));  // bottom
    int py1 = viewport_[1] + int(std::ceil((y0 + 1) * 0.5f * float(viewport_[3])));   // top
    int w = px1 - px0, h = py1 - py0;
    if (w <= 0 || h <= 0) return;
    if (!g.texture || g.w != w || g.h != h) {
        Image blank;
        blank.width = w, blank.height = h;
        blank.rgba.assign(size_t(w) * size_t(h) * 4, 0);
        if (!g.texture) g.texture = createTexture(blank, false);
        else updateTexture(g.texture, blank);
        g.w = w, g.h = h;
    }
    gl::glBindTexture(GL_TEXTURE_2D, textures_[size_t(g.texture)]);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl::glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, px0, py0, w, h);
    g.px = px0, g.py = py0;
    // Back to menu units for the 2D draw (the rect rounded to whole pixels).
    g.vx0 = float(px0 - viewport_[0]) / float(viewport_[2]) * kScreenW;
    g.vx1 = float(px1 - viewport_[0]) / float(viewport_[2]) * kScreenW;
    g.vy0 = (1 - float(py1 - viewport_[1]) / float(viewport_[3])) * kScreenH;
    g.vy1 = (1 - float(py0 - viewport_[1]) / float(viewport_[3])) * kScreenH;
    g.valid = true;
}

void Renderer::drawGrab(int slot, float add) {
    const Grab& g = grabs_[slot & 1];
    if (!g.valid) return;
    // GL copies are bottom-up: v 1 on the top edge. Alpha 255 at the top, 0 at the bottom.
    const Vertex v[6] = {{g.vx0, g.vy0, 0, 0, 1, 255, 255, 255, 255}, {g.vx1, g.vy0, 0, 1, 1, 255, 255, 255, 255},
                         {g.vx1, g.vy1, 0, 1, 0, 255, 255, 255, 0},   {g.vx0, g.vy0, 0, 0, 1, 255, 255, 255, 255},
                         {g.vx1, g.vy1, 0, 1, 0, 255, 255, 255, 0},   {g.vx0, g.vy1, 0, 0, 0, 255, 255, 255, 0}};
    draw(v, 6, g.texture, Blend::Alpha);
    if (add > 0) {
        uint8_t a = uint8_t(std::lround(add * 255));
        const Vertex q[6] = {{g.vx0, g.vy0, 0, 0, 0, a, a, a, 255}, {g.vx1, g.vy0, 0, 0, 0, a, a, a, 255},
                             {g.vx1, g.vy1, 0, 0, 0, a, a, a, 255}, {g.vx0, g.vy0, 0, 0, 0, a, a, a, 255},
                             {g.vx1, g.vy1, 0, 0, 0, a, a, a, 255}, {g.vx0, g.vy1, 0, 0, 0, a, a, a, 255}};
        draw(q, 6, 0, Blend::Add);
    }
}

void Renderer::windowToMenu(float x, float y, float& mx, float& my) const {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    float density = SDL_GetWindowPixelDensity(window_);
    if (density <= 0) density = 1;
    float scale = std::min(w / kScreenW, h / kScreenH);
    float vw = kScreenW * scale, vh = kScreenH * scale;
    float ox = (w - vw) / 2, oy = (h - vh) / 2;
    mx = (x * density - ox) / vw * kScreenW;
    my = (y * density - oy) / vh * kScreenH;
}

void Renderer::fillRect(float x, float y, float w, float h, SDL_Color c) {
    const Vertex v[6] = {{x, y, 0, 0, 0, c.r, c.g, c.b, c.a},         {x + w, y, 0, 1, 0, c.r, c.g, c.b, c.a},
                         {x + w, y + h, 0, 1, 1, c.r, c.g, c.b, c.a}, {x, y, 0, 0, 0, c.r, c.g, c.b, c.a},
                         {x + w, y + h, 0, 1, 1, c.r, c.g, c.b, c.a}, {x, y + h, 0, 0, 1, c.r, c.g, c.b, c.a}};
    draw(v, 6, 0, c.a == 255 ? Blend::Opaque : Blend::Alpha);
}

void Renderer::image(int texture, float x, float y, float w, float h, SDL_Color t, Blend blend) {
    const Vertex v[6] = {{x, y, 0, 0, 0, t.r, t.g, t.b, t.a},         {x + w, y, 0, 1, 0, t.r, t.g, t.b, t.a},
                         {x + w, y + h, 0, 1, 1, t.r, t.g, t.b, t.a}, {x, y, 0, 0, 0, t.r, t.g, t.b, t.a},
                         {x + w, y + h, 0, 1, 1, t.r, t.g, t.b, t.a}, {x, y + h, 0, 0, 1, t.r, t.g, t.b, t.a}};
    draw(v, 6, texture, blend);
}

void Renderer::text(float x, float y, float scale, SDL_Color c, const std::string& s) {
    std::vector<Vertex> v;
    v.reserve(s.size() * 6);
    const float g = kGlyph * scale;
    const float du = 1.0f / kFontCols, dv = 1.0f / kFontRows;
    for (size_t i = 0; i < s.size(); ++i) {
        int ch = static_cast<unsigned char>(s[i]);
        if (ch < 32 || ch > 126) ch = '?';
        int idx = ch - 32;
        float u0 = (idx % kFontCols) * du, v0 = (idx / kFontCols) * dv;
        float px = x + i * g;
        Vertex a{px, y, 0, u0, v0, c.r, c.g, c.b, c.a}, b{px + g, y, 0, u0 + du, v0, c.r, c.g, c.b, c.a};
        Vertex d{px + g, y + g, 0, u0 + du, v0 + dv, c.r, c.g, c.b, c.a}, e{px, y + g, 0, u0, v0 + dv, c.r, c.g, c.b, c.a};
        v.insert(v.end(), {a, b, d, a, d, e});
    }
    draw(v.data(), v.size(), font_, Blend::Alpha);
}

void Renderer::textCentered(float cx, float y, float scale, SDL_Color c, const std::string& s) {
    text(cx - s.size() * kGlyph * scale * 0.5f, y, scale, c, s);
}

Image Renderer::readPixels() {
    // The 16:9 area's rows across the whole window width (ultrawide scenery included).
    Image img;
    int w = 0, h = 0;
    frameSize(w, h);
    img.width = std::max(w, viewport_[2]);
    img.height = viewport_[3];
    img.rgba.resize(size_t(img.width) * img.height * 4);
    gl::glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl::glReadPixels(img.width == viewport_[2] ? viewport_[0] : 0, viewport_[1], img.width, img.height, GL_RGBA, GL_UNSIGNED_BYTE,
                     img.rgba.data());
    // GL rows are bottom-up.
    std::vector<uint8_t> row(size_t(img.width) * 4);
    for (int y = 0; y < img.height / 2; ++y) {
        uint8_t* a = &img.rgba[size_t(y) * img.width * 4];
        uint8_t* b = &img.rgba[size_t(img.height - 1 - y) * img.width * 4];
        std::copy_n(a, row.size(), row.data());
        std::copy_n(b, row.size(), a);
        std::copy_n(row.data(), row.size(), b);
    }
    return img;
}

// ---------------------------------------------------------------- post-processing

void Renderer::setJitter(float x, float y) {
    gl::glUseProgram(program_);
    gl::glUniform2f(uJitter_, x, y);
}

bool Renderer::beginScene() {
    if (!post_.any() || sceneActive_) return false;
    // A shader the driver rejects (logged by PostFx) turns the effects off:
    // the scene then draws straight to the frame, as with every effect off,
    // instead of into a target nothing would show (a black venue).
    if (!postFx_.ready()) {
        static bool told = false;
        if (!told) SDL_Log("post-processing is off: a shader failed to compile (see above); drawing without effects");
        told = true;
        return false;
    }
    const int vw = viewport_[2], vh = viewport_[3];
    const float scale = post_.renderScale();
    const int sw = std::max(1, int(std::lround(vw * scale))), sh = std::max(1, int(std::lround(vh * scale)));
    if (!scene_.fbo || scene_.w != sw || scene_.h != sh) {
        if (!scene_.fbo) {
            gl::glGenFramebuffers(1, &scene_.fbo);
            gl::glGenTextures(1, &scene_.color);
            gl::glGenTextures(1, &scene_.depth);
        }
        gl::glBindTexture(GL_TEXTURE_2D, scene_.color);
        gl::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sw, sh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl::glBindTexture(GL_TEXTURE_2D, scene_.depth);
        gl::glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, sw, sh, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl::glBindFramebuffer(GL_FRAMEBUFFER, scene_.fbo);
        gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_.color, 0);
        gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, scene_.depth, 0);
        if (gl::glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            SDL_Log("post-processing: scene target %dx%d incomplete", sw, sh);
            bindMain();
            return false;
        }
        scene_.w = sw, scene_.h = sh;
    }
    for (int k = 0; k < 4; ++k) sceneSaved_[k] = viewport_[k];
    sceneActive_ = true;
    bindMain();
    viewport_[0] = viewport_[1] = 0, viewport_[2] = sw, viewport_[3] = sh;
    gl::glViewport(0, 0, sw, sh);
    gl::glScissor(0, 0, sw, sh);
    gl::glDepthMask(GL_TRUE);
    gl::glClearColor(0, 0, 0, 1);
    gl::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gpuTimer.mark(GpuTimer::SceneStart);
    sceneJitter_ = postFx_.jitter(post_, sw, sh);
    setJitter(sceneJitter_[0], sceneJitter_[1]);
    return true;
}

void Renderer::endScene(const PostFrame& frame) {
    if (!sceneActive_) return;
    setJitter(0, 0);
    sceneJitter_ = {0, 0};
    gpuTimer.mark(GpuTimer::SceneEnd);
    const uint64_t cpu0 = gpuTimer.on ? SDL_GetTicksNS() : 0;
    unsigned out = postFx_.process(scene_.color, scene_.depth, scene_.w, scene_.h, sceneSaved_[2], sceneSaved_[3], post_, frame);
    sceneActive_ = false;
    for (int k = 0; k < 4; ++k) viewport_[k] = sceneSaved_[k];
    bindMain();
    gl::glEnable(GL_SCISSOR_TEST);
    gl::glScissor(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
    gl::glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
    gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    gl::glDisable(GL_BLEND);
    gl::glDisable(GL_DEPTH_TEST);
    if (out) postFx_.blit(out);
    gpuTimer.mark(GpuTimer::PostEnd);
    if (gpuTimer.on) gpuTimer.cpuPostNs += double(SDL_GetTicksNS() - cpu0);
    gl::glUseProgram(program_);
    gl::glBindVertexArray(vao_);
    gl::glActiveTexture(GL_TEXTURE0);
}
