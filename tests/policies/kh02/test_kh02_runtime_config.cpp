#include "kh3ap/features/kh02/runtime/kh02_runtime_config.h"
#include <cassert>
#include <chrono>

int main()
{
    using namespace kh3ap;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto file = std::filesystem::temp_directory_path() / ("kh02-config-test-" + std::to_string(unique) + ".json");

    struct Cleanup
    {
        std::filesystem::path file;

        ~Cleanup()
        {
            std::error_code error;
            std::filesystem::remove(file, error);
        }
    } cleanup{file};

    const std::string contents = R"({"prepare_gameplay_on_entry":true})";
    {
        std::ofstream output(file);
        output << contents;
    }
    assert(kh02_read_json_file(file, contents.size(), "limit").at("prepare_gameplay_on_entry") == true);
    bool rejected_size = false;
    try
    {
        kh02_read_json_file(file, contents.size() - 1, "config_limit");
    }
    catch (const std::runtime_error& error)
    {
        rejected_size = std::string(error.what()) == "config_limit";
    }
    assert(rejected_size);
    {
        std::ofstream output(file);
        output << "{invalid";
    }
    bool rejected_json = false;
    try
    {
        kh02_read_json_file(file, kh02_entry_config_limit_bytes, "limit");
    }
    catch (const nlohmann::json::parse_error&)
    {
        rejected_json = true;
    }
    assert(rejected_json);
    std::filesystem::remove(file);
    bool rejected_missing = false;
    try
    {
        kh02_read_json_file(file, kh02_checkpoint_config_limit_bytes, "limit");
    }
    catch (const std::filesystem::filesystem_error&)
    {
        rejected_missing = true;
    }
    assert(rejected_missing);
}
