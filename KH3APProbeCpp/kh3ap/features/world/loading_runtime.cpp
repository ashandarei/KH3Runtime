#include <algorithm>
#include "kh3ap/core/ui/ui_layout.h"
#include "loading_runtime.h"
#include "kh3ap/features/kh02/runtime/kh02_load_gate_policy.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <cmath>
#include <array>
#include <sstream>
#include <iomanip>
#include <mutex>

namespace kh3ap::loading_runtime
{
    namespace
    {
        constexpr uint8_t unknown_native_flag_byte = 0xff;

        constexpr auto loading_native_task_prompt_ready_offset = 0x19;

        constexpr auto steam_slot_load_rva = 0x159d9e0;
        constexpr auto steam_load_manager_read_site_rva = 0x159da1d;
        constexpr auto steam_load_manager_write_site_rva = 0x159da62;
        constexpr auto steam_make_completion_rva = 0x1559bb0;
        constexpr auto steam_weak_pointer_null_check_rva = 0x2cf02ac;
        constexpr auto steam_continue_loading_rva = 0x158fda0;
        constexpr auto steam_retire_loading_task_rva = 0x158a8a0;
        constexpr auto steam_loading_completion_vtable_rva = 0x6a57a08;
        constexpr auto completion_clone_vtable_offset = 0x50;
        constexpr auto egs_continue_slot_mode_site_rva = 0x1903afd;
        constexpr auto egs_retry_slot_mode_site_rva = 0x1a7f338;
        constexpr auto egs_retry_load_delegate_site_rva = 0x1a7fa27;
        constexpr auto continue_loading_clear_flag_site_offset = 0x14;
        constexpr auto egs_loading_presentation_cleanup_rva = 0x191e8c0;
        constexpr auto steam_loading_screen_offset = 0x308;
        constexpr auto egs_loading_screen_offset = 0x2c8;
        constexpr auto loading_screen_ui_manager_offset = 0x70;
        constexpr auto loading_screen_movie_player_offset = 0x58;
        constexpr auto loading_screen_extra_object_offset = 0x208;
        constexpr auto loading_screen_mode_offset = 0x1d8;
        constexpr auto loading_screen_closed_offset = 0x1e0;
        constexpr auto loading_screen_progress_offset = 0x1cc;
        constexpr auto maximum_loading_screen_mode = 2;
        constexpr auto loading_tasks_owner_data_offset = 0x10;
        constexpr auto loading_tasks_owner_count_offset = 8;
        constexpr auto loading_native_task_state_offset = 8;
        constexpr auto completion_task_id_offset = 0x10;
        constexpr auto completion_task_status_offset = 0x14;
        constexpr auto completion_task_guard_byte_offset = 0x18;
        constexpr auto completion_predicate_offset = 0x40;
        constexpr auto steam_completion_delegate_count_offset = 0xb0;
        constexpr auto steam_completion_delegate_list_offset = 0xa0;
        constexpr auto completion_action_offset = 0x20;
        constexpr auto completion_predicate_kind_offset = 0x50;
        constexpr auto completion_action_kind_offset = 0x30;
        constexpr auto completion_delegate_kind = 4;
        constexpr auto completion_delegate_context_offset = 8;
        constexpr auto completion_delegate_function_offset = 0x18;
        constexpr auto load_trampoline_diagnostic_bytes = 48;
        constexpr auto hex_digits_per_byte = 2;

