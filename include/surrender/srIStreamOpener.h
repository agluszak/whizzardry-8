#pragma once

#include "srBinFStream.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

class srIStreamOpener {
public:
    class Opener {
    public:
        // FUNCTION: SURRENDER 0x10032680
        // RECOMP: ??0Opener@srIStreamOpener@@QAE@XZ
        Opener() = default;
        // FUNCTION: SURRENDER 0x10032690
        // RECOMP: ??1Opener@srIStreamOpener@@UAE@XZ
        virtual ~Opener() = default;

        virtual srBinIStream* open(std::string_view path) = 0;
        virtual std::string_view getDescription() const = 0;
    };

    // FUNCTION: SURRENDER 0x100326B0
    // RECOMP: ??0srIStreamOpener@@QAE@XZ
    srIStreamOpener() = default;
    srIStreamOpener(const srIStreamOpener&) = delete;
    srIStreamOpener& operator=(const srIStreamOpener&) = delete;

    SR_DLL_IMPORT void addStreamType(Opener* opener, std::string_view extension);
    SR_DLL_IMPORT srBinIStream* open(std::string_view path);

private:
    SR_DLL_IMPORT Opener* findOpener(std::string_view extension);
    SR_DLL_IMPORT srBinIStream* open(std::string_view prefix, std::string path);

    // Openers are borrowed; extensions are owned. The newest registration wins.
    std::vector<std::pair<std::string, Opener*>> stream_types;
};

W8_ABI_ASSERT(sizeof(srIStreamOpener::Opener) == 0x04, "srIStreamOpener_Opener_must_be_0x04");
W8_ABI_ASSERT(sizeof(srIStreamOpener) == 0x0c, "srIStreamOpener_must_be_0x0c");

/* SR's built-in file opener. */
// VTABLE: SURRENDER 0x10075520 srFStreamOpener
class srFStreamOpener : public srIStreamOpener::Opener {
public:
    // FUNCTION: SURRENDER 0x10032440
    // RECOMP: ??0srFStreamOpener@@QAE@XZ
    srFStreamOpener() = default;

    srBinIStream* open(std::string_view path) override;
    std::string_view getDescription() const override;
};

W8_ABI_ASSERT(sizeof(srFStreamOpener) == 0x04, "srFStreamOpener_must_be_0x04");
