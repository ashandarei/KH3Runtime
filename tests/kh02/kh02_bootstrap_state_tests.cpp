#include "kh3ap/features/kh02/runtime/kh02_bootstrap_state.h"
#include "kh3ap/features/kh02/runtime/kh02_load_gate_policy.h"
#include <cassert>

namespace
{
    void validate_owned_native_load_route()
    {
        using namespace kh3ap::loading_runtime;
        constexpr uintptr_t original_entry = 0x10000000;
        constexpr size_t stolen_size = 7;
        constexpr size_t trampoline_size = 98;
        const std::array<uint8_t, 6> return_jump{0xff, 0x25, 0x4b, 0, 0, 0};
        assert(native_load_gate_jump_holder_offset(stolen_size, trampoline_size, return_jump) == 88);
        auto invalid_jump = return_jump;
        invalid_jump[1] = 0x24;
        assert(!native_load_gate_jump_holder_offset(stolen_size, trampoline_size, invalid_jump));
        invalid_jump = return_jump;
        invalid_jump[2] = 0x80;
        assert(!native_load_gate_jump_holder_offset(stolen_size, trampoline_size, invalid_jump));
        const std::array<uint8_t, 6> negative_jump{0xff, 0x25, 0xf2, 0xff, 0xff, 0xff};
        assert(!native_load_gate_jump_holder_offset(stolen_size, trampoline_size, negative_jump));
        assert(!native_load_gate_jump_holder_offset(stolen_size, 20, return_jump));
        assert(!native_load_gate_jump_holder_offset(stolen_size, trampoline_size,
                                                    std::span<const uint8_t>(return_jump).first(5)));
        auto installed_patch = native_load_gate_prologue;
        const std::array<uint8_t, stolen_size> entry_jump{0xff, 0x25, 0x12, 0x34, 0x56, 0x00, 0x90};
        std::copy(entry_jump.begin(), entry_jump.end(), installed_patch.begin());
        auto trampoline = native_load_gate_prologue;
        // The return jump replaces the next original instruction in the
        // trampoline. Only the seven actually stolen bytes must match.
        trampoline[stolen_size] = 0xff;
        trampoline[stolen_size + 1] = 0x25;
        assert(native_load_gate_bytes_match(stolen_size, installed_patch, installed_patch, trampoline,
                                            original_entry + stolen_size, original_entry));
        assert(!native_load_gate_bytes_match(stolen_size, installed_patch, installed_patch, trampoline,
                                             original_entry + stolen_size + 1, original_entry));

        auto changed_trampoline = trampoline;
        changed_trampoline[0] ^= 1;
        assert(!native_load_gate_bytes_match(stolen_size, installed_patch, installed_patch, changed_trampoline,
                                             original_entry + stolen_size, original_entry));
        auto changed_patch = installed_patch;
        changed_patch[0] ^= 1;
        assert(!native_load_gate_bytes_match(stolen_size, changed_patch, installed_patch, trampoline,
                                             original_entry + stolen_size, original_entry));
        changed_patch = installed_patch;
        changed_patch[stolen_size] ^= 1;
        assert(!native_load_gate_bytes_match(stolen_size, changed_patch, changed_patch, trampoline,
                                             original_entry + stolen_size, original_entry));
        for (const auto split_instruction : {size_t{6}, size_t{8}})
        {
            assert(!native_load_gate_bytes_match(split_instruction, installed_patch, installed_patch, trampoline,
                                                 original_entry + split_instruction, original_entry));
        }
        assert(!native_load_gate_bytes_match(stolen_size, installed_patch, installed_patch,
                                             std::span<const uint8_t>(trampoline).first(stolen_size - 1),
                                             original_entry + stolen_size, original_entry));
        assert(!native_load_gate_bytes_match(stolen_size, std::span<const uint8_t>(installed_patch).first(16),
                                             installed_patch, trampoline, original_entry + stolen_size,
                                             original_entry));

        // PolyHook's in-place fallback steals the entire pinned prefix.
        auto full_patch = native_load_gate_prologue;
        full_patch.fill(0x90);
        assert(native_load_gate_bytes_match(full_patch.size(), full_patch, full_patch, native_load_gate_prologue,
                                            original_entry + full_patch.size(), original_entry));

        // Steam retains the same whole-instruction boundaries but has a
        // different frame. Neither image's unstolen tail may validate as the other.
        auto steam_patch = steam_load_gate_prologue;
        std::copy(entry_jump.begin(), entry_jump.end(), steam_patch.begin());
        auto steam_trampoline = steam_load_gate_prologue;
        steam_trampoline[stolen_size] = 0xff;
        steam_trampoline[stolen_size + 1] = 0x25;
        assert(native_load_gate_bytes_match(stolen_size, steam_patch, steam_patch, steam_trampoline,
                                            original_entry + stolen_size, original_entry, steam_load_gate_prologue));
        assert(!native_load_gate_bytes_match(stolen_size, steam_patch, steam_patch, steam_trampoline,
                                             original_entry + stolen_size, original_entry));
        assert(!native_load_gate_bytes_match(stolen_size, installed_patch, installed_patch, trampoline,
                                             original_entry + stolen_size, original_entry, steam_load_gate_prologue));
    }
} // namespace

int main()
{
    validate_owned_native_load_route();
    using State = kh3ap::Kh02BootstrapState;
    using Decision = State::Decision;
    const auto start = State::Clock::time_point{};
    State state;
    assert(state.begin(start));
    const auto first_generation = state.generation();
    assert(!state.begin(start + std::chrono::seconds(30)));
    assert(state.generation() == first_generation);
    assert(state.poll(start, true, true, false) == Decision::wait);
    assert(state.poll(start, true, false, true) == Decision::wait);
    assert(state.poll(start, true, true, true) == Decision::dispatch);
    assert(state.poll(start, true, true, true) == Decision::wait);

    assert(state.begin(start));
    assert(state.poll(start + State::preparation_timeout, true, true, true) == Decision::cancel);
    assert(state.poll(start + State::preparation_timeout, true, true, true) == Decision::wait);
    assert(!state.fail());

    assert(state.begin(start));
    assert(state.poll(start, false, true, true) == Decision::cancel);
    assert(state.poll(start, true, true, true) == Decision::wait);

    assert(state.begin(start));
    assert(state.fail()); // Provider/alias failure retires the pending request once.
    const kh3ap::Kh02UnavailableLoadCompletion failure{53};
    assert(failure.result == 2);
    assert(failure.result != 1); // Native success must never be fabricated.
    assert(failure.slot == 53);
    assert(failure.metadata == nullptr); // Matches native no-save failure.
    assert(!state.fail());
    assert(state.poll(start, true, true, true) == Decision::wait);
    assert(state.begin(start));
    assert(state.generation() == first_generation + 4);
    assert(state.poll(start, true, true, true) == Decision::dispatch);
}
