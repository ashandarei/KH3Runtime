#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace kh3ap
{
    // Nested previews have separate rollback points; history survives closing the menu.
    class Kh02WardrobeEditTransaction
    {
      public:
        using Json = nlohmann::json;

        void configure(std::function<std::string()> read, std::function<void(const std::string&)> write)
        {
            read_selection = std::move(read);
            write_selection = std::move(write);
        }

        auto value() const -> Json
        {
            return Json::parse(read_selection());
        }

        void apply(const Json& selection)
        {
            write_selection(selection.dump());
        }

        void begin(const Json& selection)
        {
            original = selection;
            active = true;
        }

        void capture_editor(const Json& selection)
        {
            editor_original = selection;
        }

        void capture_palette(const Json& selection)
        {
            palette_original = selection;
        }

        auto editor_selection() const -> const Json&
        {
            return editor_original;
        }

        void rollback_editor(bool gradient)
        {
            apply(gradient ? editor_original : original);
        }

        void rollback_palette()
        {
            apply(palette_original);
        }

        void rollback()
        {
            apply(original);
            active = false;
        }

        void commit(const Json& selection)
        {
            original = selection;
            active = false;
        }

        void finish()
        {
            active = false;
        }

        void cancel()
        {
            if (active)
            {
                rollback();
            }
        }

        auto editing() const -> bool
        {
            return active;
        }

        auto single_colors() const -> const std::vector<Json>&
        {
            return single_history;
        }

        auto gradient_colors() const -> const std::vector<Json>&
        {
            return gradient_history;
        }

        void remember_single(const Json& color)
        {
            remember(single_history, color);
        }

        void remember_gradient(const Json& colors)
        {
            remember(gradient_history, colors);
        }

      private:
        static constexpr std::size_t history_limit = 10;

        static void remember(std::vector<Json>& history, const Json& entry)
        {
            std::erase(history, entry);
            history.insert(history.begin(), entry);
            if (history.size() > history_limit)
            {
                history.resize(history_limit);
            }
        }

        std::function<std::string()> read_selection;
        std::function<void(const std::string&)> write_selection;
        bool active{};
        Json original, editor_original, palette_original;
        std::vector<Json> single_history, gradient_history;
    };
} // namespace kh3ap
