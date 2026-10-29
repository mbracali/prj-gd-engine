// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>

#include "render/camera.h"
#include "render/device.h"
#include "render/material.h"
#include "render/post_fx.h"
#include "render/standard_shader.h"

// GD03's renderer, built from GH2's own rendering: a render device (OpenGL
// now, swappable for Vulkan), GH2's standard material shader and material
// rules, and the frame: 16:9 (1280x720 logical, letterboxed in the window),
// with the game's picture in it 4:3 pillarboxed (the PS2's own screen, the
// default) or the whole 16:9 frame (Enhanced Options > Widescreen).
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

    // Starts a frame: black bars around the picture, which is cleared to
    // `clear` (rgba 0..1) with its depth; the picture's viewport is bound.
    void beginFrame(const float clear[4]);
    // Presents (frame cap, at most two frames in flight).
    void endFrame();
    // The 16:9 frame in window pixels (origin top-left): what the window
    // shows of the game, the logical 1280x720 screen.
    void frameRect(int& x, int& y, int& w, int& h) const;
    // The game's picture in window pixels (origin top-left): the centred
    // 4:3 part of the frame, or the whole frame when widescreen (or while
    // fullFrame is on).
    void pictureRect(int& x, int& y, int& w, int& h) const;
    // The picture's width over its height (pictureAspect() in camera.h).
    float aspect() const { return wide() ? kLogicalW / kLogicalH : 4.0f / 3.0f; }
    // 16:9 (true) or 4:3 pillarboxed (false, the PS2's picture). Set by
    // the Enhanced Options; the viewers without the menus keep 16:9.
    void setWidescreen(bool on);
    bool widescreen() const { return wide_; }
    // The port's own screens (Enhanced Options, GAMEPADS) are laid out on
    // the whole 16:9 frame in either mode: while on, the picture is the
    // frame and its viewport is bound. Returns what it was. beginFrame
    // turns it off.
    bool setFullFrame(bool on);
    // The frame as an RGBA32 surface, bars included (screenshots; synchronous).
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
    bool wide() const { return wide_ || fullFrame_; }
    void publishAspect() const;
    bool wide_ = true, fullFrame_ = false;
    SDL_Window* window_ = nullptr;
    std::unique_ptr<RenderDevice> device_;
    std::unique_ptr<StandardShaders> shaders_;
    std::unique_ptr<MaterialRenderer> materials_;
    std::unique_ptr<PostFx> post_;
    ProgramId canvasProgram_;
};

}  // namespace gfx
