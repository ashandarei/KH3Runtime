#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/player/context/context_store.h"

#include "kh3ap/features/player/context/context_resolver.h"
#include "kh3ap/bridge/serialization/payload_serializer.h"
#include "kh3ap/bridge/utilities.h"

#include <filesystem>
#include <Windows.h>

namespace kh3ap
{
    auto RuntimeContextStore::read(const std::filesystem::path& bridge_dir) -> std::optional<RuntimeContextSnapshot>
    {
        const auto context_text = read_text_file(bridge_dir / channels::runtime_context);
        if (!context_text.has_value())
        {
            return std::nullopt;
        }

        return RuntimeContextResolver::parse_snapshot(*context_text);
    }

    auto RuntimeContextStore::write(
            const std::filesystem::path& bridge_dir,
            const RuntimeContextSnapshot& snapshot) -> bool
    {
        return write_text_file(bridge_dir / channels::runtime_context,
                RuntimePayloadSerializer::serialize_context(snapshot, GetCurrentProcessId()));
    }
}
