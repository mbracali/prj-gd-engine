// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// gdcore_shaderc: the build-time shader compiler of gd-engine-core
// (docs/render.md section 3). A HOST program: it runs on the build machine
// for every package (SPIR-V is the same for every platform) and is never
// shipped.
//
//   gdcore_shaderc --out <header.h> [--namespace <ns>] [--define NAME[=VALUE]]...
//                  [--quiet] <source.cpp>...
//
// It reads the gd shader dialect where the games keep it, as C++ raw string
// literals named k<Name>Vs (a vertex shader) and k<Name>Fs (a fragment
// shader):
//
//   const char* kMainVs = R"(
//   GD_ATTR(0) vec3 aPos;  ...
//   )";
//
// compiles each one to SPIR-V 1.0 for Vulkan 1.0 with glslang (the dialect's
// Vulkan macros: section 3.2), validates it with SPIRV-Tools (when the tool
// was built with it) and with the dialect's binding rules
// (src/common/spirv_reflect.cpp), and writes <header.h>:
//
//   inline constexpr uint32_t kMainVsSpirv[] = {0x07230203u, ...};
//
// A game then sets ShaderDesc::spirv(kMainVsSpirv, kMainFsSpirv). Errors are
// reported at the line of the .cpp file, and fail the build.

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#if GDCORE_SHADERC_VALIDATE
#include <spirv-tools/libspirv.hpp>
#endif

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "common/spirv_reflect.h"

