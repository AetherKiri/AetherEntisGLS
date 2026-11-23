#pragma once
#include <sakuraglx/sprite/sglx_resource_manager.h>
#include <sakuraglx/sprite/sglx_sprite_formed.h>
#include <memory>

// GLS3 skins embed another page with <object style="page-id">. The newer
// builder ignores that element, losing both its controls and their ID paths.
class LegacySkinManager : public SakuraGL::SGLSkinManager {
    unsigned depth_ = 0;
public:
    SakuraGL::SGLError PrepareFormedPage(SakuraGL::SGLSpriteFormed& page,
                                        SSystem::SXMLDocument& form) override {
        const auto error = SGLSkinManager::PrepareFormedPage(page, form);
        // GLS3 CreateFormedPage assigns the page ID too. Script command
        // patterns include that prefix before the nested control IDs.
        if (!error) page.SetID(form.GetAttrStringAs(L"id", nullptr));
        return error;
    }
    SakuraGL::SGLError AddFormedPageItems(SakuraGL::SGLSpriteFormed& page,
                                        SSystem::SXMLDocument& form) override {
        using namespace SakuraGL;
        if (depth_ >= 32) return sglErrFailed;
        struct Scope {
            unsigned& depth;
            explicit Scope(unsigned& value) : depth(value) { ++depth; }
            ~Scope() { --depth; }
        } scope(depth_);
        if (const auto error = SGLSkinManager::AddFormedPageItems(page, form)) return error;
        for (size_t i = 0; i < form.GetElementsCount(); ++i) {
            auto* item = form.GetElementAt(i);
            if (!item || item->GetType() != SSystem::SXMLDocument::typeTag ||
                item->GetTag() != L"object") continue;
            const auto* style = item->GetAttributeAs(L"style");
            if (!style) return sglErrFailed;
            std::unique_ptr<SGLSprite> child(CreateFormedSprite(*style));
            if (!child) return sglErrFailed;
            const auto position = child->GetPosition();
            child->SetPosition(item->GetAttrRichIntegerAs(L"x", int64_t(position.x)),
                               item->GetAttrRichIntegerAs(L"y", int64_t(position.y)));
            child->ChangePriority(priorityFirst + int32_t(i) * priorityStep);
            child->SetID(item->GetAttrStringAs(L"id", nullptr));
            if (item->GetAttrStringAs(L"group", L"true") != L"true")
                child->ModifyUIFlag(SGLSprite::uiGroupMember);
            if (item->GetAttrStringAs(L"tab_stop", nullptr) == L"true")
                child->ModifyUIFlag(SGLSprite::uiFocusable);
            if (auto* commands = item->GetElementTagAs(L"command")) {
                for (size_t n = 0; n < commands->GetElementsCount(); ++n) {
                    auto* command = commands->GetElementAt(n);
                    if (command && command->GetType() == SSystem::SXMLDocument::typeTag)
                        child->InvokeCommand(*command);
                }
            }
            if (child->GetUIFlag() & SGLSprite::uiFocusable)
                page.ModifyUIFlag(SGLSprite::uiFocusable);
            page.AddSmartChild(child.release());
        }
        return sglErrSuccess;
    }
};
