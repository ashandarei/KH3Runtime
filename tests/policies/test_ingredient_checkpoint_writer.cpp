#include "kh3ap/features/ingredients/ingredient_checkpoint_writer.h"
#include "kh3ap/bridge/transport/checkpoint_file.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    auto read(const std::filesystem::path& path) -> std::string
    {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), {}};
    }
} // namespace

int main()
{
    using namespace std::chrono_literals;
    using Writer = kh3ap::IngredientCheckpointWriter;
    const auto root = std::filesystem::temp_directory_path() /
                      ("KH3AP-ingredient-writer-" + std::to_string(GetCurrentProcessId()) + "-" +
                       std::to_string(GetTickCount64()));
    const auto path = root / "runtime_ingredient_state.json";
    assert(kh3ap::write_checkpoint_file_atomic_verified(path, "original"));
    assert(read(path) == "original");
    auto temporary = path;
    temporary += L".tmp";
    assert(!std::filesystem::exists(temporary));

    // A failed atomic replacement must preserve the preceding checkpoint.
    const auto held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
    assert(held != INVALID_HANDLE_VALUE);
    assert(!kh3ap::write_checkpoint_file_atomic_verified(path, "blocked"));
    CloseHandle(held);
    assert(read(path) == "original");
    assert(!std::filesystem::exists(temporary));

    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool release = false;
    std::vector<std::string> written;
    {
        Writer writer(
            [&](const auto& destination, auto data)
            {
                {
                    std::unique_lock lock{mutex};
                    entered = true;
                    changed.notify_one();
                    changed.wait(lock,
                                 [&]()
                                 {
                                     return release;
                                 });
                    written.emplace_back(data);
                }
                return kh3ap::write_checkpoint_file_atomic_verified(destination, data);
            });
        assert(writer.submit({1, 1, path, "first"}));
        {
            std::unique_lock lock{mutex};
            assert(changed.wait_for(lock, 3s,
                                    [&]()
                                    {
                                        return entered;
                                    }));
        }
        // Submission returned while disk work is still blocked, and no
        // completion/publication can be observed before commit.
        assert(writer.take_completed().empty());
        assert(read(path) == "original");
        assert(writer.submit({1, 2, path, "second"}));
        assert(writer.submit({1, 3, path, "latest"}));
        const auto other_seed = root / "other-seed.json";
        assert(writer.submit({2, 4, other_seed, "other"}));
        {
            const std::lock_guard lock{mutex};
            release = true;
        }
        changed.notify_one();
        writer.stop();
        const auto completed = writer.take_completed();
        assert(completed.size() == 3);
        assert(completed[0].committed && completed[0].checkpoint->revision == 1);
        assert(completed[1].committed && completed[1].checkpoint->revision == 3);
        assert(completed[2].committed && completed[2].checkpoint->scope == 2);
        assert(read(path) == "latest" && read(other_seed) == "other");
        assert((written == std::vector<std::string>{"first", "latest", "other"}));
        assert(!writer.submit({1, 5, path, "after shutdown"}));
        assert(writer.take_completed().empty());
        std::filesystem::remove(other_seed);
    }

    // Failed work is retained and retries off-thread; failures are never
    // reported as committed, and an exception cannot kill the worker.
    int attempts = 0;
    entered = false;
    release = false;
    {
        Writer writer(
            [&](const auto& destination, auto data)
            {
                {
                    std::unique_lock lock{mutex};
                    if (++attempts == 1)
                    {
                        throw std::runtime_error("temporary failure");
                    }
                    entered = true;
                    changed.notify_one();
                    changed.wait(lock,
                                 [&]()
                                 {
                                     return release;
                                 });
                }
                return kh3ap::write_checkpoint_file_atomic_verified(destination, data);
            });
        assert(writer.submit({3, 6, path, "recovered"}));
        {
            std::unique_lock lock{mutex};
            assert(changed.wait_for(lock, 3s,
                                    [&]()
                                    {
                                        return entered;
                                    }));
        }
        const auto failed = writer.take_completed();
        assert(failed.size() == 1 && !failed[0].committed);
        assert(read(path) == "latest");
        {
            const std::lock_guard lock{mutex};
            release = true;
        }
        changed.notify_one();
        writer.stop();
        const auto recovered = writer.take_completed();
        assert(recovered.size() == 1 && recovered[0].committed);
        assert(read(path) == "recovered");
    }
    {
        Writer failed(
            [](const auto&, auto)
            {
                return false;
            });
        assert(failed.submit({4, 7, path, "never committed"}));
        failed.stop();
        for (const auto& result : failed.take_completed())
        {
            assert(!result.committed);
        }
        assert(read(path) == "recovered");
    }
    std::filesystem::remove(path);
    std::filesystem::remove(root);
}
