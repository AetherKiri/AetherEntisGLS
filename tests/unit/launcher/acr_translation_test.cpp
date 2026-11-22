#include "launcher/acr_translation.h"
#include <cstdio>
#include <functional>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void U32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = uint8_t(value >> (8*i));
}
std::vector<uint8_t> Fixture() {
    std::vector<uint8_t> bytes(16 + 5*20);
    U32(bytes, 0, 5);
    const std::pair<std::u16string, std::u16string> entries[] = {
        {u"original", u"中文"}, {u"collision", u"\U0001f30a"},
        {u"original", u"later"}, {u"empty", u""},
        {u"line one.line two.", u"第一行\n第二行\\"}
    };
    for (unsigned i = 0; i < 5; ++i) {
        const size_t row = 16 + i*20;
        U32(bytes, row, 123); // The hash is not a unique string identity.
        auto append = [&](size_t field, const std::u16string& text) {
            U32(bytes, row + field, bytes.size());
            U32(bytes, row + field + 4, text.size()*2);
            for (const auto unit : text) { bytes.push_back(unit & 255); bytes.push_back(unit >> 8); }
        };
        append(4, entries[i].first); append(12, entries[i].second);
    }
    U32(bytes, 8, bytes.size()-16); U32(bytes, 12, bytes.size()-16);
    return bytes;
}
}
int main() try {
    const auto bytes = Fixture();
    const entis::launcher::AcrTranslation translations(bytes);
    Require(translations.Size() == 4, "duplicate keys use the first translation");
    Require(translations.Find(L"original") && *translations.Find(L"original") == L"中文", "UTF-16 text");
    Require(translations.Find(L"collision") && *translations.Find(L"collision") == L"\U0001f30a", "hash collisions and surrogate pairs");
    Require(translations.Find(L"empty") && translations.Find(L"empty")->empty(), "intentional empty translation");
    Require(!translations.Find(L"absent"), "unknown literals are unchanged");
    auto message = [&](std::wstring input, const std::wstring& expected, bool matched = true) {
        Require(translations.TranslateMessage(input) == matched, "message lookup status");
        Require(input == expected, "formatted message translation");
    };
    message(L"origi\r\nnal", L"中文");
    message(L"line one.\nline two.", L"第一行\n第二行\\\\");
    message(L"\\s;24:\\bline one.\\n:line two.\\nb\\s:",
            L"\\s;24:\\b第一行\n第二行\\\\\\nb\\s:");
    message(L"\\f;serif:\\r;original;reading:", L"\\f;serif:中文");
    message(L"or\\bin\\nbal", L"or\\bin\\nbal", false);
    message(L"\\s;24:unknown\ntext", L"\\s;24:unknown\ntext", false);
    message(L"\\r;original;", L"\\r;original;", false);
    message(L"\\x;image:", L"\\x;image:", false);
    message(L"empty", L"");
    auto reject = [&](const std::function<void(std::vector<uint8_t>&)>& corrupt) {
        auto invalid = bytes; corrupt(invalid);
        bool rejected = false;
        try { entis::launcher::AcrTranslation table(invalid); }
        catch (const std::runtime_error&) { rejected = true; }
        Require(rejected, "malformed translation table must fail");
    };
    reject([](auto& b) { b.resize(15); });
    reject([](auto& b) { b.pop_back(); });
    reject([](auto& b) { U32(b, 0, UINT32_MAX); });
    reject([](auto& b) { U32(b, 4, 1); });
    reject([](auto& b) { U32(b, 20, 16); });
    reject([](auto& b) { U32(b, 20, UINT32_MAX-1); });
    reject([](auto& b) { U32(b, 24, 1); });
    reject([](auto& b) { b[116] = 0; b[117] = 0xd8; });
    reject([](auto& b) { b[116] = 0; b[117] = 0xdc; });
    reject([](auto& b) { b[116] = 0; b[117] = 0; });
    std::puts("ACR translation parsing: PASS");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what()); return 1;
}