namespace {

// The dialect's Vulkan GLSL 450 form (docs/render.md section 3.2). SDL_GPU's
// SPIR-V resource sets: vertex textures 0, vertex uniforms 1, fragment
// textures 2, fragment uniforms 3; the Vulkan backend uses the same layout,
// so one blob serves both. Clip space is GL's (y up) as SDL_GPU's NDC is;
// only z is remapped to 0..1. The Vulkan backend gets y up from a
// negative-height viewport, as SDL_GPU's own Vulkan driver does.
const char* const kPrelude =
    "#version 450\n"
    "#define GD_SPIRV 1\n"
    "#define GD_UNIFORMS(slot, name) layout(std140, set = GD_UNIFORM_SET, binding = slot) uniform name\n"
    "#define GD_SAMPLER2D(slot, name) layout(set = GD_TEXTURE_SET, binding = slot) uniform sampler2D name\n"
    "#define GD_SAMPLER2DSHADOW(slot, name) layout(set = GD_TEXTURE_SET, binding = slot) uniform sampler2DShadow name\n"
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

struct Source {
    std::string file;   // where it was found
    std::string name;   // kMainVs
    bool vertex = true;
    int line = 1;       // the .cpp line of the literal's first line
    std::string body;
};

bool isIdent(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

bool endsWith(const std::string& s, const char* tail) {
    const size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

// Every `k<Name>Vs = R"delim(...)delim"` and `k<Name>Fs = ...` of a C++ file.
bool extract(const std::string& file, std::vector<Source>& out, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = file + ": cannot read it";
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    for (size_t at = text.find("R\""); at != std::string::npos; at = text.find("R\"", at + 2)) {
        if (at > 0 && isIdent(text[at - 1])) continue;  // u8R"..." and friends: not ours
        // Backwards: `= `, an optional `[]`, the name.
        size_t p = at;
        while (p > 0 && (text[p - 1] == ' ' || text[p - 1] == '\t' || text[p - 1] == '\n' || text[p - 1] == '\r')) --p;
        if (p == 0 || text[p - 1] != '=') continue;
        --p;
        while (p > 0 && (text[p - 1] == ' ' || text[p - 1] == '\t')) --p;
        if (p >= 2 && text[p - 1] == ']' && text[p - 2] == '[') p -= 2;
        const size_t nameEnd = p;
        while (p > 0 && isIdent(text[p - 1])) --p;
        const std::string name = text.substr(p, nameEnd - p);
        const bool vs = endsWith(name, "Vs"), fs = endsWith(name, "Fs");
        if (name.size() < 4 || name[0] != 'k' || (!vs && !fs)) continue;
        const size_t open = text.find('(', at + 2);
        if (open == std::string::npos || open - (at + 2) > 16) continue;
        const std::string close = ")" + text.substr(at + 2, open - (at + 2)) + "\"";
        const size_t end = text.find(close, open + 1);
        if (end == std::string::npos) {
            error = file + ": " + name + ": the raw string does not end";
            return false;
        }
        Source s;
        s.file = file, s.name = name, s.vertex = vs;
        s.line = 1;
        for (size_t i = 0; i < open; ++i) s.line += text[i] == '\n';
        s.body = text.substr(open + 1, end - open - 1);
        out.push_back(std::move(s));
        at = end;
    }
    return true;
}

// glslang's "ERROR: 0:12: ..." as "<file>:12: ..." (the #line of compile()).
std::string withFile(const std::string& log, const std::string& file) {
    std::string out;
    std::istringstream lines(log);
    for (std::string l; std::getline(lines, l);) {
        if (l.empty()) continue;
        for (const char* tag : {"ERROR: 0:", "WARNING: 0:"}) {
            const size_t at = l.find(tag);
            if (at == 0) {
                l = file + ":" + l.substr(std::strlen(tag)) + (tag[0] == 'E' ? "" : " (warning)");
                break;
            }
        }
        out += l + "\n";
    }
    return out;
}

bool compile(const Source& s, const std::string& defines, std::vector<uint32_t>& spirv, std::string& error) {
    const EShLanguage stage = s.vertex ? EShLangVertex : EShLangFragment;
    const std::string text = std::string(kPrelude) + defines + (s.vertex ? kVertexPrelude : kFragmentPrelude) + "#line " +
                             std::to_string(s.line) + "\n" + s.body;
    glslang::TShader shader(stage);
    const char* strings[] = {text.c_str()};
    shader.setStrings(strings, 1);
    shader.setEntryPoint("main");
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    const EShMessages msgs = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, false, msgs)) {
        error = withFile(shader.getInfoLog(), s.file) + s.file + ":" + std::to_string(s.line) + ": " + s.name + ": does not compile";
        return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(msgs)) {
        error = withFile(program.getInfoLog(), s.file) + s.file + ":" + std::to_string(s.line) + ": " + s.name + ": does not link";
        return false;
    }
    glslang::SpvOptions options;
    options.generateDebugInfo = false;
    options.disableOptimizer = true;  // the drivers optimise; the SPIR-V stays the source's maths
    options.optimizeSize = false;
    options.validate = false;  // SPIRV-Tools below
    spv::SpvBuildLogger logger;
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv, &logger, &options);
    const std::string msg = logger.getAllMessages();
    if (spirv.empty()) {
        error = s.file + ":" + std::to_string(s.line) + ": " + s.name + ": no SPIR-V generated: " + msg;
        return false;
    }
    return true;
}

bool validate(const Source& s, const std::vector<uint32_t>& spirv, gd::core::render::SpirvStageInfo& info, std::string& error) {
#if GDCORE_SHADERC_VALIDATE
    spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_0);
    std::string why;
    tools.SetMessageConsumer([&](spv_message_level_t, const char*, const spv_position_t& pos, const char* m) {
        why += "word " + std::to_string(pos.index) + ": " + m + "\n";
    });
    if (!tools.Validate(spirv)) {
        error = s.file + ":" + std::to_string(s.line) + ": " + s.name + ": invalid SPIR-V (spirv-val, Vulkan 1.0):\n" + why;
        return false;
    }
#endif
    std::string why2;
    if (!gd::core::render::reflectSpirv(spirv.data(), spirv.size() * 4, info, why2)) {
        error = s.file + ":" + std::to_string(s.line) + ": " + s.name + ": " + why2;
        return false;
    }
    if ((info.stage == gd::core::render::SpirvStage::Vertex) != s.vertex) {
        error = s.file + ":" + std::to_string(s.line) + ": " + s.name + ": wrong stage";
        return false;
    }
    return true;
}

