#include "surrender/srBinOStream.h"
#include "surrender/srIStreamOpener.h"

#include <cstdio>
#include <memory>
#include <string>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

class CapturingOpener : public srIStreamOpener::Opener {
public:
    srBinIStream* open(const char* path) override
    {
        last_path = path;
        return new srBinIMStream("x", 1);
    }
    const char* getDescription() const override { return "String regression opener"; }
    std::string last_path;
};

int main()
{
    const std::string long_text(4096, 'a');

    CapturingOpener capture;
    srIStreamOpener opener;
    opener.addStreamType(&capture, "capture");
    for (const auto& path : {std::string("\\Data\\mixed/zażółć-雪.bin\\"),
                             std::string(), long_text + "\\leaf"}) {
        std::string expected = path;
        for (char& character : expected) {
            if (character == '\\') character = '/';
        }
        const std::string source = "capture://" + path;
        std::unique_ptr<srBinIStream> stream(opener.open(source.c_str()));
        CHECK(stream && stream->good() && capture.last_path == expected);
    }
    std::unique_ptr<srBinIStream> fallback(opener.open("missing\\string-regression.bin"));
    CHECK(fallback && fallback->good() && capture.last_path == "missing/string-regression.bin");
    CHECK(!opener.open(nullptr));
    CHECK(!opener.open("unknown://file"));

    auto memory = new srBinOMStream;
    std::unique_ptr<srBinOStream> output(memory);
    const std::string memory_text = "zażółć-雪";
    output->write(memory_text.c_str(), memory_text.size());
    CHECK(output->good() && output->getSize() == memory_text.size());
    std::unique_ptr<srBinIStream> input(new srBinIMStream(memory->getPtr(), memory_text.size()));
    std::string copied(memory_text.size(), '\0');
    input->read(copied.data(), copied.size());
    CHECK(input->good() && copied == memory_text);

    puts("ok: standard string stream paths and polymorphic stream lifecycles");
}
