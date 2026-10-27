// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "gfx.h"

#include <SDL3/SDL_opengl.h>
#include <stb_image.h>
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// The launcher's built-in resources (res/, embedded by CMake: cmake/embed_file.cmake).
extern const unsigned char kGdulIconPng[];
extern const std::size_t kGdulIconPngSize;
extern const unsigned char kGdulBackdropJpg[];
extern const std::size_t kGdulBackdropJpgSize;
extern const unsigned char kFontMetalMania[];
extern const std::size_t kFontMetalManiaSize;
extern const unsigned char kFontBarlowRegular[];
extern const std::size_t kFontBarlowRegularSize;
extern const unsigned char kFontBarlowSemiBold[];
extern const std::size_t kFontBarlowSemiBoldSize;

namespace gfx {

namespace {

// The OpenGL functions the launcher uses, loaded through SDL (no GL import library).
#define GDUL_GL_FUNCS(X)                                                                                  \
    X(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                                                 \
    X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                           \
    X(void, glClear, (GLbitfield))                                                                        \
    X(void, glEnable, (GLenum))                                                                           \
    X(void, glDisable, (GLenum))                                                                          \
    X(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                                        \
    X(void, glGenTextures, (GLsizei, GLuint*))                                                            \
    X(void, glDeleteTextures, (GLsizei, const GLuint*))                                                   \
    X(void, glBindTexture, (GLenum, GLuint))                                                              \
    X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))   \
    X(void, glTexParameteri, (GLenum, GLenum, GLint))                                                     \
    X(void, glPixelStorei, (GLenum, GLint))                                                               \
    X(void, glGenerateMipmap, (GLenum))                                                                   \
    X(void, glActiveTexture, (GLenum))                                                                    \
    X(GLuint, glCreateShader, (GLenum))                                                                   \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))                        \
    X(void, glCompileShader, (GLuint))                                                                    \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint*))                                                      \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                     \
    X(void, glDeleteShader, (GLuint))                                                                     \
    X(GLuint, glCreateProgram, (void))                                                                    \
    X(void, glAttachShader, (GLuint, GLuint))                                                             \
    X(void, glLinkProgram, (GLuint))                                                                      \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint*))                                                     \
    X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                    \
    X(void, glDeleteProgram, (GLuint))                                                                    \
    X(void, glUseProgram, (GLuint))                                                                       \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar*))                                               \
    X(void, glUniform2f, (GLint, GLfloat, GLfloat))                                                       \
    X(void, glUniform1i, (GLint, GLint))                                                                  \
    X(void, glGenVertexArrays, (GLsizei, GLuint*))                                                        \
    X(void, glDeleteVertexArrays, (GLsizei, const GLuint*))                                               \
    X(void, glBindVertexArray, (GLuint))                                                                  \
    X(void, glGenBuffers, (GLsizei, GLuint*))                                                             \
    X(void, glDeleteBuffers, (GLsizei, const GLuint*))                                                    \
    X(void, glBindBuffer, (GLenum, GLuint))                                                               \
    X(void, glBufferData, (GLenum, GLsizeiptr, const void*, GLenum))                                      \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))              \
    X(void, glEnableVertexAttribArray, (GLuint))                                                          \
    X(void, glDrawArrays, (GLenum, GLint, GLsizei))                                                       \
    X(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))                        \
    X(void, glBindFramebuffer, (GLenum, GLuint))                                                          \
    X(void, glScissor, (GLint, GLint, GLsizei, GLsizei))                                                  \
    X(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(void, glReadBuffer, (GLenum))                                                                       \
    X(void, glGetIntegerv, (GLenum, GLint*))

#define GDUL_GL_DECLARE(ret, name, args) ret(APIENTRY* name) args = nullptr;
struct Gl {
    GDUL_GL_FUNCS(GDUL_GL_DECLARE)
} gl;

bool loadGl(std::string& missing) {
#define GDUL_GL_LOAD(ret, name, args)                                                     \
    gl.name = reinterpret_cast<ret(APIENTRY*) args>(SDL_GL_GetProcAddress(#name));        \
    if (!gl.name) {                                                                       \
        missing = #name;                                                                  \
        return false;                                                                     \
    }
    GDUL_GL_FUNCS(GDUL_GL_LOAD)
    return true;
}

// GLSL 3.30 core on desktops; GLSL ES 3.00 where the context is OpenGL ES 3.0
// (Android, or a GDUL_GLES build).
#if defined(__ANDROID__) || defined(GDUL_GLES)
#define GDUL_GLSL_HEADER "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#else
#define GDUL_GLSL_HEADER "#version 330 core\n"
#endif

const char* kVertexShader = GDUL_GLSL_HEADER R"(
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aLocal;
layout(location = 4) in vec2 aShape;
uniform vec2 uScreen;
out vec2 vUv;
out vec4 vColor;
out vec4 vLocal;
out vec2 vShape;
void main() {
    vUv = aUv; vColor = aColor; vLocal = aLocal; vShape = aShape;
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
}
)";

// Modes (vShape.y): 0 image, 1 text (atlas coverage), 2 filled rounded rect,
// 3 ring (width in vUv.x), 4 glow (blur in vUv.y), 5 image in a rounded rect. vLocal: the pixel's
// position from the rectangle's centre (xy) and its half size (zw); vShape.x
// the corner radius.
const char* kFragmentShader = GDUL_GLSL_HEADER R"(
in vec2 vUv;
in vec4 vColor;
in vec4 vLocal;
in vec2 vShape;
uniform sampler2D uTex;
out vec4 oColor;
float roundedBox(vec2 p, vec2 h, float r) {
    vec2 q = abs(p) - (h - vec2(r));
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    float mode = vShape.y;
    if (mode < 0.5) { oColor = texture(uTex, vUv) * vColor; return; }
    if (mode < 1.5) { oColor = vec4(vColor.rgb, vColor.a * texture(uTex, vUv).r); return; }
    float d = roundedBox(vLocal.xy, vLocal.zw, vShape.x);
    float aa = max(fwidth(d), 1e-4) * 0.7;
    float a;
    if (mode < 2.5) a = 1.0 - smoothstep(-aa, aa, d);
    else if (mode < 3.5) a = (1.0 - smoothstep(-aa, aa, d)) * smoothstep(-aa, aa, d + vUv.x);
    else if (mode < 4.5) { float t = clamp(d / vUv.y, 0.0, 1.0); a = (1.0 - t) * (1.0 - t); }
    else { vec4 c = texture(uTex, vUv) * vColor; oColor = vec4(c.rgb, c.a * (1.0 - smoothstep(-aa, aa, d))); return; }
    oColor = vec4(vColor.rgb, vColor.a * a);
}
)";

GLuint compile(GLenum type, const char* src, std::string& error) {
    GLuint s = gl.glCreateShader(type);
    gl.glShaderSource(s, 1, &src, nullptr);
    gl.glCompileShader(s);
    GLint ok = 0;
    gl.glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        gl.glGetShaderInfoLog(s, sizeof log, nullptr, log);
        error = std::string("shader: ") + log;
        gl.glDeleteShader(s);
        return 0;
    }
    return s;
}

