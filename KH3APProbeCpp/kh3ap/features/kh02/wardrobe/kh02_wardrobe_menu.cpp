#include "kh02_wardrobe_companion_movies.h"
#include "kh02_wardrobe_edit_transaction.h"
#include "kh02_wardrobe_focus.h"
#include "kh02_wardrobe_movie_view.h"
#include "kh02_wardrobe_menu.h"
#include "kh3ap/features/kh02/movies/kh02_menu_movie.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <algorithm>
#include <array>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02menu;
    using Json = nlohmann::json;

    struct Kh02WardrobeMenu::Impl
    {
        std::function<Kh02CostumeList(int)> list;
        std::function<void(int, int)> equip;
        Kh02WardrobeCompanionMovies companion_movies;
        Kh02WardrobePage page{Kh02WardrobePage::Categories};
        int category{}, selected{}, region{};
        bool gradient{}, gamepad{true};
        Kh02WardrobeEditTransaction transaction;
        Kh02WardrobeFocus focus_controller;

        Json value() const
        {
            return transaction.value();
        }

        void apply(const Json& selection)
        {
            transaction.apply(selection);
        }

        void focus_submenu(UObject* root)
        {
            focus_controller.transition(root, companion_movies.cursor_root(), navigation());
        }

        void info(UObject* root)
        {
            Kh02WardrobeMovieView::info(root, value(), region);
        }

        void histories(UObject* root)
        {
            Kh02WardrobeMovieView::histories(root, transaction.single_colors(), transaction.gradient_colors());
        }

        auto navigation() const -> Kh02WardrobeFocus::Selection
        {
            return {page,
                    category,
                    selected,
                    page == Kh02WardrobePage::Gradient ? count() : 0,
                    static_cast<int>(transaction.gradient_colors().size()),
                    gamepad};
        }

        auto focus_path() const -> std::wstring
        {
            return Kh02WardrobeFocus::path(navigation());
        }

        int count() const
        {
            switch (page)
            {
            case Kh02WardrobePage::Categories:
                return 5;
            case Kh02WardrobePage::Items:
                return static_cast<int>(list(category).rows.size());
            case Kh02WardrobePage::ColorType:
                return 3;
            case Kh02WardrobePage::Gradient:
                return 3 + static_cast<int>(transaction.gradient_colors().size());
            case Kh02WardrobePage::Editor:
                return 5;
            case Kh02WardrobePage::Palette:
                return 20 + static_cast<int>(transaction.single_colors().size());
            }
            return 0;
        }

        auto focus(UObject* root) -> bool
        {
            return focus_controller.focus(root, companion_movies.cursor_root(), navigation());
        }

        void enter(UObject* root)
        {
            if (category < 4)
            {
                const auto data = list(category);
                std::vector<MovieArgument> arguments;
                for (const auto& row : data.rows)
                {
                    arguments.emplace_back(row.text);
                    arguments.emplace_back(0);
                }
                invoke_movie(root, L"setCostumeItemListData", arguments);
                selected = data.selected;
                page = Kh02WardrobePage::Items;
                invoke_movie(root, L"openCostumeItemList", {category, data.title, selected});
            }
            else
            {
                page = Kh02WardrobePage::ColorType;
                selected = 0;
                info(root);
                invoke_movie(root, L"openColorTypeSelect", {std::wstring(L"Color")});
            }
            focus_submenu(root);
        }

        void open_editor(UObject* root)
        {
            transaction.capture_editor(value());
            page = Kh02WardrobePage::Editor;
            selected = 0;
            std::vector<MovieArgument> arguments{
                std::wstring(gradient ? L"Gradation" : L"One Color"),
                std::wstring(gradient ? (region == 0 ? L"Top Color" : L"Bottom Color") : L"")};
            Kh02WardrobeMovieView::append_color(arguments,
                                                transaction.editor_selection()[region == 0 ? "top" : "bottom"]);
            invoke_movie(root, L"openColorBar", arguments);
            focus_submenu(root);
        }

        void preview_palette(UObject* root)
        {
            auto selection = value();
            selection[region == 0 ? "top" : "bottom"] =
                selected < 20 ? Json(kh02_wardrobe_palette[selected]) : transaction.single_colors()[selected - 20];
            apply(selection);
            info(root);
        }

        bool back(UObject* root)
        {
            switch (page)
            {
            case Kh02WardrobePage::Categories:
                return true;
            case Kh02WardrobePage::Items:
                invoke_movie(root, L"closeCostumeItemList");
                page = Kh02WardrobePage::Categories;
                selected = category;
                break;
            case Kh02WardrobePage::ColorType:
                invoke_movie(root, L"closeColorTypeSelect");
                page = Kh02WardrobePage::Categories;
                selected = category;
                break;
            case Kh02WardrobePage::Gradient:
                transaction.rollback();
                info(root);
                invoke_movie(root, L"closeGradationTypeSelect");
                page = Kh02WardrobePage::ColorType;
                selected = 1;
                break;
            case Kh02WardrobePage::Editor:
                transaction.rollback_editor(gradient);
                info(root);
                invoke_movie(root, L"closeColorBar");
                page = gradient ? Kh02WardrobePage::Gradient : Kh02WardrobePage::ColorType;
                selected = gradient ? region : 0;
                if (!gradient)
                {
                    transaction.finish();
                }
                break;
            case Kh02WardrobePage::Palette:
                transaction.rollback_palette();
                info(root);
                invoke_movie(root, L"closeSingleColorPalette");
                page = Kh02WardrobePage::Editor;
                selected = 3;
                break;
            }
            focus_submenu(root);
            return false;
        }

        void confirm(UObject* root)
        {
            switch (page)
            {
            case Kh02WardrobePage::Categories:
                enter(root);
                return;
            case Kh02WardrobePage::Items:
                equip(category, selected);
                focus(root);
                return;
            case Kh02WardrobePage::ColorType:
            {
                auto selection = value();
                if (selected == 2)
                {
                    selection["color_mode"] = 0;
                    apply(selection);
                    info(root);
                    return;
                }
                transaction.begin(selection);
                gradient = selected == 1;
                region = 0;
                selection["color_mode"] = gradient ? 2 : 1;
                apply(selection);
                if (!gradient)
                {
                    open_editor(root);
                    return;
                }
                page = Kh02WardrobePage::Gradient;
                selected = 0;
                auto arguments = Kh02WardrobeMovieView::colors(selection);
                arguments.insert(arguments.begin(), std::wstring(L"Gradation"));
                invoke_movie(root, L"openGradationTypeSelect", arguments);
                histories(root);
                focus_submenu(root);
                return;
            }
            case Kh02WardrobePage::Gradient:
                if (selected < 2)
                {
                    region = selected;
                    invoke_movie(root, L"focusGradationType", {region});
                    open_editor(root);
                }
                else if (selected == count() - 1)
                {
                    const auto selection = value();
                    transaction.remember_gradient(Json::array({selection["top"], selection["bottom"]}));
                    transaction.commit(selection);
                    info(root);
                    histories(root);
                    invoke_movie(root, L"closeGradationTypeSelect");
                    page = Kh02WardrobePage::ColorType;
                    selected = 1;
                    focus_submenu(root);
                }
                else
                {
                    auto selection = value();
                    const auto& pair = transaction.gradient_colors()[selected - 2];
                    selection["top"] = pair[0];
                    selection["bottom"] = pair[1];
                    apply(selection);
                    info(root);
                }
                return;
            case Kh02WardrobePage::Editor:
                if (selected == 3)
                {
                    transaction.capture_palette(value());
                    page = Kh02WardrobePage::Palette;
                    selected = 0;
                    histories(root);
                    invoke_movie(root, L"openSingleColorPalette");
                    focus_submenu(root);
                }
                else if (selected == 4)
                {
                    const auto selection = value();
                    transaction.remember_single(selection[region == 0 ? "top" : "bottom"]);
                    if (!gradient)
                    {
                        transaction.commit(selection);
                    }
                    info(root);
                    histories(root);
                    invoke_movie(root, L"closeColorBar");
                    page = gradient ? Kh02WardrobePage::Gradient : Kh02WardrobePage::ColorType;
                    selected = gradient ? region : 0;
                    focus_submenu(root);
                }
                return;
            case Kh02WardrobePage::Palette:
                preview_palette(root);
                invoke_movie(root, L"closeSingleColorPalette");
                page = Kh02WardrobePage::Editor;
                selected = 3;
                focus_submenu(root);
                return;
            }
        }
    };

    Kh02WardrobeMenu::Kh02WardrobeMenu() : impl(std::make_unique<Impl>()) {}

    Kh02WardrobeMenu::~Kh02WardrobeMenu() = default;

    auto Kh02WardrobeMenu::prepare(UObject* pawn) -> bool
    {
        return impl->companion_movies.prepare(pawn);
    }

    void Kh02WardrobeMenu::configure(std::function<Kh02CostumeList(int)> list, std::function<void(int, int)> equip,
                                     std::function<std::string()> read, std::function<void(const std::string&)> write)
    {
        impl->list = std::move(list);
        impl->equip = std::move(equip);
        impl->transaction.configure(std::move(read), std::move(write));
    }

    void Kh02WardrobeMenu::open(UObject* root)
    {
        impl->companion_movies.show();
        impl->page = Kh02WardrobePage::Categories;
        impl->category = 0;
        impl->selected = 0;
        impl->transaction.finish();
        impl->histories(root);
        invoke_movie(root, L"setColorButtonEnable", {true});
        Kh02WardrobeMovieView::labels(root);
        impl->focus_submenu(root);
    }

    void Kh02WardrobeMenu::tick(UObject* root)
    {
        impl->focus_controller.tick(root, impl->companion_movies.cursor_root(), impl->navigation());
    }

    void Kh02WardrobeMenu::input_mode(UObject* root, bool gamepad)
    {
        if (impl->gamepad != gamepad)
        {
            impl->gamepad = gamepad;
            impl->focus(root);
        }
    }

    auto Kh02WardrobeMenu::move(UObject* root, int delta) -> bool
    {
        const int previous = impl->selected;
        if (impl->page == Kh02WardrobePage::Palette)
        {
            const int last_row = (impl->count() - 1) / 10;
            const int row = std::clamp(impl->selected / 10 + delta, 0, last_row);
            const int column = impl->selected % 10;
            impl->selected = std::min(row * 10 + column, impl->count() - 1);
        }
        else if (impl->page == Kh02WardrobePage::Gradient && !impl->transaction.gradient_colors().empty())
        {
            const int save = impl->count() - 1;
            if (impl->selected >= 2 && impl->selected < save)
            {
                impl->selected = delta < 0 ? 1 : save;
            }
            else if (impl->selected == save && delta < 0)
            {
                impl->selected = 2;
            }
            else
            {
                impl->selected = std::clamp(impl->selected + delta, 0, save);
            }
        }
        else
        {
            impl->selected = std::clamp(impl->selected + delta, 0, impl->count() - 1);
        }
        if (impl->page == Kh02WardrobePage::Categories)
        {
            impl->category = impl->selected;
        }
        if (impl->page == Kh02WardrobePage::Palette)
        {
            impl->preview_palette(root);
        }
        impl->focus(root);
        return impl->selected != previous;
    }

    auto Kh02WardrobeMenu::horizontal(UObject* root, int delta) -> bool
    {
        const int previous = impl->selected;
        if (impl->page == Kh02WardrobePage::Palette)
        {
            impl->selected = std::clamp(impl->selected + delta, 0, impl->count() - 1);
            impl->preview_palette(root);
            impl->focus(root);
        }
        else if (impl->page == Kh02WardrobePage::Editor && impl->selected < 3)
        {
            auto selection = impl->value();
            auto& channel = selection[impl->region == 0 ? "top" : "bottom"][impl->selected];
            const int previous_value = channel.get<int>();
            const int next_value = std::clamp(previous_value + delta, 0, 255);
            if (next_value == previous_value)
            {
                return false;
            }
            channel = next_value;
            impl->apply(selection);
            impl->info(root);
            return true;
        }
        else if (impl->page == Kh02WardrobePage::Gradient && impl->selected >= 2 && impl->selected < impl->count() - 1)
        {
            impl->selected = std::clamp(impl->selected + delta, 2, impl->count() - 2);
            impl->focus(root);
        }
        return impl->selected != previous;
    }

    auto Kh02WardrobeMenu::confirm(UObject* root) -> bool
    {
        if (impl->page == Kh02WardrobePage::Editor && impl->selected < 3)
        {
            return false;
        }
        impl->confirm(root);
        return true;
    }

    auto Kh02WardrobeMenu::back(UObject* root) -> bool
    {
        return impl->back(root);
    }

    void Kh02WardrobeMenu::cancel()
    {
        impl->companion_movies.hide();
        impl->transaction.cancel();
    }

    auto Kh02WardrobeMenu::snapshot() const -> Json
    {
        constexpr std::array names{"categories", "items", "color_type", "gradient", "editor", "palette"};
        return {{"page", names[static_cast<size_t>(impl->page)]},
                {"category", impl->category},
                {"selected", impl->selected},
                {"focus_pending", impl->focus_controller.pending()},
                {"focus_immediate", impl->focus_controller.immediate()},
                {"focus_target", to_utf8(impl->focus_path())},
                {"cursor_target", to_utf8(impl->focus_controller.target())},
                {"cursor_position", impl->focus_controller.position()},
                {"region", impl->region},
                {"editing", impl->transaction.editing()},
                {"single_history", impl->transaction.single_colors().size()},
                {"gradient_history", impl->transaction.gradient_colors().size()}};
    }
} // namespace kh3ap
