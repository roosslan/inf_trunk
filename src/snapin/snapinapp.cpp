#include "stdafx.h"

#include "snapinapp.h"

#include "about.h"
#include "componentdata.h"
#include "guids.h"
#include "infverif.h"

CInfSnapinApp theApp;

namespace {

LONG g_objects = 0;
LONG g_locks = 0;

const wchar_t *kSettingsKey = L"Software\\inf_editor";

std::wstring readString(HKEY key, const wchar_t *name)
{
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ)
        return {};
    std::wstring s(size / sizeof(wchar_t) + 1, L'\0');
    RegQueryValueExW(key, name, nullptr, nullptr, reinterpret_cast<BYTE *>(s.data()), &size);
    s.resize(wcsnlen(s.c_str(), s.size()));
    return s;
}

void writeString(HKEY key, const wchar_t *name, const std::wstring &value)
{
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()),
                   DWORD((value.size() + 1) * sizeof(wchar_t)));
}

// Фабрика классов для объектов оснастки
template <class T>
class ClassFactory : public IClassFactory, public ComRefCounted {
public:
    IMPLEMENT_REFCOUNT()

    STDMETHOD(QueryInterface)(REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHOD(CreateInstance)(IUnknown *outer, REFIID riid, void **ppv) override
    {
        MANAGE_STATE();
        if (!ppv)
            return E_POINTER;
        *ppv = nullptr;
        if (outer)
            return CLASS_E_NOAGGREGATION;
        T *obj = new (std::nothrow) T();
        if (!obj)
            return E_OUTOFMEMORY;
        const HRESULT hr = obj->QueryInterface(riid, ppv);
        obj->Release();
        return hr;
    }

    STDMETHOD(LockServer)(BOOL lock) override
    {
        if (lock)
            InterlockedIncrement(&g_locks);
        else
            InterlockedDecrement(&g_locks);
        return S_OK;
    }
};

} // namespace

BOOL CInfSnapinApp::InitInstance()
{
    CWinApp::InitInstance();
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES | ICC_DATE_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    AfxInitRichEdit5();
    return TRUE;
}

std::wstring traceDir()
{
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    return dir;
}

bool traceEnabled()
{
    return GetFileAttributesW((traceDir() + L"inf_snapin_trace.log").c_str()) != INVALID_FILE_ATTRIBUTES;
}

void trace(const wchar_t *fmt, ...)
{
    static int enabled = -1;
    static wchar_t path[MAX_PATH];
    if (enabled < 0) {
        GetTempPathW(MAX_PATH, path);
        wcscat_s(path, L"inf_snapin_trace.log");
        enabled = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    }
    if (!enabled)
        return;
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    vswprintf_s(buf, fmt, args);
    va_end(args);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[1200];
    swprintf_s(line, L"%02d:%02d:%02d.%03d [%lu] %s\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId(), buf);
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        const std::string u = inf::toUtf8(line);
        DWORD w = 0;
        WriteFile(h, u.data(), DWORD(u.size()), &w, nullptr);
        CloseHandle(h);
    }
}

void comObjectCreated()
{
    InterlockedIncrement(&g_objects);
}

void comObjectDestroyed()
{
    InterlockedDecrement(&g_objects);
}

bool comCanUnload()
{
    return g_objects == 0 && g_locks == 0;
}

SnapinSettings SnapinSettings::load()
{
    SnapinSettings s;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return s;
    s.infVerifPath = readString(key, L"InfVerifPath");
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(key, L"InfVerifMode", nullptr, &type, nullptr, &size) == ERROR_SUCCESS)
        s.infVerifMode = readString(key, L"InfVerifMode");
    DWORD hidden = 0, hiddenSize = sizeof(hidden);
    if (RegQueryValueExW(key, L"ShowHiddenDevices", nullptr, &type, reinterpret_cast<BYTE *>(&hidden), &hiddenSize) == ERROR_SUCCESS
        && type == REG_DWORD)
        s.showHiddenDevices = hidden != 0;
    if (RegQueryValueExW(key, L"Session", nullptr, &type, nullptr, &size) == ERROR_SUCCESS && type == REG_MULTI_SZ) {
        std::wstring buf(size / sizeof(wchar_t) + 2, L'\0');
        RegQueryValueExW(key, L"Session", nullptr, nullptr, reinterpret_cast<BYTE *>(buf.data()), &size);
        for (const wchar_t *p = buf.c_str(); *p; p += wcslen(p) + 1)
            s.session.push_back(p);
    }
    RegCloseKey(key);
    return s;
}

void SnapinSettings::save() const
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    writeString(key, L"InfVerifPath", infVerifPath);
    writeString(key, L"InfVerifMode", infVerifMode);
    const DWORD hidden = showHiddenDevices ? 1 : 0;
    RegSetValueExW(key, L"ShowHiddenDevices", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&hidden), sizeof(hidden));
    std::wstring multi;
    for (const std::wstring &f : session)
        multi += f + L'\0';
    multi += L'\0';
    RegSetValueExW(key, L"Session", 0, REG_MULTI_SZ, reinterpret_cast<const BYTE *>(multi.data()),
                   DWORD(multi.size() * sizeof(wchar_t)));
    RegCloseKey(key);
}

std::wstring SnapinSettings::resolvedInfVerif() const
{
    return infVerifPath.empty() ? inf::findInfVerif() : infVerifPath;
}

// ------------------------------------------------------------------ экспортируемые функции

STDAPI DllCanUnloadNow()
{
    MANAGE_STATE();
    return comCanUnload() && AfxDllCanUnloadNow() == S_OK ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    MANAGE_STATE();
    if (!ppv)
        return E_POINTER;
    *ppv = nullptr;
    IClassFactory *factory = nullptr;
    if (rclsid == CLSID_InfSnapin)
        factory = new (std::nothrow) ClassFactory<CComponentData>();
    else if (rclsid == CLSID_InfSnapinAbout)
        factory = new (std::nothrow) ClassFactory<CSnapinAbout>();
    else
        return CLASS_E_CLASSNOTAVAILABLE;
    if (!factory)
        return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}
