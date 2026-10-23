#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Generate the FidelityFX FSR 3.1 upscaler shader permutation headers for the
Vulkan backend, as FidelityFX_SC (the SDK's Windows-only shader compiler)
would with -compiler=glslang -reflection.

The port drives FSR 3.1 with fixed context flags (LDR colour, render-resolution
motion vectors, no jitter cancellation, standard depth) and reports wave32
(engine/third_party/fidelityfx/src/backends/vk/ffx_vk.cpp), so each pass needs
only the sharpening option's two values; every other key maps to those. FP16
is the device's: the 16-bit files are real FFX_HALF=1 builds (wave64 ones
alias the wave32 builds).

Usage: ffx_gen_shaders.py <glslangValidator> <engine/third_party/fidelityfx>
Writes <fidelityfx>/generated/*.h (checked in; the build does not need glslang).
"""
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

OPTIONS = [  # (define, fixed value or None = permuted), in the SDK's key order
    ("FFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE", 0),
    ("FFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT", 0),
    ("FFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS", 1),
    ("FFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS", 0),
    ("FFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH", 0),
    ("FFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING", None),
]
BASE = ["-DFFX_GPU=1",
        "-DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0",
        "-DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0",
        "-DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1",
        "-DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0",
        "-DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2",
        "-DFFX_GLSL=1"]

# ---------------------------------------------------------------- SPIR-V reflection
OP_NAME, OP_DECORATE, OP_MEMBER_DECORATE = 5, 71, 72
OP_TYPE_IMAGE, OP_TYPE_SAMPLER, OP_TYPE_SAMPLED_IMAGE = 25, 26, 27
OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY, OP_TYPE_STRUCT, OP_TYPE_POINTER = 28, 29, 30, 32
OP_CONSTANT, OP_VARIABLE = 43, 59
DEC_BLOCK, DEC_BUFFER_BLOCK, DEC_NON_WRITABLE, DEC_BINDING, DEC_SET = 2, 3, 24, 33, 34
SC_UNIFORM_CONSTANT, SC_UNIFORM, SC_STORAGE_BUFFER = 0, 2, 12


def reflect(spv):
    words = struct.unpack("<%dI" % (len(spv) // 4), spv)
    names, decos, member_nonwritable, types, consts, variables = {}, {}, {}, {}, {}, []
    i = 5
    while i < len(words):
        op, count = words[i] & 0xFFFF, words[i] >> 16
        args = words[i + 1:i + count]
        if op == OP_NAME:
            raw = struct.pack("<%dI" % (len(args) - 1), *args[1:])
            names[args[0]] = raw.split(b"\0")[0].decode()
        elif op == OP_DECORATE:
            decos.setdefault(args[0], {})[args[1]] = args[2] if len(args) > 2 else True
        elif op == OP_MEMBER_DECORATE and args[2] == DEC_NON_WRITABLE:
            member_nonwritable.setdefault(args[0], set()).add(args[1])
        elif op in (OP_TYPE_IMAGE, OP_TYPE_SAMPLER, OP_TYPE_SAMPLED_IMAGE, OP_TYPE_ARRAY,
                    OP_TYPE_RUNTIME_ARRAY, OP_TYPE_STRUCT, OP_TYPE_POINTER):
            types[args[0]] = (op, args[1:])
        elif op == OP_CONSTANT:
            consts[args[1]] = args[2]
        elif op == OP_VARIABLE:
            variables.append((args[1], args[0], args[2]))  # id, pointer type, storage class
        i += count
    groups = {k: [] for k in ("CBV", "TextureSRV", "TextureUAV", "BufferSRV", "BufferUAV", "Sampler", "RTAccelerationStructure")}
    for vid, ptype, sc in variables:
        d = decos.get(vid, {})
        if DEC_BINDING not in d:
            continue
        t = types[ptype][1][1]  # the pointee
        count = 1
        while types[t][0] in (OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY):
            if types[t][0] == OP_TYPE_ARRAY:
                count *= consts[types[t][1][1]]
            else:
                count = 0
            t = types[t][1][0]
        op, targs = types[t]
        if op == OP_TYPE_IMAGE:
            kind = "TextureUAV" if targs[5] == 2 else "TextureSRV"  # Sampled operand
        elif op == OP_TYPE_SAMPLER:
            kind = "Sampler"
        elif op == OP_TYPE_STRUCT:
            td = decos.get(t, {})
            if sc == SC_UNIFORM and DEC_BLOCK in td:
                kind = "CBV"
            else:
                members = len(targs)
                ro = len(member_nonwritable.get(t, ())) == members or DEC_NON_WRITABLE in d
                kind = "BufferSRV" if ro else "BufferUAV"
        else:
            raise SystemExit("unsupported resource type %d" % op)
        groups[kind].append((d.get(DEC_SET, 0), d[DEC_BINDING], names.get(vid, ""), count))
    for k in groups:
        groups[k].sort(key=lambda r: (r[0], r[1]))
    return groups


# ---------------------------------------------------------------- headers
def c_bytes(data):
    hexes = ["0x%02x" % b for b in data]
    return ",\n".join(",".join(hexes[i:i + 16]) for i in range(0, len(hexes), 16))


def compile_pass(glslang, src, include_dirs, defines, out):
    cmd = [glslang, "-V", "--target-env", "vulkan1.2", "-S", "comp", "-Os"] + BASE + defines
    cmd += ["-I" + d for d in include_dirs] + ["-o", out, src]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("glslang failed for %s %s:\n%s%s" % (src, defines, r.stdout, r.stderr))
    with open(out, "rb") as f:
        return f.read()


def write_pass(outdir, name, file_name, perms):
    unique = []
    for p in perms:
        if p[1] not in [u[1] for u in unique]:
            unique.append(p)
    index_of = {p[0]: [u[1] for u in unique].index(p[1]) for p in perms}
    lines = ["// %s_permutations.h: generated by tools/ffx_gen_shaders.py (FidelityFX SDK 1.1.4 FSR 3.1 upscaler)." % file_name,
             "#pragma once", ""]
    for _, digest, spv, refl in unique:
        pname = "%s_%s" % (file_name, digest)
        for kind, res in refl.items():
            if not res:
                continue
            lines.append("static const char* g_%s_%sResourceNames[] = { %s };" % (pname, kind, ", ".join('"%s"' % r[2] for r in res)))
            lines.append("static const uint32_t g_%s_%sResourceBindings[] = { %s };" % (pname, kind, ", ".join(str(r[1]) for r in res)))
            lines.append("static const uint32_t g_%s_%sResourceCounts[] = { %s };" % (pname, kind, ", ".join(str(r[3]) for r in res)))
            lines.append("static const uint32_t g_%s_%sResourceSets[] = { %s };" % (pname, kind, ", ".join(str(r[0]) for r in res)))
        lines.append("static const uint32_t g_%s_size = %d;" % (pname, len(spv)))
        lines.append("static const unsigned char g_%s_data[] = {\n%s\n};\n" % (pname, c_bytes(spv)))
    if file_name == name:  # the key and info types, once per pass
        lines.append("typedef union %s_PermutationKey {" % name)
        lines.append("    struct {")
        for o, _ in OPTIONS:
            lines.append("        uint32_t %s : 1;" % o)
        lines.append("    };")
        lines.append("    uint32_t index;")
        lines.append("} %s_PermutationKey;\n" % name)
        lines.append("typedef struct %s_PermutationInfo {" % name)
        lines.append("    const uint32_t       blobSize;")
        lines.append("    const unsigned char* blobData;")
        for field, plural in (("ConstantBuffer", "constantBuffer"), ("SRVTexture", "srvTexture"), ("UAVTexture", "uavTexture"),
                              ("SRVBuffer", "srvBuffer"), ("UAVBuffer", "uavBuffer"), ("Sampler", "sampler"),
                              ("RTAccelerationStructure", "rtAccelerationStructure")):
            lines.append("    const uint32_t  num%ss;" % field)
            for suffix in ("Names", "Bindings", "Counts", "Spaces"):
                lines.append("    const %s %s%s;" % ("char**   " if suffix == "Names" else "uint32_t*", plural, suffix))
        lines.append("} %s_PermutationInfo;\n" % name)
    else:
        lines.insert(2, '#include "%s_permutations.h"' % name)
    lines.append("static const uint32_t g_%s_IndirectionTable[] = {" % file_name)
    sharpen_bit = len(OPTIONS) - 1
    for key in range(1 << len(OPTIONS)):
        lines.append("    %d," % index_of[(key >> sharpen_bit) & 1])
    lines.append("};\n")
    lines.append("static const %s_PermutationInfo g_%s_PermutationInfo[] = {" % (name, file_name))
    for _, digest, spv, refl in unique:
        pname = "%s_%s" % (file_name, digest)
        parts = ["g_%s_size" % pname, "g_%s_data" % pname]
        for kind in ("CBV", "TextureSRV", "TextureUAV", "BufferSRV", "BufferUAV", "Sampler", "RTAccelerationStructure"):
            if refl[kind]:
                parts.append("%d, g_%s_%sResourceNames, g_%s_%sResourceBindings, g_%s_%sResourceCounts, g_%s_%sResourceSets" %
                             ((len(refl[kind]),) + (pname, kind) * 4))
            else:
                parts.append("0, 0, 0, 0, 0")
        lines.append("    { %s }," % ", ".join(parts))
    lines.append("};")
    with open(os.path.join(outdir, file_name + "_permutations.h"), "w") as f:
        f.write("\n".join(lines) + "\n")
    return len(unique)


def main():
    glslang, root = sys.argv[1], sys.argv[2]
    shaders = os.path.join(root, "src/backends/vk/shaders/fsr3upscaler")
    gpu = os.path.join(root, "include/FidelityFX/gpu")
    outdir = os.path.join(root, "generated")
    os.makedirs(outdir, exist_ok=True)
    tmp = tempfile.mkdtemp()
    for fn in sorted(os.listdir(shaders)):
        if not fn.endswith("_pass.glsl"):
            continue
        name = fn[:-5]
        perms = []  # (sharpen value, md5, spv, reflection)
        for sharpen in (0, 1):
            defines = ["-D%s=%d" % (o, sharpen if v is None else v) for o, v in OPTIONS]
            spv = compile_pass(glslang, os.path.join(shaders, fn), [gpu, os.path.join(gpu, "fsr3upscaler")], defines,
                               os.path.join(tmp, "%s_%d.spv" % (name, sharpen)))
            perms.append((sharpen, hashlib.md5(spv).hexdigest(), spv, reflect(spv)))
        n = write_pass(outdir, name, name, perms)
        half = []
        for sharpen in (0, 1):
            defines = ["-DFFX_HALF=1"] + ["-D%s=%d" % (o, sharpen if v is None else v) for o, v in OPTIONS]
            spv = compile_pass(glslang, os.path.join(shaders, fn), [gpu, os.path.join(gpu, "fsr3upscaler")], defines,
                               os.path.join(tmp, "%s_16_%d.spv" % (name, sharpen)))
            half.append((sharpen, hashlib.md5(spv).hexdigest(), spv, reflect(spv)))
        write_pass(outdir, name, name + "_16bit", half)
        # wave64 variants: the port reports wave32; alias them to the wave32 builds.
        for variant, base in (("wave64", name), ("wave64_16bit", name + "_16bit")):
            alias = ["// %s_%s_permutations.h: generated; the port uses the wave32 permutations." % (name, variant), "#pragma once",
                     '#include "%s_permutations.h"' % base,
                     "#define g_%s_%s_IndirectionTable g_%s_IndirectionTable" % (name, variant, base),
                     "#define g_%s_%s_PermutationInfo g_%s_PermutationInfo" % (name, variant, base)]
            with open(os.path.join(outdir, "%s_%s_permutations.h" % (name, variant)), "w") as f:
                f.write("\n".join(alias) + "\n")
        print(name, n, "permutation(s)")


if __name__ == "__main__":
    main()