// Next code point of a UTF-8 string (invalid bytes read as Latin-1).
uint32_t nextCodepoint(const std::string& s, size_t& i) {
    const auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
    uint32_t c = b(i);
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (extra == 0 || i + size_t(extra) >= s.size()) {
        ++i;
        return c;
    }
    uint32_t cp = c & (0x3F >> extra);
    for (int k = 1; k <= extra; ++k) cp = (cp << 6) | (b(i + size_t(k)) & 0x3F);
    i += size_t(extra) + 1;
    return cp;
}

const unsigned char* fontData(FontId f) {
    switch (f) {
    case FontId::Title: return kFontMetalMania;
    case FontId::SemiBold: return kFontBarlowSemiBold;
    default: return kFontBarlowRegular;
    }
}

}  // namespace

// One font at one pixel size: the glyphs of Latin-1 and some punctuation,
// packed into a single-channel texture.
struct Renderer::Atlas {
    static constexpr int kSize = 1024;
    GLuint tex = 0;
    float ascent = 0, lineHeight = 0;
    std::vector<stbtt_packedchar> chars;
    std::map<uint32_t, int> index;
};

Renderer::Renderer() = default;
Renderer::~Renderer() { shutdown(); }

bool Renderer::init(std::string& error) {
    std::string missing;
    if (!loadGl(missing)) {
        error = "OpenGL function missing: " + missing;
        return false;
    }
    GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader, error);
    if (!vs) return false;
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentShader, error);
    if (!fs) return false;
    program_ = gl.glCreateProgram();
    gl.glAttachShader(program_, vs);
    gl.glAttachShader(program_, fs);
    gl.glLinkProgram(program_);
    gl.glDeleteShader(vs);
    gl.glDeleteShader(fs);
    GLint ok = 0;
    gl.glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        gl.glGetProgramInfoLog(program_, sizeof log, nullptr, log);
        error = std::string("shader link: ") + log;
        return false;
    }
    screenLoc_ = gl.glGetUniformLocation(program_, "uScreen");
    gl.glGenVertexArrays(1, &vao_);
    gl.glGenBuffers(1, &vbo_);
    gl.glBindVertexArray(vao_);
    gl.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei stride = sizeof(Vertex);
    gl.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, x)));
    gl.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, u)));
    gl.glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(Vertex, c)));
    gl.glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, lx)));
    gl.glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, radius)));
    for (GLuint a = 0; a < 5; ++a) gl.glEnableVertexAttribArray(a);

    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    gl.glGenTextures(1, &white_);
    gl.glBindTexture(GL_TEXTURE_2D, white_);
    gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    icon_ = loadImage(kGdulIconPng, kGdulIconPngSize);
    backdrop_ = loadImage(kGdulBackdropJpg, kGdulBackdropJpgSize);
    return true;
}

