// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>

#include "render/device.h"
#include "render/material.h"
#include "render/post_fx.h"
#include "render/standard_shader.h"

// GD02's renderer, built from GH2's own rendering: a render device (OpenGL
// now, swappable for Vulkan), GH2's standard material shader and material
// rules, and the 16:9 frame (1280x720 logical, letterboxed in the window).
namespace gfx {

class Renderer {
public:
    ~Renderer();
    // `window` must have been created with prepareWindow()'s flags.
    bool init(SDL_Window* window, std::string& error);
    void shutdown();
    bool ready() const { return device_ != nullptr; }

    RenderDevice& device() { return *device_; }
    StandardShaders& shaders() { return *shaders_; }
    MaterialRenderer& materials() { return *materials_; }
    // GH2's post-process of the world (render/post_fx.h).
    PostFx& post() { return *post_; }

    // Starts a frame: black bars around the 16:9 picture, which is cleared
    // to `clear` (rgba 0..1) with its depth; the picture's viewport is bound.
    void beginFrame(const float clear[4]);
    // Presents (frame cap, at most two frames in flight).
    void endFrame();
    // The 16:9 picture in window pixels (origin top-left).
    void pictureRect(int& x, int& y, int& w, int& h) const;
    // The picture as an RGBA32 surface (screenshots; synchronous).
    SDL_Surface* readPixels();

    // Compiles every program the renderer can build on this driver; returns
    // the names of those that failed (the compiler's messages are logged).
    std::vector<std::string> compileAll(int& programs);

    // The 2D program (canvas.cpp): GdCanvas block, sampler sTex in slot 0.
    ProgramId canvasProgram() const { return canvasProgram_; }
    static ProgramDesc canvasProgramDesc();
    // The video program (video/wmv_player.cpp): the canvas's vertices and
    // GdCanvas block; samplers sY, sU, sV (slots 0-2) are a frame's YUV 4:2:0
    // planes, converted to RGB here (BT.601, limited range).
    static ProgramDesc videoProgramDesc();

private:
    SDL_Window* window_ = nullptr;
    std::unique_ptr<RenderDevice> device_;
    std::unique_ptr<StandardShaders> shaders_;
    std::unique_ptr<MaterialRenderer> materials_;
    std::unique_ptr<PostFx> post_;
    ProgramId canvasProgram_;
};

}  // namespace gfx
