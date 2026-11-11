// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd05's GLSL to SPIR-V at run time (render/spirv_compile.h), with glslang
// (the version gd-engine-core pins for its build-time gdcore_shaderc).
//
// gd05's shaders are GLSL 330 / 300 es with std140 blocks and samplers bound
// by name (render/device.h). For SDL_GPU / Vulkan each stage is preprocessed
// with the program's #defines, then given the resource layout SDL_GPU wants
// (gd-engine-core/docs/render.md 3.2): `layout(std140) uniform Name` gets
// set 1 / 3 and the binding of its slot in ProgramDesc::uniformBlocks,
// `uniform samplerX name;` set 0 / 2 and its slot in ProgramDesc::samplers;
// varyings get locations by name (the vertex stage's outputs in order),
// fragment outputs location 0.. in order. Then compiled exactly as
// gdcore_shaderc compiles the core's dialect (SPIR-V 1.0, Vulkan 1.0, not
// optimised).
#include "render/spirv_compile.h"

#if GD05_RUNTIME_SPIRV
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <SDL3/SDL.h>

#include <map>
#include <mutex>
#include <regex>
#endif

namespace gfx {

#if !GD05_RUNTIME_SPIRV
bool spirvCompilerAvailable() { return false; }
bool compileSpirv(const std::string&, const std::string&, const std::string&, const std::string&, const std::vector<std::string>&,
                  const std::vector<std::string>&, bool, std::vector<uint32_t>&, std::vector<uint32_t>&, std::string& error) {
    error = "this build has no SPIR-V compiler (GD05_RUNTIME_SPIRV)";
    return false;
}
#else

namespace {

// gdcore_shaderc's prelude (gd-engine-core/render/tools/shaderc): the
// dialect's macros for the clear quad, gd_clip for every vertex stage.
const char* const kPrelude =
    "#version 450\n"
    "#define GD_SPIRV 1\n"
    "#define GD_UNIFORMS(slot, name) layout(std140, set = GD_UNIFORM_SET, binding = slot) uniform name\n"
    "#define GD_SAMPLER2D(slot, name) layout(set = GD_TEXTURE_SET, binding = slot) uniform sampler2D name\n"
    "#define GD_SAMPLERCUBE(slot, name) layout(set = GD_TEXTURE_SET, binding = slot) uniform samplerCube name\n"
    "#define GD_ATTR(loc) layout(location = loc) in\n"
    "#define GD_IN(loc) layout(location = loc) in\n"
    "#define GD_OUT(loc) layout(location = loc) out\n"
    "#define GD_FRAG_OUT(loc) layout(location = loc) out\n"
    "#define GD_VERTEX_ID gl_VertexIndex\n"
    "#define GD_INSTANCE_ID gl_InstanceIndex\n";
const char* const kVertexPrelude =
    "#define GD_VERTEX 1\n"
    "#define GD_TEXTURE_SET 0\n"
    "#define GD_UNIFORM_SET 1\n"
    "vec4 gd_clip(vec4 p) { return vec4(p.x, p.y, 0.5 * (p.z + p.w), p.w); }\n";
const char* const kFragmentPrelude =
    "#define GD_FRAGMENT 1\n"
    "#define GD_TEXTURE_SET 2\n"
    "#define GD_UNIFORM_SET 3\n";

std::once_flag g_init;

const EShMessages kMsgs = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);

void setEnv(glslang::TShader& s, EShLanguage stage) {
    s.setEntryPoint("main");
    s.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    s.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    s.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
}

bool preprocess(EShLanguage stage, const std::string& text, std::string& out, std::string& error) {
    glslang::TShader s(stage);
    const char* strings[] = {text.c_str()};
    s.setStrings(strings, 1);
    setEnv(s, stage);
    glslang::TShader::ForbidIncluder includer;
    if (!s.preprocess(GetDefaultResources(), 450, ENoProfile, false, false, kMsgs, &out, includer)) {
        error = std::string(stage == EShLangVertex ? "vertex" : "fragment") + " shader: " + s.getInfoLog();
        return false;
    }
    return true;
}

int slotOf(const std::vector<std::string>& names, const std::string& name) {
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return int(i);
    return -1;
}

int locationsOf(const std::string& type, const std::string& array) {
    int n = type == "mat2" ? 2 : type == "mat3" ? 3 : type == "mat4" ? 4 : 1;
    if (!array.empty()) n *= std::max(1, std::atoi(array.c_str() + 1));
    return n;
}

// The resource layout and the locations, on a preprocessed stage.
bool layout(bool vertex, std::string& text, const std::vector<std::string>& blocks, const std::vector<std::string>& samplers,
            std::map<std::string, int>& varyings, std::string& error) {
    const char* texSet = vertex ? "0" : "2";
    const char* uboSet = vertex ? "1" : "3";
    std::string out;
    std::smatch m;
    // Uniform blocks.
    static const std::regex block(R"(layout\s*\(\s*std140\s*\)\s*uniform\s+(\w+))");
    std::string rest = text;
    while (std::regex_search(rest, m, block)) {
        const int slot = slotOf(blocks, m[1].str());
        if (slot < 0) return error = "uniform block " + m[1].str() + " has no binding in the program", false;
        out += m.prefix().str() + "layout(std140, set = " + uboSet + ", binding = " + std::to_string(slot) + ") uniform " + m[1].str();
        rest = m.suffix().str();
    }
    text = out + rest;
    // Samplers.
    static const std::regex sampler(R"((^|[;\n}])(\s*)uniform\s+(sampler2D|samplerCube|sampler2DShadow|sampler3D)\s+(\w+)\s*;)");
    out.clear(), rest = text;
    while (std::regex_search(rest, m, sampler)) {
        const int slot = slotOf(samplers, m[4].str());
        if (slot < 0) return error = "sampler " + m[4].str() + " has no slot in the program", false;
        out += m.prefix().str() + m[1].str() + m[2].str() + "layout(set = " + texSet + ", binding = " + std::to_string(slot) + ") uniform " +
               m[3].str() + " " + m[4].str() + ";";
        rest = m.suffix().str();
    }
    text = out + rest;
    // Varyings (and fragment outputs): global `in` / `out` declarations
    // without a layout of their own.
    static const std::regex io(R"((^|\n)([ \t]*)((?:flat|smooth|noperspective)\s+)?(in|out)\s+(\w+)\s+(\w+)\s*(\[\s*\d+\s*\])?\s*;)");
    out.clear(), rest = text;
    int next = 0, fragOut = 0;
    while (std::regex_search(rest, m, io)) {
        const std::string dir = m[4].str(), type = m[5].str(), name = m[6].str(), array = m[7].str();
        int loc = -1;
        if (vertex && dir == "out") {
            loc = next, next += locationsOf(type, array);
            varyings[name] = loc;
        } else if (!vertex && dir == "in") {
            auto it = varyings.find(name);
            if (it == varyings.end()) return error = "fragment input " + name + " is not a vertex output", false;
            loc = it->second;
        } else if (!vertex && dir == "out") {
            loc = fragOut, fragOut += locationsOf(type, array);
        }
        out += m.prefix().str() + m[1].str() + m[2].str();
        if (loc >= 0) out += "layout(location = " + std::to_string(loc) + ") ";
        out += m[3].str() + dir + " " + type + " " + name + array + ";";
        rest = m.suffix().str();
    }
    text = out + rest;
    return true;
}

}  // namespace