void Renderer::shutdown() {
    if (!program_) return;
    for (auto& [key, a] : atlases_) gl.glDeleteTextures(1, &a->tex);
    atlases_.clear();
    for (Texture* t : {&icon_, &backdrop_})
        if (t->id) gl.glDeleteTextures(1, &t->id), t->id = 0;
    gl.glDeleteTextures(1, &white_);
    gl.glDeleteBuffers(1, &vbo_);
    gl.glDeleteVertexArrays(1, &vao_);
    gl.glDeleteProgram(program_);
    program_ = 0;
}

Texture Renderer::loadImage(const unsigned char* data, size_t size) {
    Texture t;
    int n = 0;
    unsigned char* px = stbi_load_from_memory(data, int(size), &t.w, &t.h, &n, 4);
    if (!px) {
        SDL_Log("gdul: cannot decode a built-in image: %s", stbi_failure_reason());
        return {};
    }
    gl.glGenTextures(1, &t.id);
    gl.glBindTexture(GL_TEXTURE_2D, t.id);
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.w, t.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    gl.glGenerateMipmap(GL_TEXTURE_2D);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(px);
    return t;
}

Renderer::Atlas* Renderer::atlas(FontId f, float px) {
    const int size = std::max(6, int(std::lround(px)));
    auto key = std::make_pair(int(f), size);
    if (auto it = atlases_.find(key); it != atlases_.end()) return it->second.get();
    auto a = std::make_unique<Atlas>();
    const unsigned char* ttf = fontData(f);
    stbtt_fontinfo info;
    stbtt_InitFont(&info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0));
    const float scale = stbtt_ScaleForPixelHeight(&info, float(size));
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    a->ascent = float(asc) * scale;
    a->lineHeight = float(asc - desc + gap) * scale;

    static const int kExtra[] = {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192, 0x2193, 0x25B6};
    const int nExtra = int(sizeof kExtra / sizeof kExtra[0]);
    a->chars.resize(95 + 96 + size_t(nExtra));
    stbtt_pack_range ranges[3] = {};
    ranges[0].font_size = float(size), ranges[0].first_unicode_codepoint_in_range = 32, ranges[0].num_chars = 95;
    ranges[0].chardata_for_range = a->chars.data();
    ranges[1].font_size = float(size), ranges[1].first_unicode_codepoint_in_range = 160, ranges[1].num_chars = 96;
    ranges[1].chardata_for_range = a->chars.data() + 95;
    ranges[2].font_size = float(size), ranges[2].array_of_unicode_codepoints = const_cast<int*>(kExtra);
    ranges[2].num_chars = nExtra, ranges[2].chardata_for_range = a->chars.data() + 95 + 96;
    std::vector<unsigned char> pixels(size_t(Atlas::kSize) * Atlas::kSize);
    stbtt_pack_context pc;
    stbtt_PackBegin(&pc, pixels.data(), Atlas::kSize, Atlas::kSize, 0, 1, nullptr);
    stbtt_PackSetOversampling(&pc, size < 40 ? 2 : 1, 1);
    stbtt_PackFontRanges(&pc, ttf, 0, ranges, 3);
    stbtt_PackEnd(&pc);
    for (int i = 0; i < 95; ++i) a->index[uint32_t(32 + i)] = i;
    for (int i = 0; i < 96; ++i) a->index[uint32_t(160 + i)] = 95 + i;
    for (int i = 0; i < nExtra; ++i) a->index[uint32_t(kExtra[i])] = 95 + 96 + i;

    gl.glGenTextures(1, &a->tex);
    gl.glBindTexture(GL_TEXTURE_2D, a->tex);
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, Atlas::kSize, Atlas::kSize, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    boundTex_ = 0;
    Atlas* raw = a.get();
    atlases_[key] = std::move(a);
    return raw;
}

