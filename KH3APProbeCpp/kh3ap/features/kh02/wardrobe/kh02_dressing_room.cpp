#include "kh02_wardrobe_units.h"
#include "kh02_wardrobe_assets.h"
#include "kh02_wardrobe_accessories.h"
#include "kh02_wardrobe_materials.h"
#include "kh02_wardrobe_pattern.h"
#include "kh02_wardrobe_reflection.h"
#include "kh02_dressing_room.h"
#include "kh3ap/features/kh02/progress/kh02_progress_policy.h"
#include "kh02_cosmetic_items.h"
#include "kh02_cosmetic_policy.h"
#include "kh02_costume_preview.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <vector>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;
    using namespace kh02wardrobe_detail;

    namespace
    {
        constexpr const wchar_t* categories[]{L"Head", L"Arms", L"Back", L"Pattern", L"Color"};

        auto item_for(int category, int selected) -> const kh02cosmetics::Item*
        {
            for (const auto& item : kh02cosmetics::items)
            {
                if (item.category == category && --selected == 0)
                {
                    return &item;
                }
            }
            return nullptr;
        }
    } // namespace

    struct Kh02DressingRoom::Impl
    {
        Json selected = kh02cosmetics::default_selection();
        FWeakObjectPtr hero, mesh;
        Kh02CostumePreview preview;
        Kh02WardrobeAccessories accessories;

        Kh02WardrobeMaterials materials;
        Kh02WardrobeAssets wardrobe_assets;
        std::function<void()> changed;
        bool dirty{true};
        uint64_t applications{};
        std::string error;

        ~Impl()
        {
            try
            {
                clear();
                release_assets();
            }
            catch (const std::exception&)
            {
                // Shutdown can run after Unreal has already destroyed the world.
            }
        }

        void release_assets()
        {
            wardrobe_assets.release();
        }

        auto asset(UObject* context, const wchar_t* path) -> UObject*
        {
            return wardrobe_assets.load(context, path);
        }

        void clear()
        {
            accessories.clear();
            materials.restore(mesh.Get());
        }

        void attach(UObject* pawn, UObject* component, UObject* prototype, const std::wstring& socket)
        {
            accessories.attach(pawn, component, prototype, socket);
        }

        void pattern(UObject* pawn, UObject* component, UObject* outfit_material, UScriptStruct* schema, void* unit)
        {
            kh02_apply_wardrobe_pattern({pawn, component, outfit_material, schema, unit, selected}, materials,
                                        wardrobe_assets);
        }

        void apply(UObject* pawn)
        {
            const auto color_mode = selected["color_mode"].get<WardrobeColorMode>();
            std::array<UObject*, 4> assets{};
            for (int category = 0; category < 4; ++category)
            {
                int selected_index = selected["equipped"][category];
                if (category == 3 && selected_index == 0 && color_mode != WardrobeColorMode::Default)
                {
                    selected_index = 1; // The original plain mask also supports custom outfit colors.
                }
                if (const auto* selected_item = item_for(category, selected_index))
                {
                    assets[category] = asset(pawn, selected_item->asset);
                    if (!assets[category])
                    {
                        return;
                    }
                }
            }
            UObject* outfit_material = nullptr;
            if (assets[3])
            {
                outfit_material =
                    asset(pawn, L"/Game/Zharacter/pc/p_ex002/mdl/mat0/p_ex002_mat0_Inst.p_ex002_mat0_Inst");
                if (!outfit_material)
                {
                    return;
                }
            }
            clear();
            auto* component = mesh.Get();
            for (int category = 0; category < 4; ++category)
            {
                if (!assets[category])
                {
                    continue;
                }
                const auto units = Kh02WardrobeUnitView::decode(assets[category]);
                for (int index = 0; index < units.count(); ++index)
                {
                    if (category == 3)
                    {
                        pattern(pawn, component, outfit_material, units.schema(), units.unit(index));
                        continue;
                    }
                    const auto attachment = units.attachment(index);
                    attach(pawn, component, attachment.part_class->GetClassDefaultObject(), attachment.socket);
                }
            }
            dirty = false;
            ++applications;
        }
    };

    Kh02DressingRoom::Kh02DressingRoom() : impl(std::make_unique<Impl>()) {}

    Kh02DressingRoom::~Kh02DressingRoom() = default;

    void Kh02DressingRoom::release(bool restore_materials)
    {
        impl->preview.close(restore_materials);
        if (restore_materials)
        {
            impl->clear();
        }
        else
        {
            // The departing level owns these actors. Avoid native component
            // calls once map teardown has started; only release our references.
            impl->accessories.forget();
            impl->materials.forget();
        }
        impl->release_assets();
        impl->hero = {};
        impl->mesh = {};
        impl->dirty = true;
        impl->error.clear();
    }

    void Kh02DressingRoom::restore(const std::string& progress)
    {
        const auto value = Json::parse(progress);
        const auto selection = value.is_null() ? kh02cosmetics::default_selection()
                                               : value.value("cosmetics", kh02cosmetics::default_selection());
        restore(kh02_progress::cosmetic_selection(selection));
    }

    void Kh02DressingRoom::restore(const std::optional<kh02_progress::CosmeticSelection>& selection)
    {
        auto selected = kh02_progress::to_json(selection.value_or(kh02_progress::CosmeticSelection{}));
        kh02cosmetics::validate(selected);
        if (impl->selected != selected)
        {
            impl->selected = std::move(selected);
            impl->dirty = true;
            impl->error.clear();
        }
    }

    auto Kh02DressingRoom::selection() const -> std::string
    {
        return impl->selected.dump();
    }

    auto Kh02DressingRoom::progress_selection() const -> kh02_progress::CosmeticSelection
    {
        return kh02_progress::cosmetic_selection(impl->selected);
    }

    void Kh02DressingRoom::on_change(std::function<void()> changed)
    {
        impl->changed = std::move(changed);
    }

    auto Kh02DressingRoom::list(int category) const -> Kh02CostumeList
    {
        if (category < 0 || category > 4)
        {
            throw std::runtime_error("Unknown wardrobe category");
        }
        Kh02CostumeList result{categories[category]};
        const auto add = [&](std::wstring title, bool equipped)
        {
            result.rows.push_back(
                {std::move(title), equipped ? L"Equipped" : L"Available",
                 category == 4
                     ? L"Select a color mode, then adjust Red, Green and Blue. Gamepad: A apply, bumpers category, "
                       L"right stick rotate, B back."
                     : L"Select an item to equip. Back returns to the wardrobe categories.",
                 1});
        };
        if (category < 4)
        {
            result.selected = impl->selected["equipped"][category];
            add(category == 3 ? L"Default" : L"None", result.selected == 0);
            int index = 0;
            for (const auto& item : kh02cosmetics::items)
            {
                if (item.category == category)
                {
                    add(item.title, ++index == result.selected);
                }
            }
        }
        else
        {
            const auto mode = impl->selected["color_mode"].get<WardrobeColorMode>();
            result.selected = static_cast<int>(mode);
            add(L"Default colors", mode == WardrobeColorMode::Default);
            add(L"Single color", mode == WardrobeColorMode::SingleColor);
            add(L"Gradient", mode == WardrobeColorMode::Gradient);
            for (const auto* region : {"top", "bottom"})
            {
                for (int channel = 0; channel < 3; ++channel)
                {
                    const auto label = std::wstring(region[0] == 't' ? L"Top " : L"Bottom ") +
                                       std::array{L"Red", L"Green", L"Blue"}[channel];
                    add(label + L": " + std::to_wstring(impl->selected[region][channel].get<int>()), false);
                    result.rows.back().progress = L"0 - 255";
                    result.rows.back().description =
                        region[0] == 'b' && mode != WardrobeColorMode::Gradient
                            ? L"Editing this channel enables Gradient. Gamepad: D-pad Left/Right adjusts by 8; "
                              L"A adds 1; X subtracts 1."
                            : L"Editing enables custom colors. Gamepad: D-pad Left/Right adjusts by 8; A adds 1; "
                              L"X subtracts 1. A/D or right stick rotates Aqua.";
                }
            }
        }
        return result;
    }

    void Kh02DressingRoom::choose(int category, int index)
    {
        if (category < 0 || category > 4 || index < 0 || index >= static_cast<int>(list(category).rows.size()))
        {
            throw std::runtime_error("Unknown wardrobe item");
        }
        if (category < 4)
        {
            impl->selected["equipped"][category] = index;
        }
        else if (index <= static_cast<int>(WardrobeColorMode::Gradient))
        {
            impl->selected["color_mode"] = index;
        }
        else
        {
            adjust_color(index, 1);
            return;
        }
        impl->dirty = true;
        impl->error.clear();
        if (impl->changed)
        {
            impl->changed();
        }
    }

    void Kh02DressingRoom::set_selection(const std::string& selection)
    {
        auto value = Json::parse(selection);
        kh02cosmetics::validate(value);
        if (value == impl->selected)
        {
            return;
        }
        impl->selected = std::move(value);
        impl->dirty = true;
        impl->error.clear();
        if (impl->changed)
        {
            impl->changed();
        }
    }

    void Kh02DressingRoom::adjust_color(int index, int delta)
    {
        if (!kh02cosmetics::adjust_channel(impl->selected, index, delta))
        {
            return;
        }
        impl->dirty = true;
        impl->error.clear();
        if (impl->changed)
        {
            impl->changed();
        }
    }

    void Kh02DressingRoom::observe(UObject* hero, bool available)
    {
        if (impl->hero.Get() != hero)
        {
            impl->preview.close();
            impl->clear();
            impl->release_assets();
            impl->hero = make_uobject_weak(hero);
            impl->mesh = {};
            impl->dirty = true;
            impl->error.clear();
        }
        if (available && !is_uobject_valid_fast(impl->mesh.Get()) && is_uobject_valid_fast(hero) &&
            hero->GetClassPrivate()->GetName().starts_with(L"p_ex002_Pawn"))
        {
            impl->mesh = make_uobject_weak(object_field(hero, L"MyMesh"));
        }
    }

    auto Kh02DressingRoom::needs_tick() const -> bool
    {
        return impl->dirty && impl->error.empty() && is_uobject_valid_fast(impl->mesh.Get());
    }

    auto Kh02DressingRoom::ready() const -> bool
    {
        if (!impl->error.empty())
        {
            throw std::runtime_error(impl->error);
        }
        auto* hero = impl->hero.Get();
        const bool aqua =
            is_uobject_valid_fast(hero) && hero->GetClassPrivate()->GetName().starts_with(L"p_ex002_Pawn");
        return !aqua || (is_uobject_valid_fast(impl->mesh.Get()) && !impl->dirty);
    }

    void Kh02DressingRoom::tick(UObject* hero, bool available)
    {
        try
        {
            observe(hero, available);
            if (available && needs_tick())
            {
                impl->apply(hero);
            }
        }
        catch (const std::exception& error)
        {
            impl->error = error.what();
        }
    }

    auto Kh02DressingRoom::snapshot() const -> std::string
    {
        Json out{{"selection", impl->selected},  {"pending", impl->dirty},
                 {"error", impl->error},         {"applications", impl->applications},
                 {"accessories", Json::array()}, {"pattern_materials", impl->materials.size()}};
        out["preview"] = Json::parse(impl->preview.snapshot());
        for (const auto& weak : impl->accessories.actors())
        {
            if (auto* actor = weak.Get(); is_uobject_valid_fast(actor))
            {
                out["accessories"].push_back(to_utf8(actor->GetPathName()));
            }
        }
        return out.dump();
    }

    void Kh02DressingRoom::preview(bool visible, float angle)
    {
        if (visible)
        {
            impl->preview.update(impl->hero.Get(), impl->accessories.actors(), angle);
        }
        else
        {
            impl->preview.close();
        }
    }
} // namespace kh3ap
