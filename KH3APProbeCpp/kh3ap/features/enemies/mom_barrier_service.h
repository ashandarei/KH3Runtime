#pragma once
#include "kh3ap/features/enemies/enemy_clear_service.h"
#include <memory>
#include <string>
namespace kh3ap {
class RuntimeMoMBarrierService {
public:
    explicit RuntimeMoMBarrierService(RuntimeEnemyClearConfig config);
    ~RuntimeMoMBarrierService();
    std::string command(RC::Unreal::UObject* pawn,const std::string& request);
    void tick();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
