#include "kh3ap/features/kh02/diagnostics/kh02_capture_path.h"

#include <cassert>

int main()
{
    const auto file = kh3ap::kh02_capture_file(L"kh02-world-within-scene.png");
    assert(file.is_absolute());
    assert(file.filename() == L"kh02-world-within-scene.png");
    assert(file.parent_path().filename() == std::to_wstring(GetCurrentProcessId()));
    assert(std::filesystem::is_directory(file.parent_path()));
    const auto spaced = kh3ap::kh02_capture_file(L"scene capture.png");
    const auto command = kh3ap::kh02_capture_command(spaced);
    assert(command == L"HighResShot 1920x1080 filename=\"" + spaced.generic_wstring() + L"\"");
    for (const auto* name : {L"", L"../escape.png", L"C:\\escape.png", L"scene.jpg"})
    {
        bool rejected{};
        try
        {
            (void)kh3ap::kh02_capture_file(name);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        assert(rejected);
    }
}
