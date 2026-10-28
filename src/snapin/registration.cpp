// Регистрация оснастки: классы COM в HKCR и описание оснастки для MMC в HKLM
#include "stdafx.h"

#include "guids.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

std::wstring guidString(const GUID &g)
{
    wchar_t buf[64];
    StringFromGUID2(g, buf, 64);
    return buf;
}

LSTATUS setValue(HKEY root, const std::wstring &path, const wchar_t *name, const std::wstring &value)
{
    return RegSetKeyValueW(root, path.c_str(), name, REG_SZ, value.c_str(), DWORD((value.size() + 1) * sizeof(wchar_t)));
}

LSTATUS createKey(HKEY root, const std::wstring &path)
{
    HKEY key = nullptr;
    const LSTATUS r = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (r == ERROR_SUCCESS)
        RegCloseKey(key);
    return r;
}

std::wstring modulePath()
{
    wchar_t path[MAX_PATH * 2];
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, DWORD(std::size(path)));
    return path;
}

struct NodeTypeInfo {
    const GUID *guid;
    const wchar_t *name;
};

const NodeTypeInfo kNodeTypes[] = {
    {&NODETYPE_Root, L"Редактор INF: корень"},
    {&NODETYPE_File, L"Редактор INF: файл"},
    {&NODETYPE_Folder, L"Редактор INF: папка"},
    {&NODETYPE_Section, L"Редактор INF: секция"},
    {&NODETYPE_Item, L"Редактор INF: элемент"},
};

HRESULT registerClass(const GUID &clsid, const wchar_t *name, const std::wstring &dll)
{
    const std::wstring key = L"CLSID\\" + guidString(clsid);
    if (setValue(HKEY_CLASSES_ROOT, key, nullptr, name) != ERROR_SUCCESS)
        return SELFREG_E_CLASS;
    setValue(HKEY_CLASSES_ROOT, key + L"\\InprocServer32", nullptr, dll);
    setValue(HKEY_CLASSES_ROOT, key + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    return S_OK;
}

} // namespace

STDAPI DllRegisterServer()
{
    MANAGE_STATE();
    const std::wstring dll = modulePath();
    HRESULT hr = registerClass(CLSID_InfSnapin, kSnapinName, dll);
    if (FAILED(hr))
        return hr;
    hr = registerClass(CLSID_InfSnapinAbout, L"Редактор INF: сведения", dll);
    if (FAILED(hr))
        return hr;

    const std::wstring snapin = L"Software\\Microsoft\\MMC\\SnapIns\\" + guidString(CLSID_InfSnapin);
    if (setValue(HKEY_LOCAL_MACHINE, snapin, L"NameString", kSnapinName) != ERROR_SUCCESS)
        return SELFREG_E_CLASS;
    setValue(HKEY_LOCAL_MACHINE, snapin, L"NameStringIndirect", L"@" + dll + L",-1");
    setValue(HKEY_LOCAL_MACHINE, snapin, L"About", guidString(CLSID_InfSnapinAbout));
    setValue(HKEY_LOCAL_MACHINE, snapin, L"Provider", kSnapinProvider);
    setValue(HKEY_LOCAL_MACHINE, snapin, L"Version", APP_VERSION);
    createKey(HKEY_LOCAL_MACHINE, snapin + L"\\StandAlone");
    for (const NodeTypeInfo &n : kNodeTypes) {
        createKey(HKEY_LOCAL_MACHINE, snapin + L"\\NodeTypes\\" + guidString(*n.guid));
        setValue(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\MMC\\NodeTypes\\" + guidString(*n.guid), nullptr, n.name);
    }
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    MANAGE_STATE();
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, (L"Software\\Microsoft\\MMC\\SnapIns\\" + guidString(CLSID_InfSnapin)).c_str());
    for (const NodeTypeInfo &n : kNodeTypes)
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, (L"Software\\Microsoft\\MMC\\NodeTypes\\" + guidString(*n.guid)).c_str());
    RegDeleteTreeW(HKEY_CLASSES_ROOT, (L"CLSID\\" + guidString(CLSID_InfSnapin)).c_str());
    RegDeleteTreeW(HKEY_CLASSES_ROOT, (L"CLSID\\" + guidString(CLSID_InfSnapinAbout)).c_str());
    return S_OK;
}
