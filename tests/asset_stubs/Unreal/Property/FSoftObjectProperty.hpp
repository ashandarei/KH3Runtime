#pragma once
#include <Unreal/NameTypes.hpp>
namespace RC::Unreal {
struct FSoftObjectPtr { struct ID { FName AssetPathName; } ObjectID; };
}
