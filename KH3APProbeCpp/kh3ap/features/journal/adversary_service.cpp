#include "kh3ap/core/platform/game_build.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/journal/adversary_service.h"
#include "kh3ap/features/journal/adversary_journal_catalog.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/abilities/ability_snapshot_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/save_owner_layout.h"
#include "kh3ap/core/platform/steam_parity_profile.h"
#include "kh3ap/core/conditional_profile_scope.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <atomic>
#include <cstring>
#include <sstream>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
namespace kh3ap {
namespace {
        constexpr auto max_adversary_catalog_id = 256;
        constexpr size_t adversary_counter_record_count = max_adversary_catalog_id + 1;
        constexpr size_t adversary_counter_record_stride = 4;
        constexpr uintptr_t adversary_counter_bank_offset = 0x86d8;

        // Signals completion of the game's own counter update. The KH0.2 challenge
        // observer captures a bounded weak defeat event; no filesystem work or AP
        // grants are performed inside the detour.
        std::atomic<uint64_t> counter_generation{};
        std::atomic<AdversaryDefeatObserver> defeat_observer{};
        uint64_t counter_trampoline{};
        using CounterWriter = void (*)(uintptr_t, RC::Unreal::UObject*, int32_t, int32_t,
                                       uintptr_t);
        void counter_updated(uintptr_t owner, RC::Unreal::UObject* enemy, int32_t amount,
                             int32_t mode, uintptr_t category)
        {
            reinterpret_cast<CounterWriter>(counter_trampoline)(owner, enemy, amount, mode,
                                                                category);
            if (enemy && amount > 0 && mode == 0)
            {
                counter_generation.fetch_add(1, std::memory_order_release);
                if (auto observer = defeat_observer.load(std::memory_order_acquire))
                {
                    observer(enemy, amount);
                }
            }
        }
auto native_profile()->const SteamParityNativeProfile* {
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(!base||dos->e_magic!=IMAGE_DOS_SIGNATURE)return nullptr;
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE)return nullptr;
    return steam_parity_native_profile(pe->FileHeader.TimeDateStamp,pe->OptionalHeader.SizeOfImage);
}
bool matches(uintptr_t base,const ParityCodeSite& site) {
    const auto bytes=try_read_process_memory_bytes(base+site.rva,site.bytes.size());
    return bytes && bytes->size()==site.bytes.size() && std::memcmp(bytes->data(),site.bytes.data(),site.bytes.size())==0;
}
template<class T> auto journal_read(uintptr_t address) -> std::optional<T> {
    auto bytes=try_read_process_memory_bytes(address,sizeof(T));
    if(!bytes || bytes->size()!=sizeof(T))return {};
    T value{};std::memcpy(&value,bytes->data(),sizeof(value));return value;
}
}
struct RuntimeAdversaryService::CounterHook {
    bool attempted{},installed{};
    std::unique_ptr<PLH::x64Detour> detour;
    void install() {
        if(attempted)return;
        attempted=true;
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        // Both storefronts use owner, enemy, increment, mode, category.
        // Steam's counter store is factored into a leaf; guard its caller too.
        const auto* profile=native_profile();
        if(!profile || !matches(base,profile->adversary_writer) || !matches(base,profile->adversary_store)) {
            RC::Output::send<RC::LogLevel::Warning>(L"[KH3APProbeCpp] Adversary counter hook unsupported; checks disabled (no polling fallback).\n");
            return;
        }
        detour=std::make_unique<PLH::x64Detour>(base+profile->adversary_writer.rva,
                reinterpret_cast<uint64_t>(&counter_updated),&counter_trampoline);
        installed=detour->hook();
        RC::Output::send<RC::LogLevel::Normal>(L"[KH3APProbeCpp] Adversary counter update hook installed={} storefront={}\n",installed,profile->image_size==kh3ap::native_build::steam.image_size?L"steam":L"egs");
    }
    ~CounterHook(){if(installed && detour)detour->unHook();}
};
RuntimeAdversaryService::RuntimeAdversaryService(
        std::function<std::optional<std::filesystem::path>()> bridge,
        std::function<RC::Unreal::UObject*()> sora)
    :bridge_(std::move(bridge)),sora_(std::move(sora)),hook_(std::make_unique<CounterHook>()){}
