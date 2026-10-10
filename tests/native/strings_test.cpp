#include "surrender/srBinOStream.h"
#include "surrender/srBinIStream.h"

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

int main()
{
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

    puts("ok: standard strings and polymorphic memory stream lifecycles");
}
