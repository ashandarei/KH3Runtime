#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/player/combat/character_shortcuts.h"
#include "kh3ap/features/player/combat/character_magic.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/app/composition/bindings.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/TArray.hpp>
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_shortcuts
{
    using namespace RC::Unreal;
    namespace
    {
        using Pages = std::array<int32_t, 12>;
        using EditorEvent = bool (*)(UObject*, void*, void*);
        using BuildChoices = void (*)(UObject*);
        uint64_t original_event{}, original_choices{};
        std::unique_ptr<PLH::x64Detour> event_hook, choices_hook;
        ReflectedPropertyCache<8> properties;
        FWeakObjectPtr bound_pawn, bound_instance;
        magic_command::Commands owned_spells{};
        std::optional<std::filesystem::path> root;
        std::map<int, Pages> bindings;
        int active_character{-1};
        bool editing{};

        std::filesystem::path binding_file(int character)
        {
            return *root / ("character_shortcuts_" + std::to_string(character) + ".txt");
        }

        bool read_pages(int character, Pages& pages)
        {
            if (!root)
            {
                return false;
            }
            std::ifstream in(binding_file(character));
            Pages loaded{};
            for (auto& command : loaded)
            {
                if (!(in >> command) || command < 0 || command > UINT8_MAX)
                {
                    return false;
                }
            }
            in >> std::ws;
            if (!in.eof())
            {
                return false;
            }
            pages = loaded;
            return true;
        }

        void persist(const Pages& pages)
        {
            std::ostringstream out;
            for (size_t i = 0; i < pages.size(); ++i)
            {
                out << pages[i] << (i % 4 == 3 ? '\n' : ' ');
            }
            if (!root ||
                !write_text_file_atomic_verified(binding_file(active_character), out.str()))
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Could not save character shortcuts for player {}\n"),
                    active_character);
            }
        }

        UObject* editor_instance(UObject* editor)
        {
            auto* pawn = bound_pawn.Get();
            if (!pawn || pawn != character_swap::current_hero() || !is_uobject_valid_fast(editor) ||
                editor->GetClassPrivate()->GetName() != L"TresUIP_CampMenuCustomize")
            {
                return nullptr;
            }
            auto* instance = bound_instance.Get();
            auto* manager = properties.read_object(instance, L"m_UIManager");
            auto* owner = properties.read_object(pawn, L"Controller");
            auto* camp = properties.read_object(manager, L"m_CampController");
            return owner && camp && properties.read_object(editor, L"m_CampController") == camp &&
                           properties.read_object(manager, L"m_pPlayerController") == owner &&
                           properties.read_object(owner, L"Pawn") == pawn
                       ? instance
                       : nullptr;
        }

        bool dispatch(UObject* editor, void* event, void* callback)
        {
            auto native = reinterpret_cast<EditorEvent>(original_event);
            auto* instance = editor_instance(editor);
            if (!instance || editing)
            {
                return native(editor, event, callback);
            }
            auto it = bindings.find(active_character);
            if (it == bindings.end())
            {
                return native(editor, event, callback);
            }
            // Customize reads and writes the game's single three-by-four shortcut table
            // inline during its synchronous menu event. Give just this dispatch the
            // alternate's pages; gameplay, saves and other menus keep Sora's pages.
            auto* table = reinterpret_cast<std::byte*>(instance) + 0x14550;
            struct ScopedPages
            {
                std::byte* table;
                Pages saved;
                ScopedPages(std::byte* address, const Pages& alternate) : table(address)
                {
                    std::memcpy(saved.data(), table, sizeof(saved));
                    std::memcpy(table, alternate.data(), sizeof(alternate));
                    editing = true;
                }
                ~ScopedPages()
                {
                    std::memcpy(table, saved.data(), sizeof(saved));
                    editing = false;
                }
            };
            Pages presented = it->second;
            for (auto& command : presented)
            {
                command =
                    magic_command::shortcut_command(static_cast<uint8_t>(command), owned_spells);
            }
            Pages updated{};
            bool handled;
            {
                ScopedPages scope(table, presented);
                handled = native(editor, event, callback);
                std::memcpy(updated.data(), table, sizeof(updated));
            }
            if (presented != updated)
            {
                for (size_t i = 0; i < presented.size(); ++i)
                {
                    if (updated[i] != presented[i])
                    {
                        it->second[i] = updated[i];
                    }
                }
                persist(it->second);
                character_magic::refresh_shortcut_bindings(bound_pawn.Get());
            }
            return handled;
        }

        void build_choices(UObject* editor)
        {
            reinterpret_cast<BuildChoices>(original_choices)(editor);
            const bool bound = editor_instance(editor) != nullptr;
            if (!bound)
            {
                // Story Riku and Mickey can own the controller outside the experimental
                // swap's editable spell loadout. They also cannot assign Sora's Links.
                auto* manager = properties.read_object(
                    properties.read_object(kh02_current_viewport(), L"GameInstance"),
                    L"m_UIManager");
                auto* camp = properties.read_object(editor, L"m_CampController");
                auto* controller = properties.read_object(manager, L"m_pPlayerController");
                auto* pawn = properties.read_object(controller, L"Pawn");
                auto* id = properties.resolve(pawn, L"m_PlayerUniqueID");
                if (!camp || camp != properties.read_object(manager, L"m_CampController") ||
                    !controller || properties.read_object(pawn, L"Controller") != controller ||
                    !id || id->GetElementSize() != sizeof(uint8_t))
                {
                    return;
                }
                const int player_id = *id->ContainerPtrToValuePtr<uint8_t>(pawn);
                if (!is_alternate_keyblade_wielder(player_id) &&
                    player_id != player_id_value(PlayerId::KingMickey))
                {
                    return;
                }
            }
            // All, Magic and Links each store a separate TArray of command IDs.
            // The native query accepts every spell when the selected record differs
            // from the possessed pawn. Use the same owned commands as the combat HUD.
            for (const auto offset : {0x3a0, 0x3b0, 0x3c0})
            {
                auto& choices = *reinterpret_cast<TArray<int32_t>*>(
                    reinterpret_cast<std::byte*>(editor) + offset);
                if (choices.Num() < 0 || choices.Num() > 512 || choices.Max() < choices.Num())
                {
                    continue;
                }
                for (int i = choices.Num() - 1; i >= 0; --i)
                {
                    const auto command = choices[i];
                    if (magic_command::is_link_command(command) ||
                        (bound && magic_command::normal_family(command) &&
                         !magic_command::owns_normal_command(command, owned_spells)))
                    {
                        choices.RemoveAt(i);
                    }
                }
            }
        }
    } // namespace

    bool initialize()
    {
        if (event_hook && choices_hook)
        {
            return true;
        }
        using character_native::Site;
        if (!character_native::verify_sites({Site::customize_event, Site::customize_choices}))
        {
            return false;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        event_hook = std::make_unique<PLH::x64Detour>(
            base + character_native::rva(Site::customize_event),
            reinterpret_cast<uintptr_t>(&dispatch), &original_event);
        choices_hook = std::make_unique<PLH::x64Detour>(
            base + character_native::rva(Site::customize_choices),
            reinterpret_cast<uintptr_t>(&build_choices), &original_choices);
        if (event_hook->hook() && choices_hook->hook())
        {
            return true;
        }
        choices_hook.reset();
        event_hook.reset();
        return false;
    }

    void reset()
    {
        bound_pawn = {};
        bound_instance = {};
        active_character = -1;
        bindings.clear();
        root.reset();
    }

    void bind(UObject* pawn, const magic_command::Commands& spells, std::span<uint8_t> hud_slots)
    {
        if (hud_slots.size() != Pages{}.size())
        {
            return;
        }
        auto* id = properties.resolve(pawn, L"m_PlayerUniqueID");
        if (!id || id->GetElementSize() != sizeof(uint8_t))
        {
            return;
        }
        const int character = *id->ContainerPtrToValuePtr<uint8_t>(pawn);
        const auto bridge = composition_detail::try_get_bridge_dir();
        if (root != bridge)
        {
            bindings.clear();
            root = bridge;
        }
        auto [it, created] = bindings.try_emplace(character);
        auto& pages = it->second;
        if (created && !read_pages(character, pages))
        {
            std::copy(hud_slots.begin(), hud_slots.end(), pages.begin());
        }
        bound_pawn = make_uobject_weak(pawn);
        bound_instance =
            make_uobject_weak(properties.read_object(kh02_current_viewport(), L"GameInstance"));
        active_character = character;
        owned_spells = spells;
        for (size_t i = 0; i < pages.size(); ++i)
        {
            hud_slots[i] = magic_command::shortcut_command(static_cast<uint8_t>(pages[i]), spells);
        }
    }
} // namespace kh3ap::character_shortcuts
