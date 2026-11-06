#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/treasure/chest_persistence_service.h"
#include "kh3ap/features/treasure/chest_open_events.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <sstream>
#include <map>
#include <set>
#include <atomic>

#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/checks/check_checkpoint.h"
#include "kh3ap/bridge/catalog/catalog.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/features/kh02/treasure/kh02_chest_persistence.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/conditional_profile_scope.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/NameTypes.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <system_error>
#include <utility>

namespace kh3ap
{
    namespace
    {
        constexpr auto steam_treasure_bit_getter_rva = 0xe8f550;
        constexpr auto steam_treasure_bit_setter_rva = 0xe90bf0;

        constexpr int chest_id_property_offset = 0x6c0;
        constexpr uintptr_t chest_getter_vtable_byte_offset = 0x990;
        constexpr int maximum_chest_pe_header_offset = 0x1000;

        constexpr auto max_native_chest_index = 4095;
        constexpr auto max_chest_call_parameter_bytes = 4096;
        constexpr auto max_configured_chest_names = 4096;
        constexpr auto native_chest_index_offset = 0x6c8;
        constexpr auto native_chest_setter_vtable_offset = 0x998;
    } // namespace

    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        bool chest_setter_hooked{}, chest_getter_hooked{};
        thread_local bool restoring_chest{};
        std::atomic<NativeChestOpenObserver> chest_open_observer{};
    } // namespace

    void set_native_chest_open_observer(NativeChestOpenObserver observer) noexcept
    {
        chest_open_observer.store(observer, std::memory_order_release);
    }

    template <class T> bool chest_read(uintptr_t address, T& value) noexcept
    {
        __try
        {
            std::memcpy(&value, reinterpret_cast<void*>(address), sizeof(T));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool is_fake_chest(UObject* chest)
    {
        if (!is_uobject_valid_fast(chest))
        {
            return false;
        }
        auto* property = chest->GetPropertyByNameInChain(L"m_bIsFakeBox");
        return property && property->IsA<FBoolProperty>() &&
               static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(chest);
    }

    bool reconcile_chest_record(UObject* chest, bool repair)
    {
        // Supported EGS and Steam builds. Full method signatures, reflected identity,
        // and both vtable targets must match before a native call.
        static constexpr uint8_t getter[] = {
            0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x83, 0xb9, 0xc8, 0x06, 0x00, 0x00, 0xff, 0x48,
            0x8b, 0xd9, 0x7e, 0x45, 0x48, 0x8b, 0x01, 0xff, 0x90, 0xc0, 0x08, 0x00, 0x00, 0x4c,
            0x8b, 0xc0, 0x48, 0x85, 0xc0, 0x74, 0x34, 0x8b, 0x83, 0xc8, 0x06, 0x00, 0x00, 0x3d,
            0xff, 0x0f, 0x00, 0x00, 0x77, 0x27, 0x99, 0x83, 0xe2, 0x1f, 0x03, 0xc2, 0x8b, 0xc8,
            0x83, 0xe0, 0x1f, 0xc1, 0xf9, 0x05, 0x2b, 0xc2, 0x48, 0x63, 0xc9, 0x41, 0x8b, 0x8c,
            0x88, 0x84, 0x36, 0x01, 0x00, 0x0f, 0xa3, 0xc1, 0x0f, 0x92, 0xc0, 0x48, 0x83, 0xc4,
            0x20, 0x5b, 0xc3, 0x32, 0xc0, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3};
        static constexpr uint8_t setter[] = {
            0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x83, 0xb9, 0xc8, 0x06, 0x00, 0x00, 0xff, 0x48,
            0x8b, 0xd9, 0x7e, 0x44, 0x48, 0x8b, 0x01, 0xff, 0x90, 0xc0, 0x08, 0x00, 0x00, 0x4c,
            0x8b, 0xc0, 0x48, 0x85, 0xc0, 0x74, 0x33, 0x8b, 0x83, 0xc8, 0x06, 0x00, 0x00, 0x3d,
            0xff, 0x0f, 0x00, 0x00, 0x77, 0x26, 0x99, 0x83, 0xe2, 0x1f, 0x03, 0xc2, 0x8b, 0xc8,
            0x83, 0xe0, 0x1f, 0x2b, 0xc2, 0xc1, 0xf9, 0x05, 0x48, 0x63, 0xc9, 0x49, 0x8d, 0x14,
            0x88, 0x8b, 0xc8, 0xb8, 0x01, 0x00, 0x00, 0x00, 0xd3, 0xe0, 0x09, 0x82, 0x84, 0x36,
            0x01, 0x00, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3};
        static constexpr uint8_t steam_chest_getter[] = {
            0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x83, 0xb9, 0xc8, 0x06, 0x00, 0x00, 0xff,
            0x48, 0x8b, 0xd9, 0x7e, 0x25, 0x48, 0x8b, 0x01, 0xff, 0x90, 0xc0, 0x08, 0x00,
            0x00, 0x48, 0x85, 0xc0, 0x74, 0x17, 0x8b, 0x93, 0xc8, 0x06, 0x00, 0x00, 0x48,
            0x8d, 0x88, 0x80, 0x32, 0x01, 0x00, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xe9, 0x49,
            0x89, 0xed, 0xff, 0x32, 0xc0, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3};
        static constexpr uint8_t steam_chest_setter[] = {
            0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x83, 0xb9, 0xc8, 0x06, 0x00, 0x00, 0xff,
            0x48, 0x8b, 0xd9, 0x7e, 0x25, 0x48, 0x8b, 0x01, 0xff, 0x90, 0xc0, 0x08, 0x00,
            0x00, 0x48, 0x85, 0xc0, 0x74, 0x17, 0x8b, 0x93, 0xc8, 0x06, 0x00, 0x00, 0x48,
            0x8d, 0x88, 0x80, 0x32, 0x01, 0x00, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xe9, 0xa9,
            0x9f, 0xed, 0xff, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3};
        static constexpr uint8_t steam_bit_getter[] = {
            0x4c, 0x8b, 0xc9, 0x81, 0xfa, 0xff, 0x0f, 0x00, 0x00, 0x77, 0x26, 0x8b, 0xc2,
            0x99, 0x83, 0xe2, 0x1f, 0x03, 0xc2, 0x44, 0x8b, 0xc0, 0x83, 0xe0, 0x1f, 0x41,
            0xc1, 0xf8, 0x05, 0x2b, 0xc2, 0x49, 0x63, 0xc8, 0x41, 0x8b, 0x8c, 0x89, 0x04,
            0x04, 0x00, 0x00, 0x0f, 0xa3, 0xc1, 0x0f, 0x92, 0xc0, 0xc3, 0x32, 0xc0, 0xc3};
        static constexpr uint8_t steam_bit_setter[] = {
            0x4c, 0x8b, 0xc9, 0x81, 0xfa, 0xff, 0x0f, 0x00, 0x00, 0x77, 0x2a, 0x8b, 0xc2, 0x99,
            0x83, 0xe2, 0x1f, 0x03, 0xc2, 0x44, 0x8b, 0xc0, 0x83, 0xe0, 0x1f, 0x2b, 0xc2, 0x41,
            0xc1, 0xf8, 0x05, 0x49, 0x63, 0xc8, 0x49, 0x8d, 0x14, 0x89, 0x8b, 0xc8, 0xb8, 0x01,
            0x00, 0x00, 0x00, 0xd3, 0xe0, 0x09, 0x82, 0x04, 0x04, 0x00, 0x00, 0xc3};
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        if (!chest_read(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
            dos.e_lfanew >= maximum_chest_pe_header_offset ||
            !chest_read(base + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        {
            throw std::runtime_error("chest_record_unsupported_build");
        }
        const auto verify_signature = [base](uintptr_t rva, const auto& signature)
        {
            for (size_t i = 0; i < sizeof(signature); ++i)
            {
                uint8_t byte{};
                if (!chest_read(base + rva + i, byte) || byte != signature[i])
                {
                    throw std::runtime_error("chest_record_native_signature");
                }
            }
        };
        uintptr_t getter_rva{}, setter_rva{};
        if (kh3ap::native_build::egs.matches(nt.FileHeader.TimeDateStamp,
                                             nt.OptionalHeader.SizeOfImage))
        {
            getter_rva = 0x123E160;
            setter_rva = 0x123E1C0;
            if (!chest_getter_hooked)
            {
                verify_signature(getter_rva, getter);
            }
            if (!chest_setter_hooked)
            {
                verify_signature(setter_rva, setter);
            }
        }
        else if (kh3ap::native_build::steam.matches(nt.FileHeader.TimeDateStamp,
                                                    nt.OptionalHeader.SizeOfImage))
        {
            getter_rva = 0xFB6BD0;
            setter_rva = 0xFB6C10;
            if (!chest_getter_hooked)
            {
                verify_signature(getter_rva, steam_chest_getter);
            }
            if (!chest_setter_hooked)
            {
                verify_signature(setter_rva, steam_chest_setter);
            }
            // Steam tail-calls these helpers rather than inlining the bit operations.
            // Verify their complete bodies as well as the wrappers' relative jumps.
            verify_signature(steam_treasure_bit_getter_rva, steam_bit_getter);
            verify_signature(steam_treasure_bit_setter_rva, steam_bit_setter);
        }
        else
        {
            throw std::runtime_error("chest_record_unsupported_build");
        }
        auto* klass = find_loaded_runtime_asset_class(L"/Script/TresGame.TresTreasureBox");
        if (!is_uobject_a_fast(chest, klass))
        {
            throw std::runtime_error("chest_record_invalid_chest");
        }
        // Decoys use the ordinary chest class but have no treasure identity or
        // native journal index. Their puzzle script owns the interaction.
        if (is_fake_chest(chest))
        {
            return false;
        }
        auto* id = chest->GetPropertyByNameInChain(L"m_TreasureID");
        uintptr_t vt{}, get{}, set{};
        int32_t index = -1;
        const auto address = reinterpret_cast<uintptr_t>(chest);
        if (!id || id->GetOffset_Internal() != chest_id_property_offset ||
            id->GetElementSize() != sizeof(FName) || !chest_read(address, vt) ||
            !chest_read(vt + chest_getter_vtable_byte_offset, get) || get != base + getter_rva ||
            !chest_read(vt + native_chest_setter_vtable_offset, set) || set != base + setter_rva ||
            !chest_read(address + native_chest_index_offset, index) || index < 0 ||
            index > max_native_chest_index)
        {
            throw std::runtime_error("chest_record_layout_or_index");
        }
        using Get = bool(__fastcall*)(UObject*);
        const bool before = reinterpret_cast<Get>(get)(chest);
        if (repair && !before)
        {
            using Set = void(__fastcall*)(UObject*, bool);
            reinterpret_cast<Set>(set)(chest, true);
            if (!reinterpret_cast<Get>(get)(chest))
            {
                throw std::runtime_error("chest_record_readback_failed");
            }
        }
        return before;
    }

    namespace
    {
        struct ChestCall
        {
            UObject* owner;
            UFunction* fn;
            std::vector<std::byte> data;
            ChestCall(UObject* object, const wchar_t* name) : owner(object)
            {
                if (!is_uobject_valid_fast(owner))
                {
                    throw std::runtime_error("chest_call_owner");
                }
                fn = owner->GetFunctionByNameInChain(name);
                if (!is_uobject_valid_fast(fn) ||
                    fn->GetParmsSize() > max_chest_call_parameter_bytes)
                {
                    throw std::runtime_error("chest_call_function");
                }
                data.resize(std::max<int>(1, fn->GetParmsSize()));
            }
            FProperty* field(const wchar_t* name)
            {
                auto* p = find_function_param_by_name(fn, name);
                if (!p || p->GetOffset_Internal() < 0 ||
                    p->GetOffset_Internal() + p->GetElementSize() > fn->GetParmsSize())
                {
                    throw std::runtime_error("chest_call_parameters");
                }
                return p;
            }
            void run()
            {
                const RuntimeGrantInternalCallDepth::Scope scope{};
                owner->ProcessEvent(fn, data.data());
            }
            void object(const wchar_t* name, UObject* value)
            {
                if (!write_object_function_param(fn, data.data(), name, value))
                {
                    throw std::runtime_error("chest_call_object");
                }
            }
            void number(const wchar_t* name, float value)
            {
                auto* p = field(name);
                if (p->GetClass().GetName() != L"FloatProperty" ||
                    p->GetElementSize() != sizeof(float))
                {
                    throw std::runtime_error("chest_call_float");
                }
                *p->ContainerPtrToValuePtr<float>(data.data()) = value;
            }
        };
        void restore_appearance(UObject* chest)
        {
            if (is_uobject_valid_fast(
                    try_read_reflected_object_property_value(chest, L"m_pOpenner")))
            {
                return;
            }
            auto* reactor = try_read_reflected_object_property_value(chest, L"MyReactor");
            ChestCall off(reactor, L"BP_DeactivateGimmickComponent");
            off.run();
            ChestCall play(chest, L"BP_AnimPlayAnimset");
            if (!write_name_or_string_function_param(play.field(L"InAnimName"), play.data.data(),
                                                     L"m_002") ||
                !write_name_or_string_function_param(play.field(L"InSlotName"), play.data.data(),
                                                     L"None"))
            {
                throw std::runtime_error("chest_animation_name");
            }
            play.number(L"InBlendInTime", 0.f);
            play.number(L"InPlayRate", 0.f);
            play.run();
            auto* mesh = try_read_reflected_object_property_value(chest, L"MyMesh");
            ChestCall anim(mesh, L"GetAnimInstance");
            anim.run();
            auto* instance =
                try_read_function_object_param(anim.fn, anim.data.data(), L"ReturnValue");
            ChestCall montage(instance, L"GetCurrentActiveMontage");
            montage.run();
            auto* animation =
                try_read_function_object_param(montage.fn, montage.data.data(), L"ReturnValue");
            if (!is_uobject_valid_fast(animation))
            {
                throw std::runtime_error("chest_open_animation_missing");
            }
            auto* length = animation->GetPropertyByNameInChain(L"SequenceLength");
            if (!length || length->GetClass().GetName() != L"FloatProperty" ||
                length->GetElementSize() != sizeof(float))
            {
                throw std::runtime_error("chest_animation_length");
            }
            ChestCall pose(mesh, L"OverrideAnimationData");
            pose.object(L"InAnimToPlay", animation);
            pose.number(L"Position", *length->ContainerPtrToValuePtr<float>(animation));
            pose.number(L"PlayRate", 0.f);
            pose.run();
        }
    } // namespace
    bool restore_recorded_chest_appearance(UObject* chest)
    {
        if (!reconcile_chest_record(chest, false) ||
            is_uobject_valid_fast(try_read_reflected_object_property_value(chest, L"m_pOpenner")))
        {
            return false;
        }
        restore_appearance(chest);
        return true;
    }

    struct RuntimeChestPersistenceService::Impl
    {
        inline static Impl* instance{};
        std::unique_ptr<PLH::x64Detour> hook, getter_hook;
        std::mutex completed_mutex;
        uint64_t getter_original{};
        uint64_t original{}, generation{}, restores{};
        std::optional<std::filesystem::path> bridge;
        std::wstring seed;
        int64_t slot{};
        std::string config_text;
        std::map<uint64_t, std::wstring> names;
        std::set<std::wstring> completed;
        ChestOpenEvents inbox;
        std::function<void()> on_completion;
        bool initialized{}, dirty{}, failed{}, restore_requested{};
        explicit Impl(std::function<void()> callback) : on_completion(std::move(callback))
        {
            instance = this;
        }
        ~Impl()
        {
            if (getter_hook)
            {
                getter_hook->unHook();
            }
            if (hook)
            {
                hook->unHook();
            }
            chest_setter_hooked = false;
            chest_getter_hooked = false;
            if (instance == this)
            {
                instance = nullptr;
            }
        }
        static void opened(UObject* chest)
        {
            auto& s = *instance;
            reinterpret_cast<void (*)(UObject*)>(s.original)(chest);
            if (restoring_chest)
            {
                return;
            }
            if (auto observer = chest_open_observer.load(std::memory_order_acquire))
            {
                observer(chest);
            }
            uint64_t name{};
            int32_t index = -1;
            if (chest_read(reinterpret_cast<uintptr_t>(chest) + chest_id_property_offset, name) &&
                chest_read(reinterpret_cast<uintptr_t>(chest) + native_chest_index_offset, index) &&
                index >= 0 && index <= max_native_chest_index)
            {
                s.inbox.write(name);
            }
        }
        static bool was_opened(UObject* chest)
        {
            auto& s = *instance;
            const bool native = reinterpret_cast<bool (*)(UObject*)>(s.getter_original)(chest);
            if (native)
            {
                return true;
            }
            uint64_t raw{};
            int32_t index = -1;
            if (!chest_read(reinterpret_cast<uintptr_t>(chest) + chest_id_property_offset, raw) ||
                !chest_read(reinterpret_cast<uintptr_t>(chest) + native_chest_index_offset,
                            index) ||
                index < 0 || index > max_native_chest_index)
            {
                return false;
            }
            // The engine asks this while initializing each chest, including
            // later streamed actors. Restore the authoritative bit before the
            // engine chooses its pose/reactor state; no discovery timer needed.
            std::lock_guard lock(s.completed_mutex);
            const auto name = s.names.find(raw);
            if (name == s.names.end() || !s.completed.contains(name->second))
            {
                return false;
            }
            reinterpret_cast<void (*)(UObject*)>(s.original)(chest);
            return reinterpret_cast<bool (*)(UObject*)>(s.getter_original)(chest);
        }
        void install()
        {
            if (hook)
            {
                return;
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                throw std::runtime_error("chest_image");
            }
            const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            const bool egs = kh3ap::native_build::egs.matches(pe->FileHeader.TimeDateStamp,
                                                              pe->OptionalHeader.SizeOfImage);
            const bool steam = kh3ap::native_build::steam.matches(pe->FileHeader.TimeDateStamp,
                                                                  pe->OptionalHeader.SizeOfImage);
            if (!egs && !steam)
            {
                throw std::runtime_error("chest_unsupported_build");
            }
            const uintptr_t rva = egs ? 0x123e1c0 : 0xfb6c10;
            const uint8_t signature[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x83, 0xb9, 0xc8,
                                         0x06, 0,    0,    0xff, 0x48, 0x8b, 0xd9, 0x7e};
            DWORD64 image{};
            auto* unwind = RtlLookupFunctionEntry(base + rva, &image, nullptr);
            if (!unwind || image != base || unwind->BeginAddress != rva ||
                std::memcmp(reinterpret_cast<void*>(base + rva), signature, sizeof(signature)))
            {
                throw std::runtime_error("chest_setter_signature");
            }
            hook = std::make_unique<PLH::x64Detour>(base + rva, reinterpret_cast<uint64_t>(&opened),
                                                    &original);
            if (!hook->hook() || !original)
            {
                hook.reset();
                throw std::runtime_error("chest_hook_failed");
            }
            chest_setter_hooked = true;
            const uintptr_t get_rva = egs ? 0x123e160 : 0xfb6bd0;
            auto* get_unwind = RtlLookupFunctionEntry(base + get_rva, &image, nullptr);
            if (!get_unwind || image != base || get_unwind->BeginAddress != get_rva ||
                std::memcmp(reinterpret_cast<void*>(base + get_rva), signature, sizeof(signature)))
            {
                throw std::runtime_error("chest_getter_signature");
            }
            getter_hook = std::make_unique<PLH::x64Detour>(
                base + get_rva, reinterpret_cast<uint64_t>(&was_opened), &getter_original);
            if (!getter_hook->hook() || !getter_original)
            {
                getter_hook.reset();
                throw std::runtime_error("chest_getter_hook_failed");
            }
            chest_getter_hooked = true;
            Output::send<LogLevel::Warning>(
                L"[KH3APProbeCpp] NativeChestTracking event_hook=installed storefront={}\n",
                egs ? L"egs" : L"steam");
        }
        void persist()
        {
            std::ostringstream out;
            out << "{\"schema\":\"kh3ap.chest_progress.v1\",\"seed_name\":\"" << json_escape(seed)
                << "\",\"slot\":" << slot << ",\"completed\":[";
            bool first = true;
            for (const auto& id : completed)
            {
                if (!first)
                {
                    out << ',';
                }
                first = false;
                out << '"' << json_escape(id) << '"';
            }
            out << "],\"open_events\":" << inbox.events() << ",\"restore_events\":" << restores
                << "}\n";
            if (!write_text_file_atomic_verified(*bridge / L"chest_progress.json", out.str()))
            {
                throw std::runtime_error("chest_checkpoint_write");
            }
            dirty = false;
            if (!write_text_file_atomic_verified(*bridge / channels::runtime_chest_checks_file,
                                                 out.str()))
            {
                throw std::runtime_error("chest_publish");
            }
        }
        void configure(const std::filesystem::path& path, const std::string& text)
        {
            if (initialized && bridge == path && config_text == text)
            {
                return;
            }
            if (dirty)
            {
                persist();
            }
            const auto setup = native_catalog::check_setup(text, native_catalog::CheckKind::Chest);
            const auto& next_seed = setup.seed_name;
            const auto next_slot = setup.slot;
            std::map<uint64_t, std::wstring> next_names;
            std::set<uint64_t> watched;
            for (const auto& row : setup.rows)
            {
                const auto& id = row.source_id;
                FName name(id, FNAME_Add);
                uint64_t raw;
                std::memcpy(&raw, &name, sizeof(raw));
                if (next_names.contains(raw))
                {
                    throw std::runtime_error("chest_config_duplicate");
                }
                next_names[raw] = id;
                watched.insert(raw);
            }
            // Imported treasure actors use the same journal getter/setter and
            // durable ledger, including chests outside the AP location pool.
            for (const auto& group : kh02_chest_journal::groups)
            {
                for (std::size_t index = 0; index < group.count; ++index)
                {
                    const auto id = group.identities[index];
                    FName name(id.data(), FNAME_Add);
                    uint64_t raw{};
                    std::memcpy(&raw, &name, sizeof(raw));
                    next_names.emplace(raw, id);
                    watched.insert(raw);
                }
            }
            if (next_names.size() > max_configured_chest_names)
            {
                throw std::runtime_error("chest_config_count");
            }
            auto next_completed = load_check_checkpoint(
                path / L"chest_progress.json", L"kh3ap.chest_progress.v1", next_seed, next_slot);
            const auto binding = reconciliation::view();
            if (binding->seed == next_seed && binding->slot == next_slot)
            {
                for (const auto& group : kh02_chest_journal::groups)
                {
                    for (std::size_t index = 0; index < group.count; ++index)
                    {
                        const auto id = group.identities[index];
                        const std::string flag =
                            "gameflow_DW_CHALLENGE_CHEST_" + kh02_chest_source_id(id);
                        int value{};
                        if (binding->kh02_progress)
                        {
                            const auto& flags = binding->kh02_progress->flags;
                            if (const auto found = flags.find(flag); found != flags.end())
                            {
                                value = found->second;
                            }
                        }
                        if (kh02_chest_collected(id, value, binding->completed_checks))
                        {
                            next_completed.emplace(id);
                        }
                    }
                }
            }
            install();
            bridge = path;
            seed = next_seed;
            slot = next_slot;
            {
                std::lock_guard lock(completed_mutex);
                names = std::move(next_names);
                completed = std::move(next_completed);
            }
            inbox.configure(std::move(watched));
            config_text = text;
            initialized = true;
            failed = false;
            dirty = true;
            restore_requested = true;
            persist();
            Output::send<LogLevel::Warning>(L"[KH3APProbeCpp] NativeChestTracking configured "
                                            L"checks={} completed={} mode=events\n",
                                            names.size(), completed.size());
        }
        bool record(const std::wstring& id)
        {
            std::lock_guard lock(completed_mutex);
            if (completed.insert(id).second)
            {
                dirty = true;
                // Runs in the game-thread drain after the native setter returned.
                if (on_completion)
                {
                    on_completion();
                }
                Output::send<LogLevel::Warning>(
                    L"[KH3APProbeCpp] NativeChestTracking completed={} source=open_event\n", id);
                return true;
            }
            return false;
        }
        void restore(UObject* pawn)
        {
            ++restores;
            auto* klass = find_loaded_runtime_asset_class(L"/Script/TresGame.TresTreasureBox");
            if (!is_uobject_valid_fast(klass))
            {
                throw std::runtime_error("chest_class");
            }
            std::vector<AActor*> actors;
            UGameplayStatics::GetAllActorsOfClass(pawn, klass, actors);
            for (auto* chest : actors)
            {
                if (!is_uobject_valid_fast(chest) ||
                    chest->HasAnyFlags(static_cast<EObjectFlags>(RF_NeedLoad | RF_NeedPostLoad |
                                                                 RF_NeedPostLoadSubobjects)))
                {
                    continue;
                }
                uint64_t raw{};
                if (!chest_read(reinterpret_cast<uintptr_t>(chest) + chest_id_property_offset,
                                raw) ||
                    !names.contains(raw))
                {
                    continue;
                }
                const auto& id = names.at(raw);
                // A load/spawn event reconciles the live vanilla record once;
                // no save files, repeated scans, or presentation-based inference.
                if (!completed.contains(id))
                {
                    if (reconcile_chest_record(chest, false))
                    {
                        record(id);
                    }
                }
                if (completed.contains(id))
                {
                    struct Guard
                    {
                        Guard()
                        {
                            restoring_chest = true;
                        }
                        ~Guard()
                        {
                            restoring_chest = false;
                        }
                    } guard;
                    reconcile_chest_record(chest, true);
                    restore_appearance(chest);
                }
            }
            dirty = true;
        }
        void process(RuntimeState& state, const std::optional<std::filesystem::path>& path)
        {
            const auto current = pipe::input_generation();
            const bool input = current != generation;
            generation = current;
            if (!input && !inbox.pending() && !restore_requested)
            {
                return;
            }
            try
            {
                if (path && (input || !initialized))
                {
                    if (!initialized || bridge != path)
                    {
                        if (auto text = read_text_file(*path / L"slot_data.json"))
                        {
                            configure(*path, *text);
                        }
                    }
                }
                if (!initialized || failed)
                {
                    restore_requested = false;
                    inbox.take();
                    return;
                }
                const auto opened = inbox.take();
                // Drain after the native setter returns. Unwatched non-AP
                // chests still invalidate both map views without entering this ledger.
                if (opened.changed && on_completion)
                {
                    on_completion();
                }
                for (auto name : opened.checks)
                {
                    record(names.at(name));
                }
                if (restore_requested)
                {
                    restore_requested = false;
                    auto& candidate = state.grant_state().pawn_state();
                    auto* pawn = candidate.object();
                    if (candidate.candidate_trusted() && is_uobject_valid_fast(pawn) &&
                        !state.grant_state().special_battle_mode_active())
                    {
                        restore(pawn);
                    }
                }
                if (dirty)
                {
                    persist();
                }
            }
            catch (const std::exception& error)
            {
                failed = true;
                restore_requested = false;
                Output::send<LogLevel::Error>(
                    L"[KH3APProbeCpp] NativeChestTracking stopped reason={}\n",
                    std::wstring(error.what(), error.what() + std::strlen(error.what())));
            }
        }
    };
    bool native_chest_was_opened(std::wstring_view id)
    {
        auto* s = RuntimeChestPersistenceService::Impl::instance;
        if (!s || !s->initialized || s->failed)
        {
            throw std::runtime_error("chest_progress_unavailable");
        }
        return s->completed.contains(std::wstring(id));
    }
    void record_native_chest(std::wstring_view id)
    {
        auto* s = RuntimeChestPersistenceService::Impl::instance;
        if (!s || !s->initialized || s->failed)
        {
            throw std::runtime_error("chest_progress_unavailable");
        }
        FName name(id, FNAME_Add);
        uint64_t raw;
        std::memcpy(&raw, &name, sizeof(raw));
        if (!s->names.contains(raw))
        {
            throw std::runtime_error("chest_not_in_seed");
        }
        if (s->record(std::wstring(id)))
        {
            s->persist();
        }
    }
    RuntimeChestPersistenceService::RuntimeChestPersistenceService(std::function<void()> callback)
        : impl_(std::make_unique<Impl>(std::move(callback)))
    {
    }
    RuntimeChestPersistenceService::~RuntimeChestPersistenceService() = default;
    auto RuntimeChestPersistenceService::notify_treasure_changed() -> void
    {
        impl_->restore_requested = true;
        impl_->failed = false;
    }
    auto RuntimeChestPersistenceService::process(RuntimeState& state,
                                                 const std::optional<std::filesystem::path>& path,
                                                 std::chrono::steady_clock::time_point) -> void
    {
        impl_->process(state, path);
    }
} // namespace kh3ap
