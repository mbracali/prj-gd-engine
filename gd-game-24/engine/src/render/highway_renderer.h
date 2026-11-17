// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Draws the PSP models of data/highway_assets on gd-engine-core's render
// device: GE triangle strips / fans / lists turned into triangle lists, the
// textures (repeat wrap, linear), the texture modulated by the vertex colour
// and a tint, unlit (the PBT_ textures carry their shading [I]); materials
// alpha-blended with depth writes, additive ones (material byte 1 = 3)
// added without depth writes. Into a colour + depth target, with our 2D canvas on
// top (premultiplied alpha), then shown by the canvas (Canvas::present3d).
#pragma once

#include <string>
#include <vector>

#include "data/highway_assets.h"
#include "gd/core/render/device.h"

namespace gd24 {

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column-major
    static Mat4 translate(float x, float y, float z);
    static Mat4 scale(float x, float y, float z);
    static Mat4 perspective(float fovYDeg, float aspect, float zNear, float zFar);
    static Mat4 lookAt(const float eye[3], const float at[3], const float up[3]);
    Mat4 operator*(const Mat4& o) const;
};

class HighwayRenderer {
public:
    static constexpr int kW = 1920, kH = 1088;  // the 3D target (4 x the PSP's 480 x 272)

    bool init(gd::core::render::Device& dev, const HighwayAssets& assets, std::string& error);
    void shutdown();
    bool ready() const { return dev_ != nullptr; }

    // One frame: begin (clears to `clear`), draws, end (the overlay on top);
    // returns the picture to present.
    void begin(const Mat4& viewProj, const float clear[4]);
    struct Draw {
        Mat4 model;
        float tint[4] = {1, 1, 1, 1};
        int textureOverride = -1;   // a texture index for every sub-mesh
        bool overdrive = false;     // also the Overdrive layers
        bool blendedPass = false;   // false: the alpha-blended sub-meshes, true: the additive ones
        float uvOffset[2] = {0, 0};
        std::string onlySub;        // only the sub-mesh with this material name
    };
    void draw(const std::string& model, const Draw& d);
    gd::core::render::Texture end(gd::core::render::Texture overlay);

private:
    struct Sub {
        gd::core::render::Buffer vb;
        uint32_t count = 0;
        int texture = -1;
        bool blended = false, overdrive = false;
        std::string material;
    };
    struct Model {
        std::string name;
        std::vector<Sub> subs;
    };
    gd::core::render::Device* dev_ = nullptr;
    std::vector<gd::core::render::Texture> textures_;
    std::vector<Model> models_;
    gd::core::render::Texture white_{}, color_{}, depth_{};
    gd::core::render::Sampler sampler_{}, clampSampler_{};
    gd::core::render::Shader shader_{}, quadShader_{};
    gd::core::render::Pipeline opaque_{}, blended_{}, overlay_{};
    Mat4 viewProj_;
};

}  // namespace gd24