bool spirvCompilerAvailable() { return true; }

bool compileSpirv(const std::string& name, const std::string& vertex, const std::string& fragment, const std::string& defines,
                  const std::vector<std::string>& blocks, const std::vector<std::string>& samplers, bool dialect,
                  std::vector<uint32_t>& vertexOut, std::vector<uint32_t>& fragmentOut, std::string& error) {
    std::call_once(g_init, [] { glslang::InitializeProcess(); });
    (void)dialect;  // the dialect's macros are in the prelude; gd05's GLSL gets its layout below
    std::string vs, fs;
    if (!preprocess(EShLangVertex, std::string(kPrelude) + kVertexPrelude + defines + "#line 1\n" + vertex, vs, error) ||
        !preprocess(EShLangFragment, std::string(kPrelude) + kFragmentPrelude + defines + "#line 1\n" + fragment, fs, error)) {
        error = name + ": " + error;
        return false;
    }
    std::map<std::string, int> varyings;
    if (!layout(true, vs, blocks, samplers, varyings, error) || !layout(false, fs, blocks, samplers, varyings, error)) {
        error = name + ": " + error;
        return false;
    }
    glslang::TShader v(EShLangVertex), f(EShLangFragment);
    const char* vsText[] = {vs.c_str()};
    const char* fsText[] = {fs.c_str()};
    v.setStrings(vsText, 1), f.setStrings(fsText, 1);
    setEnv(v, EShLangVertex), setEnv(f, EShLangFragment);
    if (!v.parse(GetDefaultResources(), 450, false, kMsgs)) return error = name + ": vertex shader: " + v.getInfoLog(), false;
    if (!f.parse(GetDefaultResources(), 450, false, kMsgs)) return error = name + ": fragment shader: " + f.getInfoLog(), false;
    glslang::TProgram program;
    program.addShader(&v);
    program.addShader(&f);
    if (!program.link(kMsgs)) return error = name + ": link: " + program.getInfoLog(), false;
    glslang::SpvOptions options;
    options.generateDebugInfo = false;
    options.disableOptimizer = true;  // as gdcore_shaderc: the drivers optimise
    options.optimizeSize = false;
    options.validate = false;
    spv::SpvBuildLogger logger;
    vertexOut.clear(), fragmentOut.clear();
    glslang::GlslangToSpv(*program.getIntermediate(EShLangVertex), vertexOut, &logger, &options);
    glslang::GlslangToSpv(*program.getIntermediate(EShLangFragment), fragmentOut, &logger, &options);
    if (vertexOut.empty() || fragmentOut.empty()) return error = name + ": no SPIR-V generated: " + logger.getAllMessages(), false;
    return true;
}

#endif

}  // namespace gfx
