// Объект оснастки IComponentData: пространство имён (дерево), открытые файлы,
// команды контекстного меню, окна свойств и применение изменений к файлам
#pragma once

#include "inffiledoc.h"
#include "nodes.h"
#include "snapinapp.h"

#include <functional>
#include <memory>
#include <vector>

class CComponent;

// Элемент, к которому относится действие: узел дерева и, возможно, строка результатов
struct Target {
    Node *node = nullptr;
    std::shared_ptr<ResultRow> row;
    DATA_OBJECT_TYPES type = CCT_UNINITIALIZED;

    InfFileDoc *file() const { return row ? row->file : (node ? node->file : nullptr); }
};

// Изменение из окна свойств: выполняется в основном потоке MMC
struct PropertyChange {
    std::function<void(class CComponentData &)> action;
};

class CComponentData : public IComponentData,
                       public IExtendContextMenu,
                       public IExtendPropertySheet2,
                       public ComRefCounted {
public:
    CComponentData();
    ~CComponentData() override;

    IMPLEMENT_REFCOUNT()
    STDMETHOD(QueryInterface)(REFIID riid, void **ppv) override;

    // IComponentData
    STDMETHOD(Initialize)(LPUNKNOWN unknown) override;
    STDMETHOD(CreateComponent)(LPCOMPONENT *component) override;
    STDMETHOD(Notify)(LPDATAOBJECT obj, MMC_NOTIFY_TYPE event, LPARAM arg, LPARAM param) override;
    STDMETHOD(Destroy)() override;
    STDMETHOD(QueryDataObject)(MMC_COOKIE cookie, DATA_OBJECT_TYPES type, LPDATAOBJECT *obj) override;
    STDMETHOD(GetDisplayInfo)(SCOPEDATAITEM *item) override;
    STDMETHOD(CompareObjects)(LPDATAOBJECT a, LPDATAOBJECT b) override;

    // IExtendContextMenu
    STDMETHOD(AddMenuItems)(LPDATAOBJECT obj, LPCONTEXTMENUCALLBACK callback, long *allowed) override;
    STDMETHOD(Command)(long id, LPDATAOBJECT obj) override;

    // IExtendPropertySheet2
    STDMETHOD(CreatePropertyPages)(LPPROPERTYSHEETCALLBACK provider, LONG_PTR handle, LPDATAOBJECT obj) override;
    STDMETHOD(QueryPagesFor)(LPDATAOBJECT obj) override;
    STDMETHOD(GetWatermarks)(LPDATAOBJECT, HBITMAP *watermark, HBITMAP *header, HPALETTE *palette, BOOL *stretch) override;

    // ---------------------------------------------------------------- для CComponent
    Target targetOf(LPDATAOBJECT obj);
    Node *nodeFromCookie(MMC_COOKIE cookie);
    HRESULT addMenuItems(const Target &t, LPCONTEXTMENUCALLBACK callback, long *allowed);
    HRESULT command(long id, const Target &t);
    HRESULT createPages(const Target &t, LPPROPERTYSHEETCALLBACK provider, LONG_PTR handle);
    bool hasPages(const Target &t) const;
    bool deleteTarget(const Target &t);
    bool canRename(const Target &t) const;
    bool renameTarget(const Target &t, const std::wstring &newName);
    void refreshTarget(const Target &t);
    void applyPropertyChange(LPARAM param);
    void nodeSelected(Node *node);

    void registerComponent(CComponent *c);
    void unregisterComponent(CComponent *c);

    HWND mainWindow() const;
    IConsole2 *console() const { return m_console; }

    // ---------------------------------------------------------------- работа с файлами
    InfFileDoc *fileById(int id) const;
    bool applyTransform(InfFileDoc *f, const Transform &t, bool showError = true);
    void fileChanged(InfFileDoc *f);
    const SnapinSettings &settings() const { return m_settings; }
    void setSettings(const SnapinSettings &s);

private:
    // дерево
    void expand(Node *node, HSCOPEITEM item);
    void insertScopeItem(Node *node);
    void removeScopeNode(Node *node);
    void syncNode(Node *node);
    void updateScopeItem(Node *node);
    void forgetChildren(Node *node);
    Node *fileNode(InfFileDoc *f) const;
    Node *ensureNode(InfFileDoc *f, NodeType type, const std::wstring &key);
    void selectNode(Node *node);

    // файлы
    InfFileDoc *openFile(const std::wstring &path, bool select);
    InfFileDoc *addDocument(std::unique_ptr<InfFileDoc> doc, bool select);
    bool saveFile(InfFileDoc *f, bool forceAs);
    bool closeFile(InfFileDoc *f, bool ask);
    bool maybeReload(InfFileDoc *f);
    void saveSession();

    // команды
    void cmdOpen();
    void cmdWizard();
    void cmdNewEmpty();
    void cmdEditText(InfFileDoc *f, const std::wstring &section, int line);
    void cmdNotepad(InfFileDoc *f);
    void cmdInfVerif(InfFileDoc *f);
    void cmdVerifOutput(InfFileDoc *f);
    void cmdDriverVer(InfFileDoc *f);
    void cmdAddDevice(InfFileDoc *f);
    void cmdAddEntry(InfFileDoc *f, const std::wstring &section);
    void cmdAddSection(InfFileDoc *f);
    void cmdAddString(InfFileDoc *f, const std::wstring &section);
    void cmdAddLanguage(InfFileDoc *f);
    void cmdAddUndefined(InfFileDoc *f);
    void cmdRemoveUnused(InfFileDoc *f);
    void cmdAddFile(InfFileDoc *f);

    // устройства и пакеты драйверов компьютера
    void systemChanged();
    void cmdOpenSystemInf(const std::wstring &publishedName);
    void cmdExportPackage(const std::wstring &publishedName);
    void cmdExportAll();
    void cmdShowInExplorer(const std::wstring &path);
    void cmdCreateInfForDevice(const inf::DeviceInfo &d);
    void cmdCopyIds(const inf::DeviceInfo &d);

    int message(const std::wstring &text, UINT flags) const;

    IConsole2 *m_console = nullptr;
    IConsoleNameSpace2 *m_namespace = nullptr;
    std::unique_ptr<Node> m_root;
    std::vector<std::unique_ptr<InfFileDoc>> m_files;
    std::vector<CComponent *> m_components;
    SnapinSettings m_settings;
    int m_nextId = 1;
    bool m_sessionLoaded = false;
    bool m_checkingDisk = false;
};