        constexpr auto steam_save_manager_offset_delta = 0x60;
        constexpr auto max_native_pe_header_offset = 0x1000;
        constexpr auto loading_operation_vtable_rva = 0x7c40fa0;
        constexpr auto loading_manager_vtable_rva = 0x7c45c78;
        constexpr auto loading_tasks_owner_vtable_rva = 0x7c40f90;
        constexpr auto loading_native_task_vtable_rva = 0x7c45ce8;
        constexpr auto loading_completion_task_vtable_rva = 0x7c41798;
        constexpr auto loading_predicate_vtable_rva = 0x7c40ba8;
        constexpr auto loading_action_vtable_rva = 0x7c40d60;
        constexpr auto loading_continue_predicate_rva = 0x18ec6a0;
        constexpr auto loading_operation_active_offset = 0x73;
        constexpr auto loading_operation_manual_offset = 0x19;
        constexpr auto loading_operation_ready_offset = 0x72;
        constexpr auto loading_operation_tasks_offset = 0x88;
        constexpr auto loading_operation_tasks_owner_offset = 0x90;
        constexpr auto loading_tasks_count_offset = 8;
        constexpr auto loading_tasks_free_count_offset = 0x34;
        constexpr auto loading_tasks_queued_count_offset = 0x58;
        constexpr auto loading_tasks_queued_free_count_offset = 0x84;
        constexpr auto loading_manager_native_task_offset = 0x38;
        constexpr auto loading_manager_presentation_offset = 0x48;
        constexpr auto loading_tasks_allocation_bits_offset = 0x20;
        constexpr auto loading_tasks_inline_bits_offset = 0x10;
        constexpr auto loading_tasks_allocation_count_offset = 0x28;
        constexpr auto loading_completion_task_offset = 8;
        constexpr auto loading_allocation_word_bits = 32;
        constexpr uintptr_t loading_tasks_lock_offset = 0xb8;
        constexpr uintptr_t loading_manager_lock_offset = 0x10;
        constexpr size_t completion_entry_stride_bytes = 0x20;
        constexpr size_t completion_action_delegate_offset = 0x60;

        constexpr auto max_loading_completion_callbacks = 64;
    } // namespace

    using namespace RC::Unreal;
    // EGS 1.0.0 CL 1065258. These are the normal slot-load operation and
    // completion delegate constructor, below the Continue/newest-save
    // selector (0x18c8850). No Debug*, OnCallback, key, or GFx invocation.
    // TresHUDTitle's normal New Game difficulty-confirmed delegate.
    // Bound to UTresUIP_Title+0x250 by 0x18c0a00 via 0x18cbf40. KH3 then
    // initializes new save state in 0x18c5950/0x112e240 and enters the map.

    AutoloadStore startup_store = AutoloadStore::unsupported;

    namespace
    {
        uintptr_t owned_load_site{}, owned_load_trampoline{};
        size_t owned_load_stolen_size{}, owned_load_trampoline_size{};
        std::array<uint8_t, native_load_gate_prologue.size()> owned_load_patch{};
        NativeLoadGateFailure load_gate_failure;
        std::mutex load_gate_failure_mutex;

        auto trampoline_resume_target() -> uintptr_t
        {
            if (!native_load_gate_stolen_size_valid(owned_load_stolen_size) ||
                owned_load_trampoline_size <
                    owned_load_stolen_size + native_load_gate_indirect_jump_size)
            {
                return 0;
            }
            std::array<uint8_t, native_load_gate_indirect_jump_size> jump{};
            const auto address = owned_load_trampoline + owned_load_stolen_size;
            if (!read_native(address, jump))
            {
                return 0;
            }
            const auto holder = native_load_gate_jump_holder_offset(
                owned_load_stolen_size, owned_load_trampoline_size, jump);
            if (!holder)
            {
                return 0;
            }
            uintptr_t target{};
            return read_native(owned_load_trampoline + *holder, target) ? target : 0;
        }

        auto owned_load_matches() -> bool
        {
            std::array<uint8_t, native_load_gate_prologue.size()> patch{}, trampoline{};
            return read_native(owned_load_site, patch) &&
                   read_native(owned_load_trampoline, trampoline) &&
                   native_load_gate_bytes_match(
                       owned_load_stolen_size, patch, owned_load_patch, trampoline,
                       trampoline_resume_target(), owned_load_site,
                       native_load_gate_expected_prologue(startup_store == AutoloadStore::steam));
        }
    } // namespace

