#include "kh3ap/features/ui/options_menu/host.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace options_menu_detail;
    namespace options_menu_detail
    {
        auto is_setting_menu_context(UObject* object) -> bool
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                return false;
            }
            UClass* const object_class = object->GetClassPrivate();
            return object_class && object_class->GetName() == setting_menu_class_name;
        }

        auto rounded_gfx_index(const GfxProbeFunctionPaths& paths, UObject* object)
            -> std::optional<int32_t>
        {
            const auto value = try_gfx_get_member_value_number(paths, object, STR("index"));
            if (!value.has_value())
            {
                return std::nullopt;
            }
            return static_cast<int32_t>(std::llround(*value));
        }
    } // namespace options_menu_detail
    auto RuntimeOptionsMenuService::Impl::member_path(
        UObject* object, std::initializer_list<std::wstring_view> members) -> UObject*
    {
        UObject* current = object;
        for (const std::wstring_view member : members)
        {
            current = try_gfx_get_member_value_object(config_.gfx_paths, current, member);
            if (!current)
            {
                return nullptr;
            }
        }
        return current;
    }

    auto RuntimeOptionsMenuService::Impl::display_child_named(UObject* container,
                                                              std::wstring_view name) -> UObject*
    {
        return container ? try_gfx_invoke_string_arg_object(config_.gfx_paths, container,
                                                            STR("getChildByName"), name)
                         : nullptr;
    }

    auto RuntimeOptionsMenuService::Impl::object_member(UObject* root, std::wstring_view marker,
                                                        std::wstring_view instance) -> UObject*
    {
        if (!root)
        {
            return nullptr;
        }
        if (!instance.empty())
        {
            if (UObject* const child = display_child_named(root, instance))
            {
                return child;
            }
        }
        return try_gfx_get_member_value_object(config_.gfx_paths, root, marker);
    }

    auto RuntimeOptionsMenuService::Impl::page_row(UObject* root, size_t index) -> UObject*
    {
        return index < page_members_.size()
                   ? object_member(root, page_members_[index], page_instances_[index])
                   : nullptr;
    }

    auto RuntimeOptionsMenuService::Impl::choice_row(UObject* root, size_t index) -> UObject*
    {
        return index < choice_members_.size()
                   ? object_member(root, choice_members_[index], choice_instances_[index])
                   : nullptr;
    }

    auto RuntimeOptionsMenuService::Impl::page_scroll_host(UObject* root) -> UObject*
    {
        return object_member(root, page_scroll_host_member_, page_scroll_host_instance_);
    }

    auto RuntimeOptionsMenuService::Impl::live_menu_root() -> UObject*
    {
        UObject* const context = menu_context_.Get();
        if (context && is_setting_menu_context(context))
        {
            UObject* const proxy =
                try_read_reflected_object_property_value(context, STR("m_ASProxyUIComponent"));
            UObject* const root =
                proxy ? try_read_reflected_object_property_value(proxy, STR("m_ASObject"))
                      : nullptr;
            if (root && kh3ap::is_uobject_valid_fast(root))
            {
                menu_root_ = root;
                return root;
            }
        }
        UObject* const fallback = menu_root_.Get();
        return fallback && kh3ap::is_uobject_valid_fast(fallback) ? fallback : nullptr;
    }

    auto RuntimeOptionsMenuService::Impl::live_parent_button(UObject* root, size_t index)
        -> UObject*
    {
        if (index >= parent_instances_.size())
        {
            return nullptr;
        }
        UObject* const body = root ? member_path(root, {STR("menuMain"), STR("body")}) : nullptr;
        UObject* parent = display_child_named(body, parent_instances_[index]);
        if (!parent && body)
        {
            parent =
                try_gfx_get_member_value_object(config_.gfx_paths, body, parent_members_[index]);
        }
        if (!parent || !kh3ap::is_uobject_valid_fast(parent))
        {
            parent = parent_buttons_[index].Get();
        }
        if (parent && kh3ap::is_uobject_valid_fast(parent))
        {
            parent_buttons_[index] = parent;
            return parent;
        }
        return nullptr;
    }

    auto RuntimeOptionsMenuService::Impl::active_parent_button(UObject* root) -> UObject*
    {
        return live_parent_button(root, active_menu_index_);
    }

    auto RuntimeOptionsMenuService::Impl::list_item(UObject* list, int32_t index) -> UObject*
    {
        UObject* const container = member_path(list, {STR("container")});
        return container ? try_gfx_invoke_get_child_at(config_.gfx_paths, container, index)
                         : nullptr;
    }

    auto RuntimeOptionsMenuService::Impl::clone_parent_button(UObject* body, UObject* source,
                                                              size_t menu_index) -> UObject*
    {
        if (!body || !source)
        {
            return nullptr;
        }

        // Clone from Quit Game, not from any device-specific page. The
        // source is briefly unregistered so clone() cannot replace its
        // FocusManager dictionary entry; it is restored before the new
        // custom row receives its private index.
        (void)try_gfx_set_member_value_int(config_.gfx_paths, source, STR("index"), -1);
        UObject* const clone =
            try_gfx_invoke_no_args_object(config_.gfx_paths, source, STR("clone"));
        (void)try_gfx_set_member_value_int(config_.gfx_paths, source, STR("index"),
                                           stock_quit_index);
        if (!clone)
        {
            return nullptr;
        }

        (void)try_gfx_set_member_value_string(config_.gfx_paths, clone, STR("name"),
                                              parent_instances_[menu_index]);
        (void)try_gfx_set_member_value_int(config_.gfx_paths, clone, STR("index"),
                                           parent_anchor_index(menu_index));
        if (!try_gfx_invoke_object_arg(config_.gfx_paths, body, STR("addChild"), clone))
        {
            return nullptr;
        }
        // Keep the private member marker as a best-effort diagnostic only.
        // UGFxObject wrappers are transient; the display tree name is authoritative.
        (void)try_gfx_set_member_value_object(config_.gfx_paths, body, parent_members_[menu_index],
                                              clone);
        normalize_parent_button(clone);
        return clone;
    }

    auto RuntimeOptionsMenuService::Impl::clone_detached_item(UObject* root, UObject* source,
                                                              std::wstring_view marker,
                                                              std::wstring_view instance_name,
                                                              int32_t private_anchor_index)
        -> UObject*
    {
        if (!root || !source)
        {
            return nullptr;
        }

        // AnchorComponent.clone() copies and immediately registers its
        // source index. Unregister the source around clone construction,
        // then restore it before registering the clone under its private
        // custom index. This prevents any stock list anchor from being
        // replaced or deleted in FocusManager's dictionary.
        const int32_t source_index = rounded_gfx_index(config_.gfx_paths, source).value_or(-1);
        (void)try_gfx_set_member_value_int(config_.gfx_paths, source, STR("index"), -1);
        UObject* const clone =
            try_gfx_invoke_no_args_object(config_.gfx_paths, source, STR("clone"));
        (void)try_gfx_set_member_value_int(config_.gfx_paths, source, STR("index"), source_index);
        if (!clone)
        {
            return nullptr;
        }
        (void)try_gfx_set_member_value_string(config_.gfx_paths, clone, STR("name"), instance_name);
        prepare_detached_item(clone);
        set_button_visible(clone, false);
        if (!try_gfx_invoke_object_arg(config_.gfx_paths, root, STR("addChild"), clone))
        {
            return nullptr;
        }
        (void)try_gfx_set_member_value_object(config_.gfx_paths, root, marker, clone);
        (void)try_gfx_set_member_value_int(config_.gfx_paths, clone, STR("index"),
                                           private_anchor_index);
        return clone;
    }

    auto RuntimeOptionsMenuService::Impl::clone_detached_scroll_host(UObject* root, UObject* source)
        -> UObject*
    {
        if (!root || !source)
        {
            return nullptr;
        }

        // CoListSetting is the five-row scroll host shared by the stock Sound,
        // Display, and Graphics pages. Clone only its visual shell. Keeping both
        // source and clone unregistered prevents the empty shell from taking part
        // in FocusManager; the GoA rows retain their private logical anchors.
        const int32_t source_index = rounded_gfx_index(config_.gfx_paths, source).value_or(-1);
        (void)try_gfx_set_member_value_int(config_.gfx_paths, source, STR("index"), -1);
        UObject* const clone =
            try_gfx_invoke_no_args_object(config_.gfx_paths, source, STR("clone"));
        (void)try_gfx_set_member_value_int(config_.gfx_paths, source, STR("index"), source_index);
        if (!clone)
        {
            return nullptr;
        }

        (void)try_gfx_set_member_value_string(config_.gfx_paths, clone, STR("name"),
                                              page_scroll_host_instance_);
        (void)try_gfx_set_member_value_int(config_.gfx_paths, clone, STR("index"), -1);
        (void)try_gfx_set_member_value_number(config_.gfx_paths, clone, STR("alpha"), 1.0);
        set_button_visible(clone, false);
        if (!try_gfx_invoke_object_arg(config_.gfx_paths, root, STR("addChild"), clone))
        {
            return nullptr;
        }
        (void)try_gfx_set_member_value_object(config_.gfx_paths, root, page_scroll_host_member_,
                                              clone);
        return clone;
    }

    auto RuntimeOptionsMenuService::Impl::display_transform(std::initializer_list<UObject*> chain)
        -> DisplayTransform
    {
        DisplayTransform result{};
        for (UObject* const object : chain)
        {
            if (!object)
            {
                continue;
            }
            const double local_x =
                try_gfx_get_member_value_number(config_.gfx_paths, object, STR("x")).value_or(0.0);
            const double local_y =
                try_gfx_get_member_value_number(config_.gfx_paths, object, STR("y")).value_or(0.0);
            result.x += local_x * result.scale_x;
            result.y += local_y * result.scale_y;
            result.scale_x *=
                try_gfx_get_member_value_number(config_.gfx_paths, object, STR("scaleX"))
                    .value_or(1.0);
            result.scale_y *=
                try_gfx_get_member_value_number(config_.gfx_paths, object, STR("scaleY"))
                    .value_or(1.0);
        }
        return result;
    }

    auto RuntimeOptionsMenuService::Impl::apply_display_transform(UObject* item,
                                                                  const DisplayTransform& transform)
        -> void
    {
        if (!item)
        {
            return;
        }
        (void)try_gfx_set_member_value_number(config_.gfx_paths, item, STR("x"), transform.x);
        (void)try_gfx_set_member_value_number(config_.gfx_paths, item, STR("y"), transform.y);
        (void)try_gfx_set_member_value_number(config_.gfx_paths, item, STR("scaleX"),
                                              transform.scale_x);
        (void)try_gfx_set_member_value_number(config_.gfx_paths, item, STR("scaleY"),
                                              transform.scale_y);
    }

    auto RuntimeOptionsMenuService::Impl::extrapolated_transform(const DisplayTransform& first,
                                                                 const DisplayTransform& second,
                                                                 size_t index) -> DisplayTransform
    {
        const double step = static_cast<double>(index);
        return DisplayTransform{
            .x = first.x + ((second.x - first.x) * step),
            .y = first.y + ((second.y - first.y) * step),
            .scale_x = first.scale_x,
            .scale_y = first.scale_y,
        };
    }

    auto RuntimeOptionsMenuService::Impl::ensure_detached_items(UObject* root) -> bool
    {
        std::vector<UObject*> page_rows(page_capacity_);
        std::vector<UObject*> choice_rows(choice_capacity_);
        UObject* scroll_host = page_scroll_host(root);
        for (size_t i = 0; i < page_rows.size(); ++i)
        {
            page_rows[i] = page_row(root, i);
        }
        for (size_t i = 0; i < choice_rows.size(); ++i)
        {
            choice_rows[i] = choice_row(root, i);
        }

        // A SettingMenu movie can be reused across repeated Config opens. Its
        // detached display objects and private FocusManager anchors persist with
        // that movie, so do not touch their indices (or the hidden stock lists
        // they were cloned from) again once the full private tree exists.
        const bool page_rows_complete =
            std::all_of(page_rows.begin(), page_rows.end(),
                        [](UObject* object)
                        {
                            return object && kh3ap::is_uobject_valid_fast(object);
                        });
        const bool choice_rows_complete =
            std::all_of(choice_rows.begin(), choice_rows.end(),
                        [](UObject* object)
                        {
                            return object && kh3ap::is_uobject_valid_fast(object);
                        });
        const bool scroll_host_complete = scroll_host && kh3ap::is_uobject_valid_fast(scroll_host);
        if (page_rows_complete && choice_rows_complete && scroll_host_complete)
        {
            return true;
        }

        // The middle rows use the generic Sound settings row symbol. No
        // object belonging to the device settings page is read or cloned.
        UObject* const setting_page = member_path(root, {STR("menuSnd")});
        UObject* const setting_body = member_path(root, {STR("menuSnd"), STR("body")});
        UObject* const setting_list =
            member_path(root, {STR("menuSnd"), STR("body"), STR("coList")});
        UObject* const setting_container = member_path(setting_list, {STR("container")});
        UObject* const setting_scrollbar = member_path(setting_list, {STR("scrollbar")});
        UObject* const setting_source_0 = list_item(setting_list, 0);
        UObject* const setting_source_1 = list_item(setting_list, 1);

        // The right-side rows use the generic option-row art only. The
        // original option list is never focused, repainted, or refreshed.
        UObject* const choice_page = member_path(root, {STR("optionList")});
        UObject* const choice_body = member_path(root, {STR("optionList"), STR("body")});
        UObject* const choice_list =
            member_path(root, {STR("optionList"), STR("body"), STR("coList")});
        UObject* const choice_container = member_path(choice_list, {STR("container")});
        UObject* const choice_source_0 = list_item(choice_list, 0);
        UObject* const choice_source_1 = list_item(choice_list, 1);
        if (!setting_page || !setting_body || !setting_list || !setting_container ||
            !setting_scrollbar || !setting_source_0 || !setting_source_1 || !choice_page ||
            !choice_body || !choice_list || !choice_container || !choice_source_0 ||
            !choice_source_1)
        {
            return false;
        }

        if (!scroll_host)
        {
            scroll_host = clone_detached_scroll_host(root, setting_list);
        }
        if (!scroll_host || !member_path(scroll_host, {STR("scrollbar")}))
        {
            return false;
        }
        for (size_t i = 0; i < page_rows.size(); ++i)
        {
            if (!page_rows[i])
            {
                page_rows[i] = clone_detached_item(root, setting_source_0, page_members_[i],
                                                   page_instances_[i], page_anchor_index(i));
            }
            if (!page_rows[i])
            {
                return false;
            }
        }
        for (size_t i = 0; i < choice_rows.size(); ++i)
        {
            if (!choice_rows[i])
            {
                choice_rows[i] = clone_detached_item(root, choice_source_0, choice_members_[i],
                                                     choice_instances_[i], choice_anchor_index(i));
            }
            if (!choice_rows[i])
            {
                return false;
            }
        }

        page_scroll_host_transform_ = display_transform({setting_page, setting_body, setting_list});
        page_first_transform_ = display_transform(
            {setting_page, setting_body, setting_list, setting_container, setting_source_0});
        page_second_transform_ = display_transform(
            {setting_page, setting_body, setting_list, setting_container, setting_source_1});
        const DisplayTransform choice_first = display_transform(
            {choice_page, choice_body, choice_list, choice_container, choice_source_0});
        const DisplayTransform choice_second = display_transform(
            {choice_page, choice_body, choice_list, choice_container, choice_source_1});
        choice_first_transform_ = choice_first;
        choice_second_transform_ = choice_second;
        apply_display_transform(scroll_host, page_scroll_host_transform_);

        for (size_t i = 0; i < page_rows.size(); ++i)
        {
            apply_display_transform(
                page_rows[i],
                extrapolated_transform(page_first_transform_, page_second_transform_, i));
            (void)try_gfx_set_member_value_int(config_.gfx_paths, page_rows[i], STR("index"),
                                               page_anchor_index(i));
            if (!page_rows.empty())
            {
                configure_anchor(page_rows[i],
                                 page_anchor_index((i + page_rows.size() - 1) % page_rows.size()),
                                 page_anchor_index((i + 1) % page_rows.size()));
            }
        }
        for (size_t i = 0; i < choice_rows.size(); ++i)
        {
            apply_display_transform(choice_rows[i],
                                    extrapolated_transform(choice_first, choice_second, i));
            (void)try_gfx_set_member_value_int(config_.gfx_paths, choice_rows[i], STR("index"),
                                               choice_anchor_index(i));
        }
        hide_detached_items(root);
        return true;
    }
} // namespace kh3ap
