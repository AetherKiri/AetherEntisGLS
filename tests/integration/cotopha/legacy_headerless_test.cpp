// Asset-free execution images exercising the older, headerless Cotopha ABI.
#include "compatibility/sdk/legacy/gls.h"
#include "platform/sdl/system.h"
#include "runtime/cotopha_port/legacy_skin_manager.h"
#include "runtime/cotopha_port/legacy_sprite.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <memory>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
class Fixture final : public ECSExecutionImage {
public:
    DWORD memberAddress;
    Fixture() {
        std::vector<uint8_t> code{0xff}; // Entry must be resolved by name, not zero.
        auto byte = [&](uint8_t b) { code.push_back(b); };
        auto word = [&](uint32_t v) { for (int i=0;i<4;++i) byte(uint8_t(v>>(8*i))); };
        auto text = [&](const char* s) {
            word(uint32_t(std::strlen(s)));
            while (*s) { byte(uint8_t(*s++)); byte(0); }
        };
        auto enter = [&](const char* name, bool receiver) {
            byte(4); text(name); word(receiver ? 1 : 0);
            if (receiver) { byte(1); text("this"); }
        };
        auto integer = [&](uint32_t v) { byte(2); byte(0); byte(4); word(v); };
        enter("main",false); integer(37); byte(9); byte(0);
        memberAddress=DWORD(code.size());
        enter("replace",true);
        byte(0); byte(2); byte(4); text("value");
        byte(2); byte(2); byte(6); text("value");
        integer(42); byte(3); byte(255); byte(1);
        byte(0); byte(2); byte(4); text("value");
        byte(9); byte(1);
        std::memcpy(m_bufImage.PutBuffer(code.size()),code.data(),code.size());
        m_bufImage.Flush(code.size());
        m_pImage=static_cast<BYTE*>(m_bufImage.ModifyBuffer(0,code.size()));
        m_dwImageSize=DWORD(code.size());
        m_exiHeader.fnEntryPoint=1;
        auto* entry=new FUNC_ENTRY{};
        entry->dwAddress=1;
        m_wstaFunc.SetAs(L"main",entry);
    }
};
uint64_t Read64(const uint8_t* p) {
    uint64_t value=0;
    for(unsigned i=0;i<8;++i) value|=uint64_t(p[i])<<(8*i);
    return value;
}
std::vector<uint8_t> WithoutHeader(const EMemoryFile& file) {
    const auto* bytes=static_cast<const uint8_t*>(file.GetBuffer());
    std::vector<uint8_t> result(bytes,bytes+64);
    for(size_t p=64;p<file.GetLength();) {
        Require(file.GetLength()-p>=16,"fixture record header");
        const auto size=Read64(bytes+p+8);
        Require(size<=file.GetLength()-p-16,"fixture record length");
        if (std::memcmp(bytes+p,"header  ",8)) result.insert(result.end(),bytes+p,bytes+p+16+size);
        p+=16+size;
    }
    const auto size=uint64_t(result.size()-64);
    for(unsigned i=0;i<8;++i) result[56+i]=uint8_t(size>>(8*i));
    return result;
}
void CheckActivation(ECSContext& context) {
    ECSSprite sprite;
    auto call = [&](const wchar_t* name, ECSObject* value) {
        ECSObjArray<ECSObject> args;
        args.Add(new ECSReference(&sprite)); args.Add(value);
        int method = -1;
        Require(!sprite.GetFunction(context, method, name), "resolve activation method");
        Require(!sprite.CallFunction(context, method, args), "call activation method");
        context.delete_CSObject(context.PopObject());
    };
    call(L"SetBlendingEnvelope", new ECSInteger(256));
    call(L"BeginActivation", new ECSInteger(-1));
    Require(!sprite.NativeSprite().IsAction() && sprite.NativeSprite().GetTransparency() == 0,
            "negative duration retains initial pose without starting a multi-day action");
    call(L"BeginActivation", new ECSInteger(0));
    Require(!sprite.NativeSprite().IsAction() && sprite.NativeSprite().GetTransparency() == 256,
            "zero duration reaches the final pose immediately");
    call(L"SetBlendingEnvelope", new ECSInteger(0));
    call(L"BeginActivation", new ECSInteger(100));
    sprite.NativeSprite().AdvanceTime(50);
    Require(sprite.NativeSprite().IsAction() && sprite.NativeSprite().GetTransparency() >= 120 &&
            sprite.NativeSprite().GetTransparency() <= 136, "positive duration still interpolates");
    auto* durations = new ECSArray;
    durations->m_varArray.Add(new ECSInteger(-1));
    call(L"BeginActivation", durations);
    Require(!sprite.NativeSprite().IsAction(), "signed array duration cancels the preceding action");
}
void Exercise(ECSExecutionImage& image, DWORD member, bool legacy) {
    Require(image.IsHeaderlessImage()==legacy,"image format tracked across reads");
    Require(image.m_exiHeader.fnEntryPoint==1,"named entry preserves nonzero address");
    Require(image.m_exiHeader.fnStaticInitialize==UINT32_MAX,"no spurious static initializer");
    ECSContext context;
    Require(!context.InitializeContext(&image),"initialize fixture");
    ECSObjArray<ECSObject> args;
    Require(!context.CallFunction(image.m_exiHeader.fnEntryPoint,args),"call zero-argument main");
    INT64 value=0;
    Require(context.GetReturnValue() && !context.GetReturnValue()->OperateInteger(value) && value==37,
            "named main executes correct bytecode");
    ECSStructure object;
    args.Add(new ECSReference(&object));
    const auto error=context.CallFunction(member,args);
    Require(legacy ? !error : error!=eslErrSuccess,"duplicate member follows image format");
    Require(object.m_varArray.GetSize()==1 && object.GetMemberAsInt(L"value",-1)==(legacy ? 0 : 42),
            "replacement resets value in its original slot; modern declaration is rejected");
    args.RemoveAll();
    ECSStructure original;
    original.m_pwszTag = L"Bookmark";
    Require(!original.AddNewVariable(L"fSave", new ECSInteger(-1)), "bookmark flag");
    Require(!original.AddNewVariable(L"comment", new ECSString(L"saved text")), "bookmark comment");
    EMemoryFile saved;
    Require(!saved.Create(4096) && !original.Save(saved, context), "save dynamic members");
    saved.Seek(0, ESLFileObject::FromBegin);
    ECSStructure restored;
    Require(!restored.Load(saved, context), "load dynamic members");
    Require(restored.GetMemberAsInt(L"fSave", 0) == -1 && restored.FindVariableIndex(L"comment") == 1,
            "dynamic member names survive save/load");
    std::unique_ptr<ECSObject> copied(restored.Duplicate());
    Require(static_cast<ECSStructure*>(copied.get())->GetMemberAsInt(L"fSave", 0) == -1,
            "restored dynamic member names survive duplication");
    CheckActivation(context);
    context.ReleaseContext(true);
}
void CheckNestedForms() {
    class Skin : public LegacySkinManager {
    public:
        SSystem::SXMLDocument& Page(const wchar_t* id) {
            auto* form = new SSystem::SXMLDocument;
            form->SetTag(L"page"); form->SetAttributeAs(L"buffered", L"false");
            form->SetAttributeAs(L"id", id);
            m_ssoaForm.SetAs(id, form); return *form;
        }
    } skin;
    auto& leaf = skin.Page(L"leaf");
    auto* text = new SSystem::SXMLDocument;
    text->SetTag(L"static_text"); text->SetAttributeAs(L"id", L"label");
    text->SetAttributeAs(L"width", L"100"); text->SetAttributeAs(L"height", L"30");
    leaf.AddElement(text);
    auto add = [](SSystem::SXMLDocument& form, const wchar_t* style, const wchar_t* id) {
        auto* item = new SSystem::SXMLDocument;
        item->SetTag(L"object"); item->SetAttributeAs(L"style", style);
        item->SetAttributeAs(L"id", id); item->SetAttributeAs(L"x", L"24");
        form.AddElement(item);
    };
    auto& middle = skin.Page(L"middle"); add(middle, L"leaf", L"slot");
    auto& root = skin.Page(L"root"); add(root, L"middle", L"group");
    std::unique_ptr<SakuraGL::SGLSprite> sprite(skin.CreateFormedSprite(L"root"));
    Require(sprite && sprite->GetItemAs(L"group\\slot\\label"), "nested form controls retain ID paths");
    Require(!std::wcscmp(sprite->GetID(), L"root"), "form page retains its command ID prefix");
    Require(sprite->GetItemAs(L"group")->GetPosition().x == 24, "nested form position");
    auto& cycle = skin.Page(L"cycle"); add(cycle, L"cycle", L"recursive");
    std::unique_ptr<SakuraGL::SGLSprite> rejected(skin.CreateFormedSprite(L"cycle"));
    Require(!rejected, "cyclic skin references fail without unbounded recursion");
}
}
void Run() {
    struct Runtime {
        Runtime() { ECotophaScript::Initialize(0); }
        ~Runtime() { ECotophaScript::Release(); }
    } runtime;
        Fixture fixture;
        EMemoryFile encoded;
        Require(!encoded.Create(4096) && !fixture.WriteExecution(encoded),"serialize synthetic fixture");
        const auto old=WithoutHeader(encoded);
        EMemoryFile legacy;
        Require(!legacy.Create(old.size()) && legacy.Write(old.data(),old.size())==old.size(),"write old fixture");
        legacy.Seek(0,ESLFileObject::FromBegin);
        ECSExecutionImage image;
        Require(!image.ReadExecution(legacy),"read headerless image");
        Exercise(image,fixture.memberAddress,true);
        encoded.Seek(0,ESLFileObject::FromBegin);
        Require(!image.ReadExecution(encoded),"read modern image into reused image");
        Require(!image.IsHeaderlessImage(), "rereading resets the format marker");
        encoded.Seek(0,ESLFileObject::FromBegin);
        ECSExecutionImage modern;
        Require(!modern.ReadExecution(encoded), "read modern fixture");
        Exercise(modern,fixture.memberAddress,false);
        CheckNestedForms();
        std::puts("headerless entry and member semantics: PASS");
}
int main() {
    if (!SDL_Init(0)) return 2;
    int result=0;
    bool initialized=false,created=false;
    std::filesystem::path root;
    try {
        namespace fs=std::filesystem;
        root=fs::temp_directory_path()/("entis-headerless-test-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        created=fs::create_directory(root);
        Require(created,"create isolated fixture root");
        for(const char* part : {"assets","storage","local","game"}) fs::create_directory(root/part);
        study::platform::sdl::SystemPaths paths;
        paths.assetsRoot=(root/"assets").string(); paths.storageRoot=(root/"storage").string();
        paths.localRoot=(root/"local").string(); paths.gameRoot=(root/"game").string();
        Require(study::platform::sdl::ConfigureSystemPaths(paths),"configure fixture paths");
        SakuraGL::Initialize(); initialized=true;
        Run();
    } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); result=1; }
    if(initialized) SakuraGL::Finalize();
    SDL_Quit();
    if(created) std::filesystem::remove_all(root);
    return result;
}
