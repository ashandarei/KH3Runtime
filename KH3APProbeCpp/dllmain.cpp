#include "kh3ap/app/mod.h"

#define KH3_AP_PROBE_CPP_API __declspec(dllexport)

extern "C"
{
    KH3_AP_PROBE_CPP_API RC::CppUserModBase* start_mod()
    {
        return new kh3ap::Kh3ApRuntimeMod();
    }

    KH3_AP_PROBE_CPP_API void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
