#include "shared/kh02/kh02_executable_verification.h"
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2)
    {
        return 2;
    }
    try
    {
        kh02integrity::verify_executable(argv[1]);
        std::cout << "verified\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        if (argc == 3 && std::wstring(argv[2]) == L"--terminate-on-error")
        {
            kh02integrity::terminate_failed_verification(error.what());
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
