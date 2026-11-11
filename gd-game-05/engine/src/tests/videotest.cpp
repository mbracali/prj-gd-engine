// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/videotest.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <set>
#include <string>

#include "data/dta.h"
#include "data/game_data.h"
#include "video/wmv_player.h"

int runVideoTest(const std::string& dataPath) {
    try {
        GameData data(dataPath);
        if (!WmvPlayer::available()) {
            std::printf("videotest: FAIL: no Bink decoders in this build (FFmpeg missing, or GD05_VIDEO OFF)\n");
            return 1;
        }
        // Every string inside a (files ...) of videos.dtb [C data].
        std::set<std::string> names;
        const DataNode root = parseDtb(data.read("config/gen/videos.dtb"));
        std::function<void(const DataNode&, bool)> walk = [&](const DataNode& n, bool inFiles) {
            if (n.isList()) {
                const bool files = !n.children.empty() && n.children[0].type == DataNode::Type::Symbol && n.children[0].s == "files";
                for (const auto& c : n.children) walk(c, files || inFiles);
            } else if (inFiles && n.type == DataNode::Type::String) {
                names.insert(n.s);
            }
        };
        walk(root, false);
        int bad = 0;
        long long total = 0;
        for (const auto& name : names) {
            WmvPlayer p;
            std::string error;
            if (!p.open(data, "videos/" + name, error)) {
                std::printf("  FAIL %s: %s\n", name.c_str(), error.c_str());
                ++bad;
                continue;
            }
            // The Bink header [C data]: "BIK" + revision, u32 size - 8, u32
            // frame count at +8, ..., u32 audio track count at +40.
            uint8_t head[44] = {};
            auto file = data.openDiscFile("videos/" + name);
            if (!file || file->read(0, head, sizeof head) != sizeof head || head[0] != 'B' || head[1] != 'I' || head[2] != 'K') {
                std::printf("  FAIL %s: no Bink header\n", name.c_str());
                ++bad;
                continue;
            }
            auto le = [&](int at) -> long long { return uint32_t(head[at]) | uint32_t(head[at + 1]) << 8 | uint32_t(head[at + 2]) << 16 | uint32_t(head[at + 3]) << 24; };
            const long long want = le(8), tracks = le(40);
            const int got = p.dumpFrames(INT_MAX, nullptr);
            const bool ok = got > 0 && got == want && p.hasSound() == (tracks > 0);
            std::printf("  %s %s: %d of %lld frames decoded, %lld audio track(s), %s\n", ok ? "ok  " : "FAIL", name.c_str(), got, want,
                        tracks, p.hasSound() ? "sound decoder open" : "no sound");
            if (!ok) ++bad;
            total += got;
        }
        std::printf("videotest: %zu videos, %lld frames decoded; %d failed\n", names.size(), total, bad);
        std::printf("videotest: %s\n", bad || names.empty() ? "FAIL" : "OK");
        return bad || names.empty() ? 1 : 0;
    } catch (const std::exception& e) {
        std::printf("videotest: FAIL: %s\n", e.what());
        return 1;
    }
}