RuntimeAdversaryService::~RuntimeAdversaryService()=default;
auto read_adversary_revision() -> uint64_t {return counter_generation.load(std::memory_order_acquire);}
void set_adversary_defeat_observer(AdversaryDefeatObserver observer) noexcept {defeat_observer.store(observer,std::memory_order_release);}
auto read_adversary_defeats() -> std::optional<std::set<std::wstring>> {
    const RuntimeConditionalProfileScope<(KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0)> timing{L"adversaries:counter_read"};
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* profile=native_profile();
    if(!profile || !matches(base,profile->save_accessor) || !matches(base,profile->counter_layout) ||
            !matches(base,profile->adversary_store))return {};
    const auto accessor = try_read_process_memory_bytes(
        base + profile->save_accessor.rva, native_save_owner_layout::accessor_instruction_bytes);
    if (!accessor || accessor->size() != native_save_owner_layout::accessor_instruction_bytes)
    {
        return {};
    }
    int32_t displacement{};
    std::memcpy(&displacement,
                accessor->data() + native_save_owner_layout::accessor_displacement_offset,
                sizeof(displacement));
    auto global = journal_read<uintptr_t>(base + profile->save_accessor.rva +
                                          native_save_owner_layout::accessor_instruction_bytes +
                                          displacement);
    if (!global || !*global)
    {
        return {};
    }
    auto singleton = journal_read<RC::Unreal::UObject*>(
        *global + native_save_owner_layout::engine_singleton_offset);
    if(!singleton || !is_uobject_valid_fast(*singleton) || (*singleton)->GetClassPrivate()->GetName()!=L"TresGameSingleton")return {};
    auto owner = journal_read<uintptr_t>(reinterpret_cast<uintptr_t>(*singleton) +
                                         native_save_owner_layout::singleton_save_owner_offset);
    if (!owner || !*owner)
    {
        return {};
    }
    auto records = try_read_process_memory_bytes(*owner + adversary_counter_bank_offset,
                                                 adversary_counter_record_count *
                                                     adversary_counter_record_stride);
    if (!records ||
        records->size() != adversary_counter_record_count * adversary_counter_record_stride)
    {
        return {};
    }
    std::set<std::wstring> result;
    for(const auto& entry:adversary_journal_identities) {
        bool killed=false;
        for(int i=0;i<entry.count;++i) {
            const auto id=entry.ids[i];if(id<1 || id>max_adversary_catalog_id)return {};
            int16_t count{};
            std::memcpy(&count, records->data() + id * adversary_counter_record_stride,
                        sizeof(count));
            killed = killed || count > 0;
        }
        if(killed)result.emplace(entry.row);
    }
    if (journal_read<uintptr_t>(reinterpret_cast<uintptr_t>(*singleton) +
                                native_save_owner_layout::singleton_save_owner_offset) != owner)
    {
        return {};
    }
    return result;
}
void RuntimeAdversaryService::tick(std::chrono::steady_clock::time_point) {
    // The journal observes defeats even when AP adversary locations are off.
    hook_->install();
    const auto view=reconciliation::view();
    if(!view || !view->ready || !view->adversary_checks) {
        was_playable_=false;needs_sync_=true;return;
    }
    if(!hook_->installed)return;
    if(seed_!=view->seed || slot_!=view->slot || epoch_!=view->epoch) {
        seed_=view->seed;slot_=view->slot;epoch_=view->epoch;reported_.reset();needs_sync_=true;
    }
    const auto generation=counter_generation.load(std::memory_order_acquire);
    // Native load/death/seed transitions invalidate the reconciliation epoch.
    // Until one of those or a counter update arrives, there is no gameplay work
    // to validate. Avoid possession/property reads on every idle frame.
    if(!needs_sync_ && generation==observed_generation_)return;
    const bool playable=is_uobject_valid_fast(sora_());
    if(!playable){was_playable_=false;needs_sync_=true;return;}
    if(!was_playable_){was_playable_=true;needs_sync_=true;}
    const auto bridge=bridge_();if(!bridge)return;
    const auto defeated=read_adversary_defeats();
    if(!defeated)return;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    RC::Output::send<RC::LogLevel::Normal>(L"[KH3APProbeCpp] Adversary counter snapshot reason={} generation={} defeated_rows={}\n",
            needs_sync_ ? L"reconcile" : L"update_event",generation,defeated->size());
#endif
    if(defeated==reported_){observed_generation_=generation;needs_sync_=false;return;}
    const RuntimeConditionalProfileScope<(KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0)> profile{L"adversaries:publish"};
    std::ostringstream json;
    json<<"{\"schema\":\"kh3ap.runtime_adversary_state.v1\",\"seed_name\":\""<<json_escape(seed_)
        <<"\",\"slot\":"<<slot_<<",\"source\":\"native_kill_counters\",\"process_id\":"<<GetCurrentProcessId()
        <<",\"defeated\":[";
    bool first=true;for(const auto& row:*defeated){if(!first)json<<',';first=false;json<<'"'<<json_escape(row)<<'"';}
    json<<"]}";
    // The existing reconciliation journal owns durable, idempotent completion.
    // Native count rollback never removes an already completed AP check.
    if(write_text_file_atomic_verified(*bridge/channels::runtime_adversary_state_file,json.str())) {
        reported_=defeated;observed_generation_=generation;needs_sync_=false;
    }
}
}