void Renderer::begin(int pixelW, int pixelH, bool clear) {
    w_ = pixelW, h_ = pixelH;
    gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.glViewport(0, 0, w_, h_);
    gl.glDisable(GL_DEPTH_TEST);
    gl.glDisable(GL_CULL_FACE);
    gl.glDisable(GL_SCISSOR_TEST);
    gl.glEnable(GL_BLEND);
    gl.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (clear) {
        gl.glClearColor(0.035f, 0.03f, 0.04f, 1);
        gl.glClear(GL_COLOR_BUFFER_BIT);
    }
    gl.glUseProgram(program_);
    gl.glUniform2f(screenLoc_, float(w_), float(h_));
    gl.glActiveTexture(GL_TEXTURE0);
    gl.glBindVertexArray(vao_);
    gl.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    boundTex_ = 0;
    useTexture(white_);
}

void Renderer::end() { flush(); }

void Renderer::clip(const Rect* r) {
    flush();
    if (!r) {
        gl.glDisable(GL_SCISSOR_TEST);
        return;
    }
    gl.glEnable(GL_SCISSOR_TEST);
    const int x = int(std::floor(r->x)), y = int(std::floor(r->y));
    gl.glScissor(x, h_ - (y + int(std::ceil(r->h))), std::max(0, int(std::ceil(r->w))), std::max(0, int(std::ceil(r->h))));
}

void Renderer::flush() {
    if (verts_.empty()) return;
    gl.glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts_.size() * sizeof(Vertex)), verts_.data(), GL_STREAM_DRAW);
    gl.glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts_.size()));
    verts_.clear();
}

void Renderer::useTexture(unsigned tex) {
    if (tex == boundTex_) return;
    flush();
    gl.glBindTexture(GL_TEXTURE_2D, tex);
    boundTex_ = tex;
}

void Renderer::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color top, Color bottom,
                    float hx, float hy, float radius, float mode) {
    const auto byte = [](float f) { return uint8_t(std::clamp(f, 0.f, 1.f) * 255.f + 0.5f); };
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    auto v = [&](float x, float y, float u, float vv, Color c) {
        Vertex o{x, y, u, vv, {byte(c.r), byte(c.g), byte(c.b), byte(c.a)}, x - cx, y - cy, hx, hy, radius, mode};
        verts_.push_back(o);
    };
    v(x0, y0, u0, v0, top), v(x1, y0, u1, v0, top), v(x1, y1, u1, v1, bottom);
    v(x0, y0, u0, v0, top), v(x1, y1, u1, v1, bottom), v(x0, y1, u0, v1, bottom);
}

void Renderer::rect(const Rect& r, Color top, Color bottom, float radius) {
    if (r.w <= 0 || r.h <= 0) return;
    useTexture(white_);
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    quad(r.x, r.y, r.x + r.w, r.y + r.h, 0, 0, 0, 0, top, bottom, r.w * 0.5f, r.h * 0.5f, radius, 2);
}

void Renderer::ring(const Rect& r, Color c, float radius, float width) {
    if (r.w <= 0 || r.h <= 0) return;
    useTexture(white_);
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    quad(r.x, r.y, r.x + r.w, r.y + r.h, width, 0, width, 0, c, c, r.w * 0.5f, r.h * 0.5f, radius, 3);
}

void Renderer::glow(const Rect& r, Color c, float radius, float blur) {
    useTexture(white_);
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    quad(r.x - blur, r.y - blur, r.x + r.w + blur, r.y + r.h + blur, 0, blur, 0, blur, c, c, r.w * 0.5f, r.h * 0.5f, radius, 4);
}

void Renderer::image(const Texture& t, const Rect& r, Color tint, bool flipY, float radius) {
    if (!t.id) return;
    useTexture(t.id);
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    quad(r.x, r.y, r.x + r.w, r.y + r.h, 0, flipY ? 1.f : 0.f, 1, flipY ? 0.f : 1.f, tint, tint, r.w * 0.5f, r.h * 0.5f, radius,
         radius > 0 ? 5.f : 0.f);
}

