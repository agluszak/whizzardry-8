#include "surrender/srBinOStream.h"
#include "surrender/srIStreamOpener.h"

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

class CapturingOpener : public srIStreamOpener::Opener {
public:
    srBinIStream* open(std::string_view path) override
    {
        paths.emplace_back(path);
        if (!accept) return nullptr;
        return new srBinIMStream("x", 1);
    }
    std::string_view getDescription() const override { return "String regression opener"; }
    std::vector<std::string> paths;
    bool accept = true;
};

class BadStream : public srBinIMStream {
public:
    explicit BadStream(int& destroyed) : srBinIMStream(nullptr, 0), destroyed(destroyed) {}
    ~BadStream() override { ++destroyed; }
private:
    int& destroyed;
};

class BadOpener : public srIStreamOpener::Opener {
public:
    srBinIStream* open(std::string_view) override { return new BadStream(destroyed); }
    std::string_view getDescription() const override { return "Bad stream fixture"; }
    int destroyed = 0;
};

int main()
{
    const std::string long_text(4096, 'a');

    CapturingOpener capture;
    srIStreamOpener opener;
    std::string extension = "capture";
    opener.addStreamType(&capture, extension);
    extension.assign("changed");
    for (const auto& path : {std::string("\\Data\\mixed/zażółć-雪.bin\\"),
                             std::string(), long_text + "\\leaf", std::string("C:\\Data\\leaf")}) {
        std::string expected = path;
        for (char& character : expected) {
            if (character == '\\') character = '/';
        }
        const std::string source = "CaPtUrE://" + path + "ignored suffix";
        std::unique_ptr<srBinIStream> stream(
            opener.open(std::string_view(source.data(), source.size() - 14)));
        CHECK(stream && stream->good() && capture.paths.back() == expected);
    }
    std::unique_ptr<srBinIStream> fallback(opener.open("missing\\string-regression.bin"));
    CHECK(fallback && fallback->good() && capture.paths.back() == "missing/string-regression.bin");
    std::unique_ptr<srBinIStream> drive(opener.open("C:\\missing\\string-regression.bin"));
    CHECK(drive && capture.paths.back() == "C:/missing/string-regression.bin");
    CHECK(!opener.open("unknown://file"));

    CapturingOpener duplicate;
    opener.addStreamType(&duplicate, "CAPTURE");
    const auto earlier_calls = capture.paths.size();
    std::unique_ptr<srBinIStream> preferred(opener.open("capture://newest"));
    CHECK(preferred && duplicate.paths.back() == "newest");
    CHECK(capture.paths.size() == earlier_calls);
    duplicate.accept = false;
    CHECK(!opener.open("capture://no-prefix-fallback"));
    CHECK(capture.paths.size() == earlier_calls);
    std::unique_ptr<srBinIStream> ordered(opener.open("missing\\ordered.bin"));
    CHECK(ordered && duplicate.paths.back() == "missing/ordered.bin");
    CHECK(capture.paths.back() == "missing/ordered.bin");

    BadOpener bad;
    srIStreamOpener bad_registry;
    bad_registry.addStreamType(&bad, "bad");
    CHECK(!bad_registry.open("bad://discard") && bad.destroyed == 1);
    // Prefix-free fallback returns the first non-null stream, even if it is bad.
    std::unique_ptr<srBinIStream> bad_fallback(bad_registry.open("missing\\bad.bin"));
    CHECK(bad_fallback && !bad_fallback->good() && bad.destroyed == 1);
    bad_fallback.reset();
    CHECK(bad.destroyed == 2);

    auto memory = new srBinOMStream;
    std::unique_ptr<srBinOStream> output(memory);
    const std::string memory_text = "zażółć-雪";
    output->write(memory_text.data(), memory_text.size());
    CHECK(output->good() && output->getSize() == memory_text.size());
    std::unique_ptr<srBinIStream> input(new srBinIMStream(memory->getPtr(), memory_text.size()));
    std::string copied(memory_text.size(), '\0');
    input->read(copied.data(), copied.size());
    CHECK(input->good() && copied == memory_text);
    CHECK(static_cast<bool>(*input) && !(!*input));
    input->setState(srBinStream::SR_STREAM_ERROR);
    CHECK(!static_cast<bool>(*input) && !*input);
    input->clear();
    CHECK(static_cast<bool>(*input));

    puts("ok: standard string paths, extension ownership/order and polymorphic stream lifecycles");
}
