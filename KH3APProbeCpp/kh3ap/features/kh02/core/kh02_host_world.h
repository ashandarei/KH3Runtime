#pragma once

#include <string_view>

namespace kh3ap
{
    // Shared by camp and native save-point travel. Match host worlds exactly;
    // streamed source maps and arbitrary /Game/Maps packages are not KH0.2.
    constexpr bool is_kh02_host_world(std::wstring_view world)
    {
        return world == L"World /Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown" ||
               world == L"World /Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin" ||
               world == L"World /Game/Maps/APKH02Mirrors/APKH02Stairs.APKH02Stairs" ||
               world == L"World /Game/Maps/APKH02Mirrors/APKH02Corridor.APKH02Corridor" ||
               world == L"World /Game/Maps/APKH02Mirrors/APKH02Tunnel.APKH02Tunnel" ||
               world == L"World /Game/Maps/APKH02Mirrors/APKH02Phantom.APKH02Phantom" ||
               world == L"World /Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest" ||
               world == L"World /Game/Maps/APKH02Mirrors/APKH02Depths.APKH02Depths" ||
               world == L"World /Game/Maps/APKH02Mirrors/APKH02DestinyIslands.APKH02DestinyIslands" ||
               world == L"World /Game/Maps/APKH02Mirrors/APKH02Transition.APKH02Transition" ||
               world == L"World /Game/Maps/APKH02Mirrors/APKH02Epilogue.APKH02Epilogue" ||
               world == L"World /Game/Maps/APKH02Mirrors/APKH02MirrorChallenge.APKH02MirrorChallenge";
    }
} // namespace kh3ap