void Renderer::upload(Texture& t, int w, int h, const uint8_t* rgba) {
    if (w <= 0 || h <= 0) return;
    flush();
    if (!t.id) {
        gl.glGenTextures(1, &t.id);
        gl.glBindTexture(GL_TEXTURE_2D, t.id);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        gl.glBindTexture(GL_TEXTURE_2D, t.id);
    }
    gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (t.w != w || t.h != h) {
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        t.w = w, t.h = h;
    } else {
        gl.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    boundTex_ = t.id;
}

void Renderer::release(Texture& t) {
    if (t.id) {
        flush();
        if (boundTex_ == t.id) boundTex_ = 0;
        gl.glDeleteTextures(1, &t.id);
    }
    t = {};
}

bool readBackBuffer(int x, int y, int w, int h, std::vector<uint8_t>& out) {
    if (w <= 0 || h <= 0 || !gl.glReadPixels) return false;
    // Whatever context is current (a game module's): only read state is
    // touched, and put back as it was.
    GLint readFb = 0, readBuf = 0, packAlign = 4, packBuffer = 0;
    gl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFb);
    gl.glGetIntegerv(GL_READ_BUFFER, &readBuf);
    gl.glGetIntegerv(GL_PACK_ALIGNMENT, &packAlign);
    gl.glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
    gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    gl.glReadBuffer(GL_BACK);
    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    out.resize(size_t(w) * size_t(h) * 4);
    gl.glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    gl.glPixelStorei(GL_PACK_ALIGNMENT, packAlign);
    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(packBuffer));
    gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(readFb));
    if (readFb == 0) gl.glReadBuffer(GLenum(readBuf));
    // Opaque: the module's alpha is not meant for blending.
    for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
    return true;
}

float Renderer::lineHeight(FontId f, float px) { return atlas(f, px)->lineHeight; }

float Renderer::text(FontId f, float px, float x, float y, const std::string& s, Color c, float tracking) {
    Atlas* a = atlas(f, px);
    useTexture(a->tex);
    float pen = std::round(x), base = std::round(y + a->ascent);
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = nextCodepoint(s, i);
        auto it = a->index.find(cp);
        if (it == a->index.end()) it = a->index.find('?');
        stbtt_aligned_quad q;
        float yy = base;
        stbtt_GetPackedQuad(a->chars.data(), Atlas::kSize, Atlas::kSize, it->second, &pen, &yy, &q, 0);
        if (cp != ' ') quad(q.x0, q.y0, q.x1, q.y1, q.s0, q.t0, q.s1, q.t1, c, c, 0, 0, 0, 1);
        pen += tracking;
    }
    return pen - x;
}

float Renderer::measure(FontId f, float px, const std::string& s, float tracking) {
    Atlas* a = atlas(f, px);
    float w = 0;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = nextCodepoint(s, i);
        auto it = a->index.find(cp);
        if (it == a->index.end()) it = a->index.find('?');
        w += a->chars[size_t(it->second)].xadvance + tracking;
    }
    return w;
}

std::string Renderer::elide(FontId f, float px, const std::string& s, float maxW) {
    if (measure(f, px, s) <= maxW) return s;
    const std::string dots = "\xE2\x80\xA6";  // …
    std::string cut = s;
    while (!cut.empty()) {
        size_t back = cut.size() - 1;
        while (back > 0 && (static_cast<unsigned char>(cut[back]) & 0xC0) == 0x80) --back;
        cut.erase(back);
        if (measure(f, px, cut + dots) <= maxW) break;
    }
    return cut + dots;
}

std::vector<std::string> Renderer::wrap(FontId f, float px, const std::string& s, float maxW) {
    std::vector<std::string> lines;
    std::string line, word;
    auto push = [&](bool hard) {
        // A word wider than the line (a long folder path) is cut where it must.
        while (!word.empty() && measure(f, px, word) > maxW) {
            size_t cut = word.size();
            while (cut > 1 && measure(f, px, word.substr(0, cut)) > maxW) {
                do --cut;
                while (cut > 1 && (static_cast<unsigned char>(word[cut]) & 0xC0) == 0x80);
            }
            if (!line.empty()) lines.push_back(line), line.clear();
            lines.push_back(word.substr(0, cut));
            word.erase(0, cut);
        }
        if (!word.empty()) {
            std::string attempt = line.empty() ? word : line + " " + word;
            if (!line.empty() && measure(f, px, attempt) > maxW) {
                lines.push_back(line);
                line = word;
            } else {
                line = attempt;
            }
            word.clear();
        }
        if (hard) lines.push_back(line), line.clear();
    };
    for (char ch : s) {
        if (ch == ' ') push(false);
        else if (ch == '\n') push(true);
        else word += ch;
    }
    push(false);
    if (!line.empty()) lines.push_back(line);
    return lines;
}

std::vector<uint8_t> Renderer::readPixels(int w, int h) {
    flush();
    std::vector<uint8_t> rows(size_t(w) * size_t(h) * 4);
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
    std::vector<uint8_t> flipped(rows.size());
    for (int y = 0; y < h; ++y)
        std::memcpy(&flipped[size_t(y) * size_t(w) * 4], &rows[size_t(h - 1 - y) * size_t(w) * 4], size_t(w) * 4);
    return flipped;
}

}  // namespace gfx
