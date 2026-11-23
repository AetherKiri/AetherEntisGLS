"""Preserve pre-header Cotopha entry and dynamic-member semantics.

Natsu.exe: 0x4026e0 looks up main by name and supplies no arguments;
0x41b3a0 -> 0x4a1ee0 replaces an existing dynamic member in place.
Modern images retain the SDK's explicit entry and duplicate-member checks.
"""
from pathlib import Path


def replace(text, old, new):
    if text.count(old) != 1:
        raise ValueError('headerless Cotopha patch anchor: ' + old)
    return text.replace(old, new)


def patch_source(relative_path, text):
    name = Path(relative_path).name
    if name == 'glsscriptobj.h':
        start = text.index('\tconst ECSStrTagArray & operator = ( const ECSStrBufTagArray & array )')
        end = text.index('\n\t//', start)
        # LoadArray restores interned names in the base tag array. Copying only
        # the owned-string pool drops those names while retaining sorted indices.
        text = text[:start] + '''    const ECSStrTagArray& operator=(const ECSStrBufTagArray& array)
    {
        if (this == &array) return *this;
        RemoveAll();
        for (unsigned i = 0; i < array.GetSize(); ++i) Add(array.GetAt(i));
        return *this;
    }''' + text[end:]
    elif name == 'glscs_execution_image.h':
        text = replace(text, '\tEWStrFuncEntryArray\tm_wstaFunc ;',
                       '    bool m_headerlessImage = false;\n\tEWStrFuncEntryArray\tm_wstaFunc ;')
        text = replace(text, '\tDWORD * GetFunctionAddress(',
                       '    bool IsHeaderlessImage() const { return m_headerlessImage; }\n\tDWORD * GetFunctionAddress(')
    elif name == 'glscs_execution_image.cpp':
        start = text.index('ESLError ECSExecutionImage::ReadExecution(')
        end = text.index('ESLError ECSExecutionImage::WriteExecution(', start)
        body = text[start:end]
        body = replace(body, '\tDeleteImage( ) ;', '\tDeleteImage( ) ;\n    m_headerlessImage = true;')
        body = replace(body, '\t\tif ( idRec == *((UINT64*)"header  ") )\n\t\t{',
                       '\t\tif ( idRec == *((UINT64*)"header  ") )\n\t\t{\n            m_headerlessImage = false;')
        body = replace(body, '\temcfile.Close( ) ;', '''    if (m_headerlessImage) {
        m_exiHeader.nStackSize = m_exiHeader.nHeapSize = 0x1000;
        m_exiHeader.fnStaticInitialize = m_exiHeader.fnResumePrepare = UINT32_MAX;
        const auto* entry = GetFunctionAddress(L"main");
        m_exiHeader.fnEntryPoint = entry ? *entry : UINT32_MAX;
    }
\temcfile.Close( ) ;''')
        text = text[:start] + body + text[end:]
    elif name == 'glscs_context.cpp':
        text = replace(text, '\t\terr = pThisStruct->AddNewVariable( *pwstrName, pNewObj ) ;', '''        const int previous = m_pcsxi->IsHeaderlessImage() && !pThisStruct->m_pClassInf
            ? pThisStruct->FindVariableIndex(*pwstrName) : -1;
        if (previous >= 0) {
            pThisStruct->m_varArray.SetAt(previous, pNewObj);
            err = eslErrSuccess;
        } else {
            err = pThisStruct->AddNewVariable(*pwstrName, pNewObj);
        }''')
        start = text.index('ESLError ECSContext::CallGlobalFunction(')
        end = text.index('FARPROC ECSContext::FindPluginedFunction(', start)
        body = text[start:end]
        body = replace(body, '\tAPI_ECS_FUNC\tapiCallFunc = GetImportNativeFunction( pwszFuncName ) ;', '''    // Early scripts issue an unqualified UpdateWindow() after waiting for
    // sprite activation. Win32 resolves the name from user32; portable hosts
    // explicitly refresh the game's screen instead of invoking a foreign ABI.
    if (m_pcsxi->IsHeaderlessImage() && !EWideString::Compare(pwszFuncName, L"UpdateWindow") &&
        m_arg.m_varArray.GetSize() == 0) {
        auto* screen = ESLTypeCast<ECSWindow>(ECSObject::GetEntity(m_pcsxi->GetGlobalObject(L"screen")));
        if (!screen) return ESLErrorMsg("UpdateWindow requires the game's screen Window");
        return PushObject(new ECSInteger(screen->GetWindow()->UpdateWindow()));
    }
\tAPI_ECS_FUNC\tapiCallFunc = GetImportNativeFunction( pwszFuncName ) ;''')
        text = text[:start] + body + text[end:]
    return text
