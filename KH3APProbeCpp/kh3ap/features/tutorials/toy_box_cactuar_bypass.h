#pragma once

#include "kh3ap/features/tutorials/tutorial_popup_bypass.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace kh3ap
{
    // The three reactor delegates only forward an entry point. Their three
    // parameter copies are never read by the audited EGS/Steam ubergraphs. Replace
    // that unused prefix with the owning block's existing stop call, retaining
    // the original RemoteEvent/collected-flag path and animation completion.
    [[nodiscard]] inline auto try_patch_cactuar_reactor_delegate(
            uint8_t* script, size_t size, const uint8_t* owner, size_t owner_size,
            size_t part) noexcept -> RuntimeTutorialPopupBypassResult
    {
        using R = RuntimeTutorialPopupBypassResult;
        constexpr std::array<size_t, 3> stops{0x1B96, 0x3E76, 0x4141};
        constexpr std::array<uint32_t, 3> entries{0x1AA3, 0x4773, 0x471F};
        if (!script || !owner || part >= 3 || size != 0x4C ||
            owner_size != (part == 0 ? 0x2418u : 0x502Bu)) return R::Rejected;
        const auto* stop = owner + stops[part];
        if (stop[0] != 0x19 || stop[1] != 1 || stop[0x16] != 0x1C ||
            stop[0x1F] != 0x27 || stop[0x20] != 0x16 ||
            script[0x36] != 0x1B || script[0x43] != 0x1D ||
            std::memcmp(script + 0x44, &entries[part], 4) != 0 ||
            script[0x48] != 0x16 || script[0x49] != 4 ||
            script[0x4A] != 0x0B || script[0x4B] != 0x53) return R::Rejected;
        std::array<uint8_t, 0x36> replacement{};
        replacement.fill(0x0B); // EX_Nothing; no offsets or script size change.
        std::copy_n(stop, 0x21, replacement.begin());
        if (std::equal(replacement.begin(), replacement.end(), script)) return R::AlreadyPatched;
        for (size_t at : {0u, 0x12u, 0x24u})
            if (script[at] != 0x64 || script[at + 9] != 0) return R::Rejected;
        std::copy(replacement.begin(), replacement.end(), script);
        return R::Patched;
    }

    // EGS CL1065258 and Steam 66743F34 ship identical ms141/ms142 and
    // Denjigimmickfront/Back packages (paired base-pak audit, 2026-10-18).
    // Select by script layout and retain every bytecode guard on both stores.
    // ms141 omits the post-battle camera reveal. ms142
    // keeps every block/build animation but removes cinematic ownership.
    // Its director tracks are disabled separately at map readiness.
    [[nodiscard]] inline auto try_patch_runtime_toy_box_cactuar_bytecode(
            uint8_t* script, size_t size, bool assemble) noexcept
            -> RuntimeTutorialPopupBypassResult
    {
        using R = RuntimeTutorialPopupBypassResult;
        if (!script || size != (assemble ? 0x10CEFu : 0xABEAu)) return R::Rejected;
        const auto matches = [&](size_t offset, std::initializer_list<uint8_t> bytes) {
            return std::equal(bytes.begin(), bytes.end(), script + offset);
        };
        const auto branch = [](uint8_t op, size_t to) {
            return std::array<uint8_t, 5>{op, uint8_t(to), uint8_t(to >> 8),
                    uint8_t(to >> 16), uint8_t(to >> 24)};
        };
        if (!assemble)
        {
            constexpr std::array<size_t, 3> entries{0xA7C2, 0xA7DF, 0xA7E4};
            constexpr std::array<size_t, 3> original{0xA740, 0xA7AA, 0xA7C7};
            const auto after = branch(6, 0x8784);
            bool before_all = true, after_all = true;
            for (size_t i = 0; i < entries.size(); ++i)
            {
                const auto before = branch(6, original[i]);
                before_all &= std::equal(before.begin(), before.end(), script + entries[i]);
                after_all &= std::equal(after.begin(), after.end(), script + entries[i]);
            }
            if ((!before_all && !after_all) || !matches(0x8784, {0x14, 0}) ||
                script[0x87A4] != 0x1C || script[0x87AE] != 0x21 || script[0x87BC] != 0x4D)
                return R::Rejected;
            if (after_all) return R::AlreadyPatched;
            for (size_t entry : entries) std::copy(after.begin(), after.end(), script + entry);
            return R::Patched;
        }

        // Sources stay intact, including resolved UObject/FName operands.
        // Only unused cinematic cleanup becomes storage for these small paths.
        constexpr size_t scratch = 0xD29B;
        constexpr std::array<size_t, 3> collection_entries{0x1075E, 0x10763, 0x10768};
        constexpr std::array<size_t, 3> collection_originals{0x1065F, 0x106B4, 0x10709};
        constexpr std::array<size_t, 3> names{0x10694, 0x106E9, 0x1073E};
        constexpr std::array<size_t, 3> first_matinee_properties{0x1068C, 0x106E1, 0x10736};
        constexpr std::array<size_t, 3> second_plays{0x6C7A, 0x661E, 0x696B};
        constexpr std::array<size_t, 3> first_finished{0xE61D, 0xE586, 0xE4AF};
        constexpr std::array<size_t, 3> second_finished{0xE3C7, 0xE539, 0xE5B7};
        constexpr std::array<size_t, 3> build_entries{0x108DA, 0x10901, 0x1076D};
        constexpr std::array<size_t, 3> build_plays{0x1084B, 0x10948, 0x107B4};
        constexpr std::array<size_t, 3> build_finished{0xE89D, 0xE7AD, 0xE873};
        if (!matches(0x105A2, {0x5F, 0}) || script[0x105CD] != 0x5F ||
            script[0x10600] != 0x0F || script[0x10641] != 0x4F ||
            !matches(0x1064B, {0x5F, 1}) || script[0x1065E] != 0x4D ||
            !matches(0xE8D6, {0x14, 0}) || script[0xE8F6] != 0x1C || script[0xE8FF] != 0x16 ||
            !matches(0xE990, {0x19, 0x20}) || script[0xE9A6] != 0x1C ||
            !matches(0xE9B0, {0x1E, 0, 0, 0, 0x3F, 0x2F}) ||
            !matches(0xE9C2, {0x5B, 0xBB, 0x8C, 0, 0}) ||
            script[0xE9C7] != 0x1D || script[0xE9CC] != 0x21 || script[0xE9DC] != 0x4D ||
            !matches(0xDEF3, {0x14, 0}) || script[0xDF13] != 0x1C || script[0xDF1D] != 1 ||
            !matches(0xE900, {0x19, 1}) || script[0xE923] != 0x27 ||
            !matches(0x8C7F, {0x19, 1}) || !matches(0xE31A, {0x19, 1})) return R::Rejected;
        for (size_t i = 0; i < 3; ++i)
        {
            if (script[names[i]] != 0x0F || script[names[i] + 9] != 1 || script[names[i] + 0x12] != 0x21 ||
                script[first_matinee_properties[i] - 1] != 1) return R::Rejected;
            for (size_t play : {second_plays[i], build_plays[i]})
                if (!matches(play, {0x19, 1}) || script[play + 0x16] != 0x1B || script[play + 0x23] != 0x16)
                    return R::Rejected;
        }
        for (size_t event : {0xE925u, 0xE835u, 0xE6B0u, 0xE6E8u, 0xE720u})
            if (!matches(event, {0x14, 0}) || script[event + 0x20] != 0x1C || script[event + 0x2A] != 0x21)
                return R::Rejected;

        struct Edit { size_t at{}, count{}; std::array<uint8_t, 5> bytes{}; uint8_t original{}; };
        std::array<Edit, 24> edits{};
        size_t edit_count = 0;
        std::array<uint8_t, 0x800> code{};
        size_t used = 0;
        const auto copy = [&](size_t from, size_t count) {
            std::memcpy(code.data() + used, script + from, count); used += count;
        };
        const auto emit_branch = [&](uint8_t op, size_t to) {
            const auto bytes = branch(op, to);
            std::copy(bytes.begin(), bytes.end(), code.data() + used); used += 5;
        };
        const auto end = [&] { code[used++] = 0x4D; };
        const auto redirect = [&](size_t at, uint8_t original) {
            edits[edit_count++] = {at, 5, branch(6, scratch + used), original};
        };
        const auto ignore = [&](size_t at, uint8_t original) {
            edits[edit_count++] = {at, 1, {0x4D}, original};
        };
        const auto release_player = [&] {
            // Reacquire the current Gigas for every command; do not rely on a
            // previous collection. Preserve the reactor's next-tick ordering.
            emit_branch(0x4C, scratch + used + 10);
            emit_branch(6, 0x105A2);
            const size_t delay = used;
            copy(0xE990, 0x4C);
            std::fill_n(code.data() + delay + 0x21, 4, uint8_t{0});
            const uint32_t resume = static_cast<uint32_t>(scratch + used + 1);
            std::memcpy(code.data() + delay + 0x33, &resume, 4);
            std::memcpy(code.data() + delay + 0x38, &resume, 4); // unique latent UUID
            end();
            copy(0xE8D6, 0x2A); // EndElectroState, without a camera/cinematic reset.
        };
        for (size_t i = 0; i < 3; ++i)
        {
            redirect(collection_entries[i], 6);
            release_player();
            const size_t play = used;
            copy(second_plays[i], 0x24);
            // Play the complete collection animation (_1 then _2), not just
            // its end-frame transform. The camera director is disabled.
            std::memcpy(code.data() + play + 2, script + first_matinee_properties[i], 8);
            end();
            redirect(first_finished[i], i == 2 ? 0x19 : 6);
            copy(second_plays[i], 0x24); end();
            redirect(second_finished[i], i == 0 ? 0x0F : 0x14);
            copy(names[i], 0x1F); copy(0xDEF3, 0x34); end();
        }
        for (size_t i = 0; i < 3; ++i)
        {
            redirect(build_entries[i], 0x19);
            release_player();
            const size_t show = used;
            copy(0xE900, 0x25); code[show + 0x23] = 0x28; // SetActorHiddenInGame(false)
            copy(build_plays[i], 0x24); end();
            redirect(build_finished[i], 0x14);
            if (i == 2)
            {
                copy(0x8C7F, 0x24); end(); // final build -> buildEnd_0 -> buildEnd_1
            }
            else
            {
                copy(0xE900, 0x25); copy(i == 0 ? 0xE925 : 0xE835, 0x38); end();
            }
        }
        redirect(0xE628, 0x14);
        copy(0xE900, 0x25); copy(0xE6B0, 0xA8); end();
        // The collection Played callbacks changed view mode and destroyed
        // nearby Gigas. Beam/shake callbacks also belonged to the locked scene.
        for (size_t at : {0xE33Fu, 0xE512u, 0xE590u}) ignore(at, 0x19);
        ignore(0xE4E6, 0x14); ignore(0xE58B, 6); ignore(0xE622, 6); ignore(0xE763, 6);

        bool before_all = true, after_all = true, any_after = false;
        for (size_t i = 0; i < edit_count; ++i)
        {
            const auto& e = edits[i];
            before_all &= script[e.at] == e.original;
            const bool patched = std::equal(e.bytes.begin(), e.bytes.begin() + e.count, script + e.at);
            after_all &= patched; any_after |= patched;
        }
        if (after_all)
            return std::equal(code.begin(), code.begin() + used, script + scratch) ? R::AlreadyPatched : R::Rejected;
        if (any_after || !before_all || !matches(scratch, {0x5F, 0}) || script[0xD2F6] != 0x0F) return R::Rejected;
        for (size_t i = 0; i < 3; ++i)
        {
            const auto original = branch(6, collection_originals[i]);
            if (!std::equal(original.begin(), original.end(), script + collection_entries[i])) return R::Rejected;
        }
        std::copy_n(code.begin(), used, script + scratch);
        for (size_t i = 0; i < edit_count; ++i)
        {
            const auto& e = edits[i];
            std::copy_n(e.bytes.begin(), e.count, script + e.at);
        }
        return R::Patched;
    }
}
