#include "launcher/acr_translation.h"
#include <stdexcept>

namespace entis::launcher {
namespace {
uint32_t U32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
std::wstring Text(const std::vector<uint8_t>& bytes, size_t tableEnd, uint32_t offset, uint32_t length) {
    if ((offset&1) || (length&1) || offset<tableEnd || offset>bytes.size() ||
        length>bytes.size()-offset || length>1024*1024)
        throw std::runtime_error("Invalid ACR text range");
    std::wstring result;
    for(size_t i=offset,end=i+length;i<end;i+=2) {
        uint32_t cp=uint32_t(bytes[i]) | uint32_t(bytes[i+1])<<8;
        if(cp>=0xd800 && cp<=0xdbff) {
            if(i+3>=end) throw std::runtime_error("Truncated ACR surrogate");
            const uint32_t low=uint32_t(bytes[i+2]) | uint32_t(bytes[i+3])<<8;
            if(low<0xdc00 || low>0xdfff) throw std::runtime_error("Invalid ACR surrogate");
            if(sizeof(wchar_t)==2) result.push_back(wchar_t(cp));
            cp=sizeof(wchar_t)==2 ? low : 0x10000+((cp-0xd800)<<10)+(low-0xdc00);
            i+=2;
        } else if(cp>=0xdc00 && cp<=0xdfff) throw std::runtime_error("Invalid ACR surrogate");
        if(!cp) throw std::runtime_error("Embedded NUL in ACR text");
        result.push_back(wchar_t(cp));
    }
    return result;
}
}
AcrTranslation::AcrTranslation(const std::vector<uint8_t>& bytes) {
    if(bytes.size()<16 || bytes.size()>64*1024*1024 || U32(bytes.data()+4)!=0 ||
        U32(bytes.data()+8)!=bytes.size()-16 || U32(bytes.data()+12)!=bytes.size()-16)
        throw std::runtime_error("Unsupported or truncated ACR translation table");
    const auto count=U32(bytes.data());
    if(count>200000 || count>(bytes.size()-16)/20)
        throw std::runtime_error("Invalid ACR entry count");
    const size_t tableEnd=16+size_t(count)*20;
    for(size_t i=0;i<count;++i) {
        const auto* row=bytes.data()+16+i*20;
        auto original=Text(bytes,tableEnd,U32(row+4),U32(row+8));
        auto translated=Text(bytes,tableEnd,U32(row+12),U32(row+16));
        if(!original.empty()) entries_.emplace(std::move(original),std::move(translated));
    }
}
const std::wstring* AcrTranslation::Find(const wchar_t* text) const {
    const auto found=entries_.find(text ? text : L"");
    return found==entries_.end() ? nullptr : &found->second;
}
bool AcrTranslation::TranslateMessage(std::wstring& text) const {
    std::wstring plain, prefix, suffix;
    for (size_t at = 0; at < text.size();) {
        if (text[at] == L'\r' || text[at] == L'\n') { ++at; continue; }
        if (text[at] != L'\\') {
            // Moving an interior style change to the start would change its
            // meaning. Leave such messages untouched.
            if (!suffix.empty()) return false;
            plain += text[at++];
            continue;
        }
        const size_t start = at++;
        if (at == text.size()) return false;
        const wchar_t command = text[at++];
        if (command == L'\\') {
            if (!suffix.empty()) return false;
            plain += L'\\';
            continue;
        }
        if (text.compare(start, 3, L"\\n:") == 0) { at = start + 3; continue; }
        if (command == L'r' && at < text.size() && text[at] == L';') {
            const auto wordEnd = text.find(L';', ++at);
            if (wordEnd == std::wstring::npos) return false;
            const auto rubyEnd = text.find(L':', wordEnd + 1);
            if (rubyEnd == std::wstring::npos || !suffix.empty()) return false;
            // Japanese furigana belongs to the source word, not its translation.
            plain.append(text, at, wordEnd - at);
            at = rubyEnd + 1;
            continue;
        }
        if (command == L'b' || command == L'i') {
            // No delimiter for bold/italic flags.
        } else if (command == L'n' && at < text.size() &&
                   (text[at] == L'b' || text[at] == L'i')) {
            ++at;
        } else if (command == L'f' || command == L's' || command == L'h' ||
                   command == L'c' || command == L'v') {
            const auto end = text.find(L':', at);
            if (end == std::wstring::npos) return false;
            at = end + 1;
        } else return false;
        (plain.empty() ? prefix : suffix).append(text, start, at - start);
    }
    const auto* replacement = Find(plain.c_str());
    if (!replacement) return false;
    text = std::move(prefix);
    for (const auto c : *replacement) {
        text += c;
        if (c == L'\\') text += c;
    }
    text += suffix;
    return true;
}
}
