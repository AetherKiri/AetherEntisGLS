#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace entis::launcher {
// Uncompressed UTF-16 Accelerator translation table. Lookup compares the
// original text rather than trusting the table's 32-bit hash (collisions exist).
class AcrTranslation {
public:
    explicit AcrTranslation(const std::vector<uint8_t>& bytes);
    const std::wstring* Find(const wchar_t* text) const;
    // Match the complete message after script layout, preserving surrounding
    // font commands. Table values are plain text, not MessageSprite commands.
    bool TranslateMessage(std::wstring& text) const;
    size_t Size() const { return entries_.size(); }
private:
    std::unordered_map<std::wstring,std::wstring> entries_;
};
}
