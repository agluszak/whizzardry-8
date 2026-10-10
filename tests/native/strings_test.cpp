#include "surrender/srConfig.h"
#include "surrender/srBinOStream.h"
#include "surrender/srCore.h"
#include "surrender/srGERD.h"
#include "surrender/srIStreamOpener.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

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
    class srConfig config;
    config.append("text", "first");
    CHECK(std::string(config.get("text")) == "first");
    config.append("text", "-second");
    CHECK(std::string(config.get("text")) == "first-second");
    config.append("text", config.get("text"));
    CHECK(std::string(config.get("text")) == "first-secondfirst-second");
    config.append("empty", "");
    CHECK(config.exists("empty") && std::string(config.get("empty")).empty());
    config.append("empty", "zażółć-雪");
    CHECK(std::string(config.get("empty")) == "zażółć-雪");
    const std::string long_text(4096, 'a');
    config.append("empty", long_text.c_str());
    CHECK(std::string(config.get("empty")) == "zażółć-雪" + long_text);
    config.append("empty", "");
    CHECK(std::string(config.get("empty")) == "zażółć-雪" + long_text);
    config.append("ignored", nullptr);
    config.append(nullptr, "ignored");
    CHECK(!config.exists("ignored"));

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
    srFileManager file_manager;
    auto* previous_manager = srCore.getFileManager();
    srCore.setFileManager(&file_manager);
    std::unique_ptr<srBinIStream> fallback(opener.open("missing\\string-regression.bin"));
    srCore.setFileManager(previous_manager);
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

    const std::vector<std::string> devices{"missing-driver(0)"};
    CHECK(!srGERD::loadDevice(devices, 1));
    CHECK(!srGERD::loadDevice(std::vector<std::string>{}, 0));
    puts("ok: standard string config appends, stream paths and device-list bounds");
}
