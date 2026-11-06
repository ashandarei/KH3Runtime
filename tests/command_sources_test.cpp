#include "kh3ap/bridge/session/command_sources.h"
#include <cassert>

int main()
{
    using namespace kh3ap;
    assert(runtime_command_source_allowed(true));
    assert(runtime_command_sources.front() == RuntimeCommandSource::Recovery);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    assert(runtime_command_source_allowed(false));
    assert(runtime_command_sources.size() == 2);
    assert(runtime_command_sources.back() == RuntimeCommandSource::Diagnostic);
#else
    assert(!runtime_command_source_allowed(false));
    assert(runtime_command_sources.size() == 1);
#endif
}
