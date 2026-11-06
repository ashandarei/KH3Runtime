#include "kh3ap/bridge/transport/bounded_read.h"
#include <cassert>
#include <sstream>
#include <stdexcept>

namespace
{
    class GrowingBuffer : public std::streambuf
    {
    public:
        size_t consumed{};

    protected:
        auto xsgetn(char* output, std::streamsize count) -> std::streamsize override
        {
            std::fill_n(output, count, 'x');
            consumed += static_cast<size_t>(count);
            return count;
        }

        auto underflow() -> int_type override
        {
            ++consumed;
            return traits_type::to_int_type('x');
        }
    };

    class FailingBuffer : public std::streambuf
    {
    protected:
        auto xsgetn(char*, std::streamsize) -> std::streamsize override
        {
            throw std::runtime_error("simulated read failure");
        }
    };
} // namespace

int main()
{
    for (const size_t limit : {0u, 1u, 4095u, 4096u, 4097u, 8192u})
    {
        std::istringstream empty;
        assert(kh3ap::read_stream_bounded(empty, limit) == std::optional<std::string>{""});

        const std::string bytes(limit, '\0');
        std::istringstream exact(bytes);
        assert(kh3ap::read_stream_bounded(exact, limit) == bytes);

        std::istringstream oversized(bytes + "x");
        assert(!kh3ap::read_stream_bounded(oversized, limit));
        if (limit)
        {
            std::istringstream short_input(bytes.substr(1));
            assert(kh3ap::read_stream_bounded(short_input, limit) == bytes.substr(1));
        }
    }

    // Model a file that keeps growing: validation must stop at the budget,
    // with only a one-byte EOF probe, rather than reading until the writer stops.
    GrowingBuffer growing;
    std::istream growing_input(&growing);
    assert(!kh3ap::read_stream_bounded(growing_input, 8192));
    assert(growing.consumed == 8193);

    FailingBuffer failing;
    std::istream failed_input(&failing);
    assert(!kh3ap::read_stream_bounded(failed_input, 8192));
    std::istringstream unopened;
    unopened.setstate(std::ios::failbit);
    assert(!kh3ap::read_stream_bounded(unopened, 0));
    assert(!kh3ap::read_stream_bounded(unopened, 8192));
}
