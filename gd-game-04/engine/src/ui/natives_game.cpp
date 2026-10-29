// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/natives_game.h"

#include "data/qbkey.h"
#include "render/texture_cache.h"
#include "ui/front_end.h"

namespace {
uint32_t K(const char* s) { return qbkey::key(s); }
qb::Struct* locals(qs::ScriptRun& run) {
    if (run.frames.empty()) return nullptr;
    if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
    return run.frames.back().locals.get();
}
}  // namespace

void addGameNatives(FrontEnd& fe, const Disc& disc, TextureCache& textures) {
    using qs::ScriptRun;
    using qs::Vm;
    (void)disc;
    (void)textures;
    qs::Vm& vm = fe.vm();
    // GetButtonsPressed: makes = buttons pressed since the last call, device_num [I].
    vm.addNative("GetButtonsPressed", [&fe](Vm&, ScriptRun& run, qb::Struct&) {
        if (qb::Struct* l = locals(run)) {
            Vm::set(*l, K("makes"), Vm::makeInt(fe.pad.makes));
            Vm::set(*l, K("device_num"), Vm::makeInt(fe.pad.device));
        }
        fe.pad.makes = 0;
        return true;
    });
}
