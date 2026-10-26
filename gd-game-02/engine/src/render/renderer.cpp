// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/renderer.h"

#include <algorithm>
#include <cstdlib>

#include "render/camera.h"

namespace gfx {
namespace {

const char* kCanvasVertex = R"(
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aUv;
layout(std140) uniform GdCanvas {
    mat4 uProj;
};
out vec4 vColor;
out vec2 vUv;
void main() {
    vColor = aColor;
    vUv = aUv;
    gl_Position = uProj * vec4(aPos, 1.0);
}
)";

const char* kCanvasFragment = R"(
in vec4 vColor;
in vec2 vUv;
uniform sampler2D sTex;
out vec4 fragColor;
void main() {
    fragColor = texture(sTex, vUv) * vColor;
}
)";

}  // namespace

Renderer::~Renderer() { shutdown(); }

ProgramDesc Renderer::canvasProgramDesc() {
    ProgramDesc d;
    d.name = "canvas";
    d.vertex = kCanvasVertex;
    d.fragment = kCanvasFragment;
    d.uniformBlocks = {"GdCanvas"};
    d.samplers = {"sTex"};
    return d;
}

bool Renderer::init(SDL_Window* window, std::string& error) {
    window_ = window;
    device_ = createGlDevice(window, error);
    if (!device_) return false;
    canvasProgram_ = device_->createProgram(canvasProgramDesc(), error);
    if (!canvasProgram_) {
        error = "the 2D shader does not compile: " + error;
        device_.reset();
        return false;
    }
    shaders_ = std::make_unique<StandardShaders>(*device_);
    materials_ = std::make_unique<MaterialRenderer>(*device_, *shaders_);
    // The plainest material variant is every other one's last fallback.
    if (!shaders_->program(0)) SDL_Log("render: the standard shader does not compile here; 3D scenes will not draw");
    if (const char* fps = SDL_getenv("GD02_FPS")) device_->setFrameCap(std::atoi(fps));  // development aid
    return true;
}

void Renderer::shutdown() {
    if (!device_) return;
    materials_.reset();
    shaders_.reset();
    if (canvasProgram_) device_->destroyProgram(canvasProgram_);
    canvasProgram_ = {};
    device_.reset();
}

void Renderer::pictureRect(int& x, int& y, int& w, int& h) const {
    int ww = 0, wh = 0;
    device_->drawableSize(ww, wh);
    const float scale = std::min(ww / kLogicalW, wh / kLogicalH);
    w = std::max(1, int(kLogicalW * scale));
    h = std::max(1, int(kLogicalH * scale));
    x = (ww - w) / 2;
    y = (wh - h) / 2;
}

void Renderer::beginFrame(const float clear[4]) {
    device_->beginFrame();
    const float black[4] = {0, 0, 0, 1};
    device_->clear(black, true);
    int x, y, w, h;
    pictureRect(x, y, w, h);
    device_->setViewport(x, y, w, h);
    device_->clear(clear, true);
}

void Renderer::endFrame() { device_->endFrame(); }

SDL_Surface* Renderer::readPixels() {
    int x, y, w, h;
    pictureRect(x, y, w, h);
    return device_->readPixels(x, y, w, h);
}

std::vector<std::string> Renderer::compileAll(int& programs) {
    std::vector<std::string> failed;
    programs = 0;
    std::string error;
    // The 2D program.
    ++programs;
    if (ProgramId p = device_->createProgram(canvasProgramDesc(), error)) device_->destroyProgram(p);
    else SDL_Log("shader canvas: %s", error.c_str()), failed.push_back("canvas");
    // Every standard permutation.
    for (uint32_t key : StandardShaders::testKeys()) {
        ++programs;
        error.clear();
        if (!shaders_->compile(key, error)) {
            char name[64];
            SDL_snprintf(name, sizeof name, "standard_%x", key);
            SDL_Log("shader %s (%s): %s", name, StandardShaders::name(key).c_str(), error.c_str());
            failed.push_back(std::string(name) + " (" + StandardShaders::name(key) + ")");
        }
    }
    return failed;
}

}  // namespace gfx
