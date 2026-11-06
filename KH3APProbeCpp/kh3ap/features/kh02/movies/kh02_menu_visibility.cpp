#include "kh02_menu_visibility.h"
#include "kh02_menu_movie.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02menu;

    namespace
    {
        auto display_root(UObject* widget) -> UObject*
        {
            if (!is_uobject_valid_fast(widget))
            {
                return nullptr;
            }
            auto* proxy = try_read_reflected_object_property_value(widget, L"m_ASProxyUIComponent");
            return is_uobject_valid_fast(proxy) ? try_read_reflected_object_property_value(proxy, L"m_ASObject")
                                                : nullptr;
        }
    } // namespace

    Kh02NativeMenuVisibility::~Kh02NativeMenuVisibility()
    {
        restore();
    }

    void Kh02NativeMenuVisibility::restore(Display& display)
    {
        if (auto* root = display.root.Get(); is_uobject_valid_fast(root))
        {
            try_gfx_set_member_value_number(gfx, root, L"alpha", display.alpha);
        }
        display.root = {};
    }

    void Kh02NativeMenuVisibility::suspend(Display& display, UObject* root)
    {
        // Repeated requests must not replace the original opacity with zero.
        if (is_uobject_valid_fast(root) && display.root.Get() == root)
        {
            return;
        }
        restore(display);
        if (!is_uobject_valid_fast(root))
        {
            return;
        }
        const auto alpha = try_gfx_get_member_value_number(gfx, root, L"alpha");
        if (alpha && try_gfx_set_member_value_number(gfx, root, L"alpha", 0))
        {
            display.alpha = *alpha;
            display.root = make_uobject_weak(root);
        }
    }

    void Kh02NativeMenuVisibility::suspend(UObject* camp)
    {
        suspend(menu, display_root(camp));
        auto* manager =
            is_uobject_valid_fast(camp) ? try_read_reflected_object_property_value(camp, L"m_pUIManager") : nullptr;
        auto* native_cursor =
            is_uobject_valid_fast(manager) ? try_read_reflected_object_property_value(manager, L"m_Cursor") : nullptr;
        suspend(cursor, display_root(native_cursor));
    }

    void Kh02NativeMenuVisibility::restore()
    {
        restore(menu);
        restore(cursor);
    }
} // namespace kh3ap