    auto native_load_gate_original_matches(uintptr_t base) -> bool
    {
        std::array<uint8_t, native_load_gate_prologue.size()> original{};
        return startup_store != AutoloadStore::unsupported &&
               read_native(base + native_rva(load_slot_rva), original) &&
               original ==
                   native_load_gate_expected_prologue(startup_store == AutoloadStore::steam);
    }

    void register_owned_load_detour(uintptr_t base, uintptr_t trampoline, size_t stolen_size,
                                    size_t trampoline_size)
    {
        owned_load_site = base + native_rva(load_slot_rva);
        owned_load_trampoline = trampoline;
        owned_load_stolen_size = stolen_size;
        owned_load_trampoline_size = trampoline_size;
        read_native(owned_load_site, owned_load_patch);
    }

    auto native_load_gate_diagnostic() -> std::wstring
    {
        std::array<uint8_t, native_load_gate_prologue.size()> current{};

        std::array<uint8_t, load_trampoline_diagnostic_bytes> trampoline{};
        const bool patch_read = read_native(owned_load_site, current);
        const bool trampoline_read = read_native(owned_load_trampoline, trampoline);
        std::wostringstream output;
        output << L"stolen=" << owned_load_stolen_size << L" trampoline_size="
               << owned_load_trampoline_size << L" entry=0x" << std::hex << owned_load_site
               << L" trampoline=0x" << owned_load_trampoline << L" resume=0x"
               << trampoline_resume_target() << L" patch_read=" << patch_read
               << L" trampoline_read=" << trampoline_read;
        const auto bytes = [&](const wchar_t* label, const auto& data)
        {
            output << L" " << label << L"=";
            for (const auto byte : data)
            {
                output << std::setw(hex_digits_per_byte) << std::setfill(L'0')
                       << static_cast<unsigned>(byte);
            }
        };
        bytes(L"expected",
              native_load_gate_expected_prologue(startup_store == AutoloadStore::steam));
        bytes(L"installed", owned_load_patch);
        bytes(L"current", current);
        bytes(L"trampoline_bytes", trampoline);
        return output.str();
    }

    void clear_owned_load_detour()
    {
        owned_load_site = 0;
        owned_load_trampoline = 0;
        owned_load_stolen_size = 0;
        owned_load_trampoline_size = 0;
        owned_load_patch = {};
    }

    auto native_load_gate_installed() -> bool
    {
        return owned_load_site != 0 && owned_load_trampoline != 0;
    }

    auto native_load_gate_failure() -> NativeLoadGateFailure
    {
        std::lock_guard lock(load_gate_failure_mutex);
        return load_gate_failure;
    }

    void report_native_load_gate_failure(uint64_t request_generation, std::string_view reason)
    {
        std::lock_guard lock(load_gate_failure_mutex);
        ++load_gate_failure.sequence;
        load_gate_failure.request_generation = request_generation;
        load_gate_failure.reason = reason;
    }

    void clear_native_load_gate_failure()
    {
        std::lock_guard lock(load_gate_failure_mutex);
        load_gate_failure.reason.clear();
    }

    uintptr_t native_rva(uintptr_t egs)
    {
        return autoload_rva(startup_store, egs);
    }

    size_t manager_offset(size_t egs)
    {
        return egs + (startup_store == AutoloadStore::steam ? steam_save_manager_offset_delta : 0);
    }

    bool matches(uintptr_t base, uintptr_t rva, std::initializer_list<uint8_t> signature)
    {
        if (owned_load_trampoline && base + rva == owned_load_site)
        {
            // PolyHook copies only the stolen whole instructions, then jumps
            // back. Validate that complete route, not a contiguous 16-byte copy
            // that crosses its correctly emitted return jump after seven bytes.
            const auto& expected =
                native_load_gate_expected_prologue(startup_store == AutoloadStore::steam);
            return signature.size() <= expected.size() &&
                   std::equal(signature.begin(), signature.end(), expected.begin()) &&
                   owned_load_matches();
        }
        for (const auto expected : signature)
        {
            uint8_t actual{};
            if (!read_native(base + rva++, actual) || actual != expected)
            {
                return false;
            }
        }
        return true;
    }