std::string maskText(uint32_t m) {
    std::string t;
    for (int b = 0; b < 32; ++b)
        if (m & (1u << b)) t += (t.empty() ? "" : ",") + std::to_string(b);
    return t.empty() ? "-" : t;
}

int usage() {
    std::fprintf(stderr, "usage: gdcore_shaderc --out <header.h> [--namespace <ns>] [--define NAME[=VALUE]]... [--quiet] <source.cpp>...\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::string out, ns, defines;
    std::vector<std::string> inputs;
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--namespace" && i + 1 < argc) ns = argv[++i];
        else if (a == "--define" && i + 1 < argc) {
            std::string d = argv[++i];
            const size_t eq = d.find('=');
            defines += "#define " + (eq == std::string::npos ? d + " 1" : d.substr(0, eq) + " " + d.substr(eq + 1)) + "\n";
        } else if (a == "--quiet") quiet = true;
        else if (!a.empty() && a[0] == '-') return usage();
        else inputs.push_back(a);
    }
    if (out.empty() || inputs.empty()) return usage();

    std::vector<Source> sources;
    std::string error;
    for (const std::string& f : inputs)
        if (!extract(f, sources, error)) {
            std::fprintf(stderr, "gdcore_shaderc: %s\n", error.c_str());
            return 1;
        }
    if (sources.empty()) {
        std::fprintf(stderr, "gdcore_shaderc: no k<Name>Vs / k<Name>Fs raw strings in the sources\n");
        return 1;
    }

    glslang::InitializeProcess();
    std::ostringstream h;
    h << "// Generated by gdcore_shaderc (gd-engine-core/render/tools/shaderc) from\n";
    for (const std::string& f : inputs) h << "//   " << f.substr(f.find_last_of("/\\") + 1) << "\n";
    h << "// SPIR-V 1.0 for Vulkan 1.0 / SDL_GPU, gd shader dialect (gd-engine-core/docs/render.md section 3). Do not edit.\n"
      << "#pragma once\n#include <cstdint>\n\n";
    if (!ns.empty()) h << "namespace " << ns << " {\n\n";
    int failed = 0;
    for (const Source& s : sources) {
        std::vector<uint32_t> spirv;
        gd::core::render::SpirvStageInfo info;
        if (!compile(s, defines, spirv, error) || !validate(s, spirv, info, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            ++failed;
            continue;
        }
        h << "// " << s.name << " (" << (s.vertex ? "vertex" : "fragment") << ", " << s.file.substr(s.file.find_last_of("/\\") + 1)
          << ":" << s.line << "): samplers " << maskText(info.samplerMask) << ", uniform blocks " << maskText(info.uniformMask)
          << ", inputs " << maskText(info.inputMask) << ", outputs " << maskText(info.outputMask) << "\n";
        h << "inline constexpr uint32_t " << s.name << "Spirv[] = {";
        char word[16];
        for (size_t i = 0; i < spirv.size(); ++i) {
            std::snprintf(word, sizeof word, "0x%08xu", spirv[i]);
            h << (i % 8 == 0 ? "\n    " : " ") << word << (i + 1 < spirv.size() ? "," : "");
        }
        h << "\n};\n\n";
        if (!quiet)
            std::printf("gdcore_shaderc: %s %s: %zu words, samplers %s, uniform blocks %s\n", s.name.c_str(),
                        s.vertex ? "vertex" : "fragment", spirv.size(), maskText(info.samplerMask).c_str(),
                        maskText(info.uniformMask).c_str());
    }
    glslang::FinalizeProcess();
    if (failed) {
        std::fprintf(stderr, "gdcore_shaderc: %d shader(s) failed\n", failed);
        return 1;
    }
    if (!ns.empty()) h << "}  // namespace " << ns << "\n";
    std::ofstream f(out, std::ios::binary);
    f << h.str();
    if (!f) {
        std::fprintf(stderr, "gdcore_shaderc: cannot write %s\n", out.c_str());
        return 1;
    }
    return 0;
}
