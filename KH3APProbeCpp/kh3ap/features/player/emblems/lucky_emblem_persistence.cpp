#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/player/emblems/lucky_emblem_persistence.h"
#include "kh3ap/features/player/emblems/lucky_emblem_signatures.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/checks/check_checkpoint.h"
#include "kh3ap/bridge/checks/lucky_marks.h"
#include "kh3ap/hooks/memory/memory_hook_detail.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/save_owner_layout.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/AActor.hpp>
#include <UnrealDef.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <atomic>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        constexpr int maximum_emblem_pe_header_offset = 0x1000;

        constexpr auto checkpoint_name = L"lucky_emblem_progress.json";
        constexpr auto checkpoint_schema = L"kh3ap.lucky_emblem_native_indices.v1";
        constexpr uintptr_t bank_offset = 0x8d660;
        // Save data + 0x13894, with save data at owner + 0x870.
        constexpr uintptr_t reward_claim_offset = 0x14104;
        LuckyEmblemPersistence* observer_owner{};
        std::atomic<uint64_t> photo_revision_value{};

        template<class T> auto read(uintptr_t address) -> T
        {
            T value{};
            SIZE_T size{};
            if (!address || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
                    &value, sizeof(value), &size) || size != sizeof(value))
                throw std::runtime_error("emblem_native_read");
            return value;
        }

        auto add_recorded_bits(uintptr_t address, const emblem_record::Words& words) noexcept -> bool
        {
            __try
            {
                auto* bank = reinterpret_cast<uint32_t*>(address);
                for (size_t i = 0; i < words.size(); ++i) bank[i] |= words[i];
                return true;
            }
            __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        auto add_claimed_rewards(uintptr_t address, uint32_t mask) noexcept -> bool
        {
            __try
            {
                *reinterpret_cast<uint32_t*>(address) |= mask;
                return true;
            }
            __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        auto resolve_bank(UObject* pawn) -> uintptr_t
        {
            if (!is_uobject_a_fast(pawn, AActor::StaticClass())) return 0;
            auto* level = pawn->GetOuterPrivate();
            if (!is_uobject_valid_fast(level)) return 0;
            auto* world = try_read_reflected_object_property_value(level, L"OwningWorld");
            if (!is_uobject_valid_fast(world)) return 0;

            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto dos = read<IMAGE_DOS_HEADER>(base);
            if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
                dos.e_lfanew >= maximum_emblem_pe_header_offset)
            {
                throw std::runtime_error("emblem_native_image");
            }
            const auto pe = read<IMAGE_NT_HEADERS64>(base + dos.e_lfanew);
            if (pe.Signature != IMAGE_NT_SIGNATURE || pe.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
                pe.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
                throw std::runtime_error("emblem_native_image");
            std::span<const emblem_native::Signature> signatures;
            if (kh3ap::native_build::egs.matches(pe.FileHeader.TimeDateStamp, pe.OptionalHeader.SizeOfImage))
                signatures = emblem_native::egs;
            else if (kh3ap::native_build::steam.matches(pe.FileHeader.TimeDateStamp, pe.OptionalHeader.SizeOfImage))
                signatures = emblem_native::steam;
            else throw std::runtime_error("emblem_native_unsupported_build");
            for (const auto& signature : signatures)
            {
                if (std::memcmp(reinterpret_cast<void*>(base + signature.rva),
                        signature.bytes.data(), signature.bytes.size()) != 0)
                    throw std::runtime_error("emblem_native_signature");
            }

            // Same accessor used by the native getter: engine -> TresGameSingleton ->
            // save owner. Check the engine's current world before touching its bank.
            const auto accessor = base + signatures.front().rva;
            const auto global =
                accessor + native_save_owner_layout::accessor_instruction_bytes +
                read<int32_t>(accessor + native_save_owner_layout::accessor_displacement_offset);
            auto* engine = read<UObject*>(global);
            if (!is_uobject_valid_fast(engine)) return 0;
            auto* viewport = try_read_reflected_object_property_value(engine, L"GameViewport");
            if (!is_uobject_valid_fast(viewport) ||
                try_read_reflected_object_property_value(viewport, L"World") != world) return 0;
            auto* singleton = try_read_reflected_object_property_value(engine, L"GameSingleton");
            auto* singleton_class = find_loaded_runtime_asset_class(L"/Script/TresGame.TresGameSingleton");
            if (!is_uobject_a_fast(singleton, singleton_class) ||
                read<UObject*>(reinterpret_cast<uintptr_t>(engine) +
                               native_save_owner_layout::engine_singleton_offset) != singleton)
            {
                return 0;
            }
            const auto owner =
                read<uintptr_t>(reinterpret_cast<uintptr_t>(singleton) +
                                native_save_owner_layout::singleton_save_owner_offset);
            return owner ? owner + bank_offset : 0;
        }
    }

    LuckyEmblemPersistence::LuckyEmblemPersistence() = default;
    LuckyEmblemPersistence::~LuckyEmblemPersistence()
    {
        if (hook_) hook_->unHook();
        if (observer_owner == this) observer_owner = nullptr;
    }

    auto LuckyEmblemPersistence::photographed(UObject* component, bool recorded) -> void
    {
        auto* owner = observer_owner;
        if (!owner || !owner->original_) return;
        reinterpret_cast<void(*)(UObject*, bool)>(owner->original_)(component, recorded);
        // The normal camera calls this native method directly, bypassing
        // ProcessEvent. Observe after the real setter; never synthesize a photo.
        if (recorded) photo_revision_value.fetch_add(1, std::memory_order_release);
    }

    auto LuckyEmblemPersistence::photo_revision() -> uint64_t
    {
        return photo_revision_value.load(std::memory_order_acquire);
    }

    auto LuckyEmblemPersistence::install_observer() -> void
    {
        if (hook_) return;
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto dos = read<IMAGE_DOS_HEADER>(base);
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
            dos.e_lfanew >= maximum_emblem_pe_header_offset)
        {
            throw std::runtime_error("emblem_native_image");
        }
        const auto pe = read<IMAGE_NT_HEADERS64>(base + dos.e_lfanew);
        const emblem_native::Signature* signature{};
        if (pe.Signature != IMAGE_NT_SIGNATURE || pe.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            pe.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            throw std::runtime_error("emblem_native_image");
        if (kh3ap::native_build::egs.matches(pe.FileHeader.TimeDateStamp, pe.OptionalHeader.SizeOfImage))
            signature = &emblem_native::egs_record;
        else if (kh3ap::native_build::steam.matches(pe.FileHeader.TimeDateStamp, pe.OptionalHeader.SizeOfImage))
            signature = &emblem_native::steam_record;
        else throw std::runtime_error("emblem_native_unsupported_build");
        const auto target = base + signature->rva;
        DWORD64 image{};
        auto* unwind = RtlLookupFunctionEntry(target, &image, nullptr);
        if (!unwind || image != base || unwind->BeginAddress != signature->rva ||
            std::memcmp(reinterpret_cast<void*>(target), signature->bytes.data(), signature->bytes.size()))
            throw std::runtime_error("emblem_record_signature");
        auto hook = std::make_unique<PLH::x64Detour>(target,
                reinterpret_cast<uint64_t>(&photographed), &original_);
        observer_owner = this;
        if (!hook->hook() || !original_)
        {
            observer_owner = nullptr;
            throw std::runtime_error("emblem_record_hook");
        }
        hook_ = std::move(hook);
        Output::send<LogLevel::Normal>(L"[KH3APProbeCpp] LuckyEmblemPersistence native photo observer installed\n");
    }

    auto LuckyEmblemPersistence::reconcile(const std::filesystem::path& bridge,
            std::wstring_view seed, int64_t slot, UObject* pawn) -> bool
    {
        if (seed.empty() || slot <= 0) return false;
        if (!bridge_ || *bridge_ != bridge || seed_ != seed || slot_ != slot)
        {
            const auto metadata = JsonObject::read_seed(bridge / L"slot_data.json").object_field("kh3_seed");
            if (metadata.string_field("seed_name") != seed || metadata.int64_field("slot") != slot)
                return false;
            // Load before changing the active identity; invalid history is never overwritten.
            auto restored = emblem_record::from_checkpoint(load_check_checkpoint(
                    bridge / checkpoint_name, checkpoint_schema, seed, slot));
            bridge_ = bridge;
            seed_ = seed;
            slot_ = slot;
            remembered_ = restored;
        }
        const auto bank = resolve_bank(pawn);
        if (!bank) return false;
        const auto rewards = lucky_marks::view();
        if (!rewards->error.empty()) throw std::runtime_error("emblem_reward_checkpoint_invalid");
        if (!rewards->ready || rewards->bridge != bridge || rewards->seed != seed || rewards->slot != slot)
            return false;
        const auto rows = memory_hook_detail::collect_lucky_mark_native_milestone_rows();
        if (!rows) return false;
        const auto claim_mask = emblem_record::claimed_rewards(rewards->milestones, *rows);
        const auto claims = bank - bank_offset + reward_claim_offset;
        const auto claims_before = read<uint32_t>(claims);
        if ((claims_before & claim_mask) != claim_mask)
        {
            if (!add_claimed_rewards(claims, claim_mask) ||
                read<uint32_t>(claims) != (claims_before | claim_mask))
                throw std::runtime_error("emblem_reward_restore_readback");
            Output::send<LogLevel::Normal>(L"[KH3APProbeCpp] LuckyEmblemPersistence restored reward claims before={} after={}\n",
                    claims_before, claims_before | claim_mask);
        }
        const auto before = read<emblem_record::Words>(bank);
        const auto merged = emblem_record::merge(remembered_, before);
        if (merged != remembered_)
        {
            std::ostringstream out;
            out << "{\"schema\":\"kh3ap.lucky_emblem_native_indices.v1\",\"seed_name\":\""
                << json_escape(seed) << "\",\"slot\":" << slot << ",\"completed\":[";
            bool first = true;
            for (const auto& index : emblem_record::checkpoint_ids(merged))
            {
                if (!first) out << ',';
                first = false;
                out << '"' << json_escape(index) << '"';
            }
            out << "]}\n";
            if (!write_text_file_atomic_verified(bridge / checkpoint_name, out.str()))
                throw std::runtime_error("emblem_checkpoint_write");
            remembered_ = merged;
            Output::send<LogLevel::Normal>(L"[KH3APProbeCpp] LuckyEmblemPersistence committed count={}\n",
                    emblem_record::checkpoint_ids(merged).size());
        }
        const auto native_before = emblem_record::merge({}, before);
        if (native_before != merged)
        {
            // Merge only the 90 real records. Preserve the six unused native bits
            // and never touch album photos or unrelated save data. Claim restoration
            // above only retires milestones already committed by the AP worker.
            if (!add_recorded_bits(bank, merged) ||
                emblem_record::merge({}, read<emblem_record::Words>(bank)) != merged)
                throw std::runtime_error("emblem_native_restore_readback");
            Output::send<LogLevel::Normal>(L"[KH3APProbeCpp] LuckyEmblemPersistence restored before={} after={}\n",
                    emblem_record::checkpoint_ids(native_before).size(),
                    emblem_record::checkpoint_ids(merged).size());
        }
        return true;
    }
}
