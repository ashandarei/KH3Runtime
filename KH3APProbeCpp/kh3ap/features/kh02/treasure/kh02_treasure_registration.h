#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    void kh02_register_treasure_table(RC::Unreal::UObject* chests, bool& chest_table_registered);
}
