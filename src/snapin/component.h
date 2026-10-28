// Объект представления IComponent: область результатов одного окна консоли
#pragma once

#include "componentdata.h"

class CComponent : public IComponent,
                   public IExtendContextMenu,
                   public IExtendPropertySheet2,
                   public IResultDataCompareEx,
                   public ComRefCounted {
public:
    explicit CComponent(CComponentData *owner);
    ~CComponent() override;

    IMPLEMENT_REFCOUNT()
    STDMETHOD(QueryInterface)(REFIID riid, void **ppv) override;

    // IComponent
    STDMETHOD(Initialize)(LPCONSOLE console) override;
    STDMETHOD(Notify)(LPDATAOBJECT obj, MMC_NOTIFY_TYPE event, LPARAM arg, LPARAM param) override;
    STDMETHOD(Destroy)(MMC_COOKIE cookie) override;
    STDMETHOD(QueryDataObject)(MMC_COOKIE cookie, DATA_OBJECT_TYPES type, LPDATAOBJECT *obj) override;
    STDMETHOD(GetResultViewType)(MMC_COOKIE cookie, LPOLESTR *viewType, long *options) override;
    STDMETHOD(GetDisplayInfo)(RESULTDATAITEM *item) override;
    STDMETHOD(CompareObjects)(LPDATAOBJECT a, LPDATAOBJECT b) override;

    // IExtendContextMenu
    STDMETHOD(AddMenuItems)(LPDATAOBJECT obj, LPCONTEXTMENUCALLBACK callback, long *allowed) override;
    STDMETHOD(Command)(long id, LPDATAOBJECT obj) override;

    // IExtendPropertySheet2
    STDMETHOD(CreatePropertyPages)(LPPROPERTYSHEETCALLBACK provider, LONG_PTR handle, LPDATAOBJECT obj) override;
    STDMETHOD(QueryPagesFor)(LPDATAOBJECT obj) override;
    STDMETHOD(GetWatermarks)(LPDATAOBJECT, HBITMAP *, HBITMAP *, HPALETTE *, BOOL *) override { return S_FALSE; }

    // IResultDataCompareEx
    STDMETHOD(Compare)(RDCOMPARE *compare, int *result) override;

    // уведомления от CComponentData
    void nodeRemoved(Node *node);
    void fileChanged(InfFileDoc *file);
    void systemChanged();

private:
    void show(Node *node);
    void populate();
    void setVerbs(const Target &t, bool select);
    std::wstring columnText(const RDITEMHDR *item, int column) const;

    CComponentData *m_owner;
    IConsole2 *m_console = nullptr;
    IResultData *m_result = nullptr;
    IHeaderCtrl2 *m_header = nullptr;
    IConsoleVerb *m_verb = nullptr;
    Node *m_current = nullptr;
    std::vector<std::shared_ptr<ResultRow>> m_rows;
};
