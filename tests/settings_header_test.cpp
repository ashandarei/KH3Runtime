#include "kh3ap/app/config.h"
#include "kh3ap/app/config.h"
#include "kh3ap/app/composition/settings.h"

#ifdef STR
#error The settings header must not depend on or introduce Unreal string macros.
#endif

const std::wstring_view* settings_storage_from_other_translation_unit();

static_assert(kh3ap::settings::s_enable_runtime_hot_path_profiling ==
              (KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0));
static_assert(&kh3ap::settings::s_caribbean_chart_visit_flags ==
              &kh3ap::composition_detail::settings::s_caribbean_chart_visit_flags);

int main()
{
    // Independently included translation units must share the inline settings.
    return settings_storage_from_other_translation_unit() ==
                   kh3ap::settings::s_caribbean_chart_visit_flags
               ? 0
               : 1;
}
