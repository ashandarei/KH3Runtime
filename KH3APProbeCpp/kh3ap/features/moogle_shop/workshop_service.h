#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace RC::Unreal {class UObject;}
namespace kh3ap {
void notify_workshop_object_created(RC::Unreal::UObject* object);
struct RuntimeWorkshopConfig {
    std::function<std::optional<std::filesystem::path>()> bridge_dir;
    std::function<bool()> gameplay_ready;
    std::function<bool(std::wstring_view)> owns_item;
};
class RuntimeWorkshopService {
public:
    explicit RuntimeWorkshopService(RuntimeWorkshopConfig config);
    ~RuntimeWorkshopService();
    void tick();
    void request_refresh();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
