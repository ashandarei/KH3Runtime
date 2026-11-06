#include "kh02_movie_settings.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    auto kh02_movie_volume(UObject* world) -> float
    {
        auto* instance = try_read_reflected_object_property_value(world, L"OwningGameInstance");
        auto* settings = try_read_reflected_object_property_value(instance, L"m_pGameSettings");
        if (!is_uobject_valid_fast(settings))
        {
            throw std::runtime_error("movie_audio_game_settings");
        }
        const auto read = [settings](const wchar_t* name)
        {
            auto* property = settings->GetPropertyByNameInChain(name);
            const auto* value = settings->GetValuePtrByPropertyNameInChain(name);
            if (!property || !value || property->GetElementSize() != 1 ||
                property->GetClass().GetName() != L"ByteProperty" ||
                *static_cast<const uint8_t*>(value) > maximum_game_volume_setting)
            {
                throw std::runtime_error("movie_audio_game_volume");
            }
            return *static_cast<const uint8_t*>(value);
        };
        return kh02_movie_gain(read(L"SoundMasterVolume"), read(L"SoundSeVolume"));
    }

} // namespace kh3ap