    bool supported_image(uintptr_t base)
    {
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        if (!read_native(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
            dos.e_lfanew >= max_native_pe_header_offset || !read_native(base + dos.e_lfanew, nt) ||
            nt.Signature != IMAGE_NT_SIGNATURE)
        {
            return false;
        }
        startup_store = autoload_store(nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage);
        if (startup_store == AutoloadStore::steam)
        {
            // Checked against Steam PE 66743F34/A969000. The different slot-loader
            // frame, manager offsets, callback clone and Continue ABI are
            // validated before any native dispatch.
            return matches(base, steam_slot_load_rva,
                           {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48,
                            0x8d, 0x6c, 0x24, 0xe0}) &&
                   matches(base, steam_load_manager_read_site_rva,
                           {0x8b, 0x81, 0x30, 0x6a, 0, 0}) &&
                   matches(base, steam_load_manager_write_site_rva,
                           {0x45, 0x89, 0xa6, 0x90, 0x6a, 0, 0}) &&
                   matches(base, steam_make_completion_rva,
                           {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xb9, 0x20, 0, 0,
                            0}) &&
                   matches(base, steam_weak_pointer_null_check_rva,
                           {0x48, 0x8d, 0x48, 0x01, 0x48, 0x85, 0xc9}) &&
                   matches(base, steam_continue_loading_rva,
                           {0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe,
                            0xff, 0xff, 0xff, 0xc6, 0x41, 0x73, 0}) &&
                   matches(base, steam_retire_loading_task_rva,
                           {0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe,
                            0xff, 0xff, 0xff}) &&
                   matches(base, native_rva(egs_loading_presentation_cleanup_rva),
                           {0x4c, 0x8b, 0xdc, 0x53, 0x56, 0x57, 0x48, 0x83, 0xec, 0x60}) &&
                   read_field<uintptr_t>(
                       reinterpret_cast<void*>(base + steam_loading_completion_vtable_rva),
                       completion_clone_vtable_offset) == base + steam_make_completion_rva;
        }
        return startup_store == AutoloadStore::egs &&
               matches(base, save_slot_rva,
                       {0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                        0x48, 0x8d, 0x6c, 0x24, 0xe1}) &&
               matches(base, load_slot_rva,
                       {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48,
                        0x8d, 0x6c, 0x24, 0xe9}) &&
               matches(base, make_completion_rva,
                       {0x4c, 0x8b, 0xdc, 0x57, 0x48, 0x81, 0xec, 0x80, 0, 0, 0}) &&
               matches(base, egs_continue_slot_mode_site_rva,
                       {0xc7, 0x81, 0x20, 0x6a, 0, 0, 1, 0, 0, 0}) &&
               matches(base, egs_retry_slot_mode_site_rva,
                       {0xc7, 0x86, 0x20, 0x6a, 0, 0, 2, 0, 0, 0}) &&
               matches(base, egs_retry_load_delegate_site_rva,
                       {0x83, 0xfa, 0x01, 0x75, 0x11, 0x48, 0x81, 0xc1, 0xf0, 0, 0, 0}) &&
               matches(base, destroy_delegate_rva,
                       {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x83, 0x79, 0x30, 0}) &&
               matches(base, close_loading_screen_rva,
                       {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b,
                        0x01, 0x0f, 0xb6, 0xfa}) &&
               matches(base, continue_loading_rva,
                       {0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe,
                        0xff, 0xff, 0xff}) &&
               matches(base, continue_loading_rva + continue_loading_clear_flag_site_offset,
                       {0xc6, 0x41, 0x73, 0x00}) &&
               matches(base, retire_loading_task_rva,
                       {0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe,
                        0xff, 0xff, 0xff}) &&
               matches(base, egs_loading_presentation_cleanup_rva,
                       {0x48, 0x8b, 0xc4, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
                        0x56, 0x41, 0x57}) &&
               matches(base, free_rva,
                       {0x48, 0x85, 0xc9, 0x74, 0x2e, 0x53, 0x48, 0x83, 0xec, 0x20});
    }

    bool property_offset(UObject* object, const wchar_t* name, int offset)
    {
        if (!is_uobject_valid_fast(object))
        {
            return false;
        }
        const auto* property = object->GetPropertyByNameInChain(name);
        return property && property->GetOffset_Internal() == offset;
    }

    UObject* owned_save_manager(UObject* pawn)
    {
        if (!is_uobject_valid_fast(pawn))
        {
            return nullptr;
        }
        auto* level = pawn->GetOuterPrivate();
        if (!is_uobject_valid_fast(level))
        {
            return nullptr;
        }
        auto* world = try_read_reflected_object_property_value(level, L"OwningWorld");
        if (!is_uobject_valid_fast(world))
        {
            return nullptr;
        }
        auto* instance = try_read_reflected_object_property_value(world, L"OwningGameInstance");
        if (!is_uobject_valid_fast(instance))
        {
            return nullptr;
        }
        auto* manager = try_read_reflected_object_property_value(instance, L"m_UIManager");
        return is_uobject_valid_fast(manager) ? manager : nullptr;
    }

    LoadingScreenSnapshot loading_screen_snapshot(uintptr_t base, UObject* manager)
    {
        LoadingScreenSnapshot out{};
        if (!is_uobject_valid_fast(manager))
        {
            return out;
        }
        if (!property_offset(manager, L"m_LoadingScreen",
                             startup_store == AutoloadStore::steam ? steam_loading_screen_offset
                                                                   : egs_loading_screen_offset))
        {
            out.layout_mismatch = true;
            return out;
        }
        out.object = try_read_reflected_object_property_value(manager, L"m_LoadingScreen");
        if (!out.object)
        {
            // Same absent-screen result as TresUI_IsClosedLoadingScreen.
            out.known = out.closed = true;
            return out;
        }
        if (!is_uobject_valid_fast(out.object))
        {
            return out;
        }
        if (read_field<uintptr_t>(out.object, 0) != base + native_rva(loading_screen_vtable_rva) ||
            !property_offset(out.object, L"m_pUIManager", loading_screen_ui_manager_offset) ||
            !property_offset(out.object, L"m_pMoviePlayer", loading_screen_movie_player_offset) ||
            !property_offset(out.object, L"m_gfxObj_extraLoading",
                             loading_screen_extra_object_offset) ||
            try_read_reflected_object_property_value(out.object, L"m_pUIManager") != manager)
        {
            out.layout_mismatch = true;
            return out;
        }
        const auto open =
            read_field<uint8_t>(out.object, native_ui::panel_open_offset, unknown_native_flag_byte);
        const auto closed =
            read_field<uint8_t>(out.object, loading_screen_closed_offset, unknown_native_flag_byte);
        out.mode = read_field<int32_t>(out.object, loading_screen_mode_offset, -1);
        const auto progress = read_field<float>(out.object, loading_screen_progress_offset, -1.0f);
        out.progress = std::isfinite(progress) ? progress : -1.0f;
        out.known =
            open <= 1 && closed <= 1 && out.mode >= 0 && out.mode <= maximum_loading_screen_mode;
        out.open = open == 1;
        out.closed = out.known && closed == 1;
        return out;
    }

    struct NativeTryLock
    {
        ::CRITICAL_SECTION* section{};

        explicit NativeTryLock(uintptr_t address)
        {
            ::CRITICAL_SECTION readable{};
            if (read_native(address, readable) &&
                TryEnterCriticalSection(reinterpret_cast<::CRITICAL_SECTION*>(address)))
            {
                section = reinterpret_cast<::CRITICAL_SECTION*>(address);
            }
        }

        ~NativeTryLock()
        {
            if (section)
            {
                LeaveCriticalSection(section);
            }
        }

        NativeTryLock(const NativeTryLock&) = delete;
        NativeTryLock& operator=(const NativeTryLock&) = delete;
    };

    LoadingTransition loading_transition(uintptr_t base, bool allow_continue)
    {
        LoadingTransition out{};
        // These are native shared-pointer singletons, not UObjects. Resolve
        // them anew each tick and inspect their task map under its own lock.
        void* operation{};
        void* manager{};
        if (!read_native(base + native_rva(loading_operation_rva), operation) || !operation ||
            !read_native(base + native_rva(loading_manager_rva), manager) || !manager)
        {
            return out;
        }
        if (read_field<uintptr_t>(operation, 0) !=
                base + native_rva(loading_operation_vtable_rva) ||
            read_field<uintptr_t>(manager, 0) != base + native_rva(loading_manager_vtable_rva))
        {
            out.layout_mismatch = true;
            return out;
        }
        const auto active = read_field<uint8_t>(operation, loading_operation_active_offset,
                                                unknown_native_flag_byte);
        const auto manual = read_field<uint8_t>(operation, loading_operation_manual_offset,
                                                unknown_native_flag_byte);
        const auto ready = read_field<uint8_t>(operation, loading_operation_ready_offset,
                                               unknown_native_flag_byte);
        auto* tasks = read_field<void*>(operation, loading_operation_tasks_offset);
        auto* tasks_owner = read_field<void*>(operation, loading_operation_tasks_owner_offset);
        if (active > 1 || manual > 1 || ready > 1 || !tasks ||
            read_field<uintptr_t>(tasks_owner, 0) !=
                base + native_rva(loading_tasks_owner_vtable_rva) ||
            read_field<void*>(tasks_owner, loading_tasks_owner_data_offset) != tasks ||
            read_field<int32_t>(tasks_owner, loading_tasks_owner_count_offset) <= 0)
        {
            out.layout_mismatch = true;
            return out;
        }
        NativeTryLock tasks_lock(reinterpret_cast<uintptr_t>(tasks) + loading_tasks_lock_offset);
        if (!tasks_lock.section)
        {
            return out;
        }
        const auto count = read_field<int32_t>(tasks, loading_tasks_count_offset, -1);
        const auto free_count = read_field<int32_t>(tasks, loading_tasks_free_count_offset, -1);
        const auto queued_count = read_field<int32_t>(tasks, loading_tasks_queued_count_offset, -1);
        const auto queued_free =
            read_field<int32_t>(tasks, loading_tasks_queued_free_count_offset, -1);
        if (count < 0 || count > max_loading_completion_callbacks || free_count < 0 ||
            free_count > count || queued_count < 0 ||
            queued_count > max_loading_completion_callbacks || queued_free < 0 ||
            queued_free > queued_count)
        {
            out.layout_mismatch = true;
            return out;
        }
        void* native_task{};
        void* presentation{};
        {
            NativeTryLock manager_lock(reinterpret_cast<uintptr_t>(manager) +
                                       loading_manager_lock_offset);
            if (!manager_lock.section)
            {
                return out;
            }
            native_task = read_field<void*>(manager, loading_manager_native_task_offset);
            presentation = read_field<void*>(manager, loading_manager_presentation_offset);
            out.complete = !active && !native_task && !presentation && count == free_count &&
                           queued_count == queued_free;
            if (!active || !manual || !ready || !native_task)
            {
                return out;
            }
            if (read_field<uintptr_t>(native_task, 0) !=
                base + native_rva(loading_native_task_vtable_rva))
            {
                out.layout_mismatch = true;
                return out;
            }
            // KH3's normal Continue predicate waits for this native loading
            // thread to finish and for the prompt animation's ready step.
            out.prompt_ready =
                read_field<int32_t>(native_task, loading_native_task_state_offset, -1) == 0 &&
                read_field<uint8_t>(native_task, loading_native_task_prompt_ready_offset,
                                    unknown_native_flag_byte) == 1;
        }
        if (!allow_continue || !out.prompt_ready || count - free_count != 1 ||
            queued_count != queued_free)
        {
            return out;
        }
        // Locate only the live one-shot Continue task, using the sparse-map
        // allocation bits and its exact bound predicate/action. No heap scan
        // or input-state mutation; never consume a different loading task.
        auto* entries = read_field<const std::byte*>(tasks, 0);
        auto* bits = read_field<const std::byte*>(tasks, loading_tasks_allocation_bits_offset);
        if (!bits)
        {
            bits = reinterpret_cast<const std::byte*>(tasks) + loading_tasks_inline_bits_offset;
        }
        if (!entries ||
            read_field<int32_t>(tasks, loading_tasks_allocation_count_offset, -1) != count)
        {
            out.layout_mismatch = true;
            return out;
        }
        for (int i = 0; i < count; ++i)
        {
            uint32_t allocated{};
            if (!read_native(reinterpret_cast<uintptr_t>(bits) +
                                 (i / loading_allocation_word_bits) * sizeof(uint32_t),
                             allocated))
            {
                return out;
            }
            if (!(allocated & (uint32_t{1} << (i % loading_allocation_word_bits))))
            {
                continue;
            }
            const auto* entry = entries + i * completion_entry_stride_bytes;
            auto* task = read_field<void*>(entry, loading_completion_task_offset);
            const auto id = read_field<int32_t>(entry, 0, -1);
            if (read_field<uintptr_t>(task, 0) !=
                    base + native_rva(loading_completion_task_vtable_rva) ||
                id < 1 || read_field<int32_t>(task, completion_task_id_offset, -1) != id ||
                read_field<int32_t>(task, completion_task_status_offset, -1) != 0 ||
                read_field<uint8_t>(task, completion_task_guard_byte_offset,
                                    unknown_native_flag_byte) != 0)
            {
                return out;
            }
            auto* predicate = read_field<void*>(task, completion_predicate_offset);
            const void* action_delegate =
                reinterpret_cast<const std::byte*>(task) + completion_action_delegate_offset;
            if (startup_store == AutoloadStore::steam)
            {
                if (read_field<int32_t>(task, steam_completion_delegate_count_offset) != 1)
                {
                    return out;
                }
                if (auto* heap_list =
                        read_field<void*>(task, steam_completion_delegate_list_offset))
                {
                    action_delegate = heap_list;
                }
            }
            auto* action = read_field<void*>(action_delegate, completion_action_offset);
            if (read_field<int32_t>(task, completion_predicate_kind_offset) !=
                    completion_delegate_kind ||
                read_field<int32_t>(action_delegate, completion_action_kind_offset) !=
                    completion_delegate_kind ||
                read_field<uintptr_t>(predicate, 0) !=
                    base + native_rva(loading_predicate_vtable_rva) ||
                read_field<uintptr_t>(action, 0) != base + native_rva(loading_action_vtable_rva) ||
                read_field<void*>(predicate, completion_delegate_context_offset) != operation ||
                read_field<void*>(action, completion_delegate_context_offset) != operation ||
                read_field<uintptr_t>(predicate, completion_delegate_function_offset) !=
                    base + native_rva(loading_continue_predicate_rva) ||
                read_field<uintptr_t>(action, completion_delegate_function_offset) !=
                    base + native_rva(continue_loading_rva))
            {
                return out;
            }
            // Run the same completion action as normal Continue: animated
            // NowLoading::Close(false), then release the native loading
            // thread/presentation. Retire its task with the same native
            // remover the scheduler uses, so it cannot execute twice.
            using Continue = void(__fastcall*)(void*);
            using Retire = int32_t(__fastcall*)(void*, int32_t);
            reinterpret_cast<Continue>(base + native_rva(continue_loading_rva))(operation);
            out.dispatched = true;
            out.retire_failed = reinterpret_cast<Retire>(
                                    base + native_rva(retire_loading_task_rva))(tasks, id) != 1;
            return out;
        }
        return out;
    }

} // namespace kh3ap::loading_runtime
