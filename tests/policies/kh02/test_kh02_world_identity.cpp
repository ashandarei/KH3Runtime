#include "kh3ap/features/kh02/core/kh02_world_identity.h"
#include <cassert>
#include <string>

int main()
{
    using kh3ap::kh02_world_identity::mirror_room;
    for (const auto& room : kh02mirror_metadata::rooms)
    {
        const std::wstring world = std::wstring(room.package) + L"." + room.name;
        assert(mirror_room(world) == &room);
        assert(mirror_room(room.package) == nullptr);
        assert(mirror_room(world + L":PersistentLevel.Actor_0") == nullptr);
        assert(mirror_room(world + L"_0") == nullptr);
        assert(mirror_room(L"prefix" + world) == nullptr);
        assert(mirror_room(std::wstring(room.package) + L"/" + room.name) == nullptr);
        assert(mirror_room(std::wstring(room.package) + L".OtherWorld") == nullptr);
        assert(mirror_room(std::wstring_view(world).substr(0, world.size() - 1)) == nullptr);
    }
    assert(mirror_room(L"") == nullptr);
    assert(mirror_room(L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01") == nullptr);
    assert(mirror_room(L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown") == nullptr);
    assert(mirror_room(L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin") == nullptr);
}
