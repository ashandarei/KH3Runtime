#include "kh02_wardrobe_movie_view.h"
#include "kh02_wardrobe_presentation.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02menu;
    using Json = nlohmann::json;

    namespace
    {
        UObject* member(UObject* root, const std::wstring& relative)
        {
            return find_movie_member(root, (std::wstring(kh02_wardrobe_costume_path) + relative).c_str());
        }

        void label(UObject* root, const std::wstring& path, const wchar_t* text)
        {
            if (auto* object = member(root, path))
            {
                try_gfx_set_member_value_string(gfx, object, L"label", text);
            }
        }
    } // namespace

    void Kh02WardrobeMovieView::append_color(std::vector<MovieArgument>& arguments, const Json& color)
    {
        for (const auto& channel : color)
        {
            arguments.emplace_back(channel.get<int>());
        }
    }

    auto Kh02WardrobeMovieView::colors(const Json& selection) -> std::vector<MovieArgument>
    {
        std::vector<MovieArgument> arguments;
        append_color(arguments, selection["top"]);
        append_color(arguments, selection["bottom"]);
        return arguments;
    }

    void Kh02WardrobeMovieView::info(UObject* root, const Json& selection, int region)
    {
        const int mode = selection["color_mode"];
        std::vector<MovieArgument> arguments{mode != 2, std::wstring(mode == 0   ? L"Default"
                                                                     : mode == 1 ? L"One Color"
                                                                                 : L"Gradation")};
        auto rgb = colors(selection);
        arguments.insert(arguments.end(), rgb.begin(), rgb.end());
        invoke_movie(root, L"setColorTypeInfo", arguments);
        invoke_movie(root, L"setGradationTypeSelectColor", rgb);
        std::vector<MovieArgument> channel;
        append_color(channel, selection[region == 0 ? "top" : "bottom"]);
        invoke_movie(root, L"setColorBarRGB", channel);
    }

    void Kh02WardrobeMovieView::histories(UObject* root, const std::vector<Json>& single_history,
                                          const std::vector<Json>& gradient_history)
    {
        std::vector<MovieArgument> arguments;
        for (const auto& color : kh02_wardrobe_palette)
        {
            for (int channel : color)
            {
                arguments.emplace_back(channel);
            }
        }
        invoke_movie(root, L"setSingleColorPalette", arguments);
        invoke_movie(root, L"setGradationColorPalette", arguments);
        arguments.clear();
        for (const auto& color : single_history)
        {
            append_color(arguments, color);
        }
        invoke_movie(root, L"setSingleRecordColorPalette", arguments);
        arguments.clear();
        for (const auto& pair : gradient_history)
        {
            append_color(arguments, pair[0]);
            append_color(arguments, pair[1]);
        }
        invoke_movie(root, L"setGradationRecordColorPalette", arguments);
    }

    void Kh02WardrobeMovieView::labels(UObject* root)
    {
        label(root, L"colorInfoAnm.colorInfo.singleColorBtn", L"One Color");
        label(root, L"colorInfoAnm.colorInfo.grdBtn", L"Gradation");
        label(root, L"colorInfoAnm.colorInfo.defaultBtn", L"Default");
        label(root, L"makeColorBarAnm.makeColorBar.choiceColorPaletBtn", L"Select from Palette");
        label(root, L"makeColorBarAnm.makeColorBar.decisionColorBtn", L"Save");
        label(root, L"grdColorSelectAnm.grdColorSelect.decisionColorBtn", L"Save");
        label(root, L"grdColorSelectAnm.grdColorSelect.grdColorSelect.choiceTopColorBtn", L"Top Color");
        label(root, L"grdColorSelectAnm.grdColorSelect.grdColorSelect.choiceBottomColorBtn", L"Bottom Color");
    }
} // namespace kh3ap
