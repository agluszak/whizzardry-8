#include "wiz8/sr_api.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/string_database.h"
#include "wiz8/virtual_file.h"
#include "wiz8/filesystem.h"

#include <stdlib.h>
#include <string.h>

// GLOBAL: WIZ8 0x0068c098
int giStringListLen;
// GLOBAL: WIZ8 0x0068c09c
wchar_t** gppStringList;

/* Read one entry of a .msg string database. The file ends with the
   entry table; each record carries two metadata dwords, then the code-unit
   count and the text itself. The fifth header byte selects the 0x9697 text
   encoding, and the count guard admits at most 0x7D0 code units, which is the
   shared quote buffer's proven extent. */
// FUNCTION: WIZ8 0x0052FF80
unsigned char GetStringFromStringDatabase(const char* path, int index, wchar_t* output,
                                          unsigned int* metadata_00, unsigned int* metadata_04)
try
{
    std::unique_ptr<wiz8::File> handle;
    unsigned char header[5];
    wchar_t* destination;
    int count;
    int entry_offset;
    int length;
    int character;

    destination = output;
    *output = 0;
    handle = [&]() { try { return wiz8::open_file(const_cast<char*>(path), wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (!handle) {
        return 0;
    }
    handle->read_exact(header, 5);
    handle->seek(-static_cast<std::int64_t>(8), wiz8::SeekOrigin::end);
    handle->read_exact(&count, 4);
    if (index < count) {
        handle->seek(-static_cast<std::int64_t>((count - index) * 4 + 8), wiz8::SeekOrigin::end);
        handle->read_exact(&entry_offset, 4);
        handle->seek(entry_offset, wiz8::SeekOrigin::begin);
        if (metadata_00) {
            handle->read_exact(metadata_00, 4);
        } else {
            handle->seek(4, wiz8::SeekOrigin::current);
        }
        if (metadata_04) {
            handle->read_exact(metadata_04, 4);
        } else {
            handle->seek(4, wiz8::SeekOrigin::current);
        }
        handle->read_exact(&length, 4);
        if (length <= 0x7d0) {
            handle->read_exact(destination, length * 2);
            if (header[4] && length > 0) {
                for (character = 0; character < length; ++character) {
                    destination[character] = static_cast<wchar_t>(~destination[character] + 0x9697);
                }
            }
            if (handle) handle->close();
            handle.reset();
            return 1;
        }
    }
    if (handle) handle->close();
    handle.reset();
    return 0;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x005300e0
void DecodeLocalizedText(wchar_t* text, int character_count)
{
    while (character_count-- > 0) {
        *text = static_cast<unsigned short>(~*text + 0x9697);
        ++text;
    }
}

// STRING: WIZ8 0x0061a4ec
#define STRINGS_CPP "C:\\Projects\\Wizardry 8\\Local Code\\Strings.cpp"

// FUNCTION: WIZ8 0x00518360
void LoadLocalizedStrings(const char* path)
{
    std::unique_ptr<wiz8::File> handle = [&]() { try { return wiz8::open_file(const_cast<char*>(path), wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    int index;

    if (!handle) {
        srAssertFail("hFile", STRINGS_CPP, 74, "Failed to open localization string table.");
    }
    handle->read_exact(&giStringListLen, 4);
    if (!giStringListLen) {
        srAssertFail("giStringListLen", STRINGS_CPP, 79, 0);
    }
    gppStringList = static_cast<wchar_t**>(malloc(giStringListLen * sizeof(wchar_t*)));
    if (!gppStringList) {
        srAssertFail("gppStringList", STRINGS_CPP, 82, 0);
    }
    memset(gppStringList, 0, giStringListLen * sizeof(wchar_t*));
    for (index = 0; index < giStringListLen; ++index) {
        int byte_count;
        handle->read_exact(&byte_count, 4);
        gppStringList[index] = static_cast<wchar_t*>(malloc(byte_count));
        if (!gppStringList[index]) {
            srAssertFail("gppStringList[iCount]", STRINGS_CPP, 89, 0);
        }
        handle->read_exact(gppStringList[index], byte_count);
        DecodeLocalizedText(gppStringList[index], byte_count / 2);
    }
    if (handle) handle->close();
    handle.reset();
}
