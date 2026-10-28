// Приложение MFC (обычная DLL), счётчик объектов COM и параметры оснастки
#pragma once

#include <string>
#include <vector>

class CInfSnapinApp : public CWinApp {
public:
    BOOL InitInstance() override;
};

// Трассировка вызовов MMC в %TEMP%\inf_snapin_trace.log (пишется, только если файл уже существует)
void trace(const wchar_t *fmt, ...);
bool traceEnabled();
std::wstring traceDir();  // директория временных файлов с завершающей косой чертой

// Счётчик живых объектов COM для DllCanUnloadNow
void comObjectCreated();
void comObjectDestroyed();
bool comCanUnload();

// Параметры оснастки в HKCU\Software\inf_editor
struct SnapinSettings {
    std::wstring infVerifPath;          // пусто — искать автоматически
    std::wstring infVerifMode = L"/w";
    bool showHiddenDevices = false;     // ветка «Устройства компьютера»
    std::vector<std::wstring> session;  // открытые файлы

    static SnapinSettings load();
    void save() const;
    std::wstring resolvedInfVerif() const;
};

// Базовый класс со счётчиком ссылок для объектов COM оснастки
class ComRefCounted {
public:
    ComRefCounted() { comObjectCreated(); }
    virtual ~ComRefCounted() { comObjectDestroyed(); }

protected:
    ULONG addRef() { return ULONG(InterlockedIncrement(&m_ref)); }
    ULONG release()
    {
        const LONG r = InterlockedDecrement(&m_ref);
        if (r == 0)
            delete this;
        return ULONG(r);
    }

private:
    LONG m_ref = 1;
};

#define IMPLEMENT_REFCOUNT()                                                                                           \
    STDMETHOD_(ULONG, AddRef)() override { return addRef(); }                                                          \
    STDMETHOD_(ULONG, Release)() override { return release(); }
