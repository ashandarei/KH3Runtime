#pragma once
namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap
{
    void configure_master_query_contexts();
    void notify_master_query_created(RC::Unreal::UObject* object);
    void activate_master_query_contexts();
    void process_pending_master_query_contexts();
} // namespace kh3ap
