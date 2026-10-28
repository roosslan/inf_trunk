#include "stdafx.h"

#include "component.h"

#include "dataobject.h"
#include "images.h"

namespace {

template <class T>
void releaseInterface(T *&p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

bool asNumber(const std::wstring &s, double &out)
{
    if (s.empty())
        return false;
    wchar_t *end = nullptr;
    out = wcstod(s.c_str(), &end);
    return end && *end == 0;
}

} // namespace

CComponent::CComponent(CComponentData *owner) : m_owner(owner)
{
    m_owner->AddRef();
    m_owner->registerComponent(this);
}

CComponent::~CComponent()
{
    m_owner->unregisterComponent(this);
    m_owner->Release();
}

STDMETHODIMP CComponent::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IComponent)
        *ppv = static_cast<IComponent *>(this);
    else if (riid == IID_IExtendContextMenu)
        *ppv = static_cast<IExtendContextMenu *>(this);
    else if (riid == IID_IExtendPropertySheet || riid == IID_IExtendPropertySheet2)
        *ppv = static_cast<IExtendPropertySheet2 *>(this);
    else if (riid == IID_IResultDataCompareEx)
        *ppv = static_cast<IResultDataCompareEx *>(this);
    else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP CComponent::Initialize(LPCONSOLE console)
{
    trace(L"C::Initialize");
    MANAGE_STATE();
    if (!console)
        return E_POINTER;
    console->QueryInterface(IID_IConsole2, reinterpret_cast<void **>(&m_console));
    console->QueryInterface(IID_IResultData, reinterpret_cast<void **>(&m_result));
    console->QueryInterface(IID_IHeaderCtrl2, reinterpret_cast<void **>(&m_header));
    console->QueryConsoleVerb(&m_verb);
    return m_console && m_result && m_header ? S_OK : E_UNEXPECTED;
}

STDMETHODIMP CComponent::Destroy(MMC_COOKIE)
{
    trace(L"C::Destroy");
    MANAGE_STATE();
    m_current = nullptr;
    releaseInterface(m_verb);
    releaseInterface(m_header);
    releaseInterface(m_result);
    releaseInterface(m_console);
    return S_OK;
}

STDMETHODIMP CComponent::Notify(LPDATAOBJECT obj, MMC_NOTIFY_TYPE event, LPARAM arg, LPARAM param)
{
    trace(L"C::Notify event=%x arg=%llx param=%llx", unsigned(event), (unsigned long long)arg, (unsigned long long)param);
    MANAGE_STATE();
    switch (event) {
    case MMCN_SHOW: {
        const Target t = m_owner->targetOf(obj);
        if (arg) {
            show(t.node);
            m_owner->nodeSelected(t.node);
        } else {
            m_current = nullptr;
        }
        return S_OK;
    }
    case MMCN_ADD_IMAGES:
        loadImages(reinterpret_cast<IImageList *>(arg));
        return S_OK;
    case MMCN_SELECT:
        setVerbs(m_owner->targetOf(obj), HIWORD(arg) != 0);
        return S_OK;
    case MMCN_DBLCLICK:
        // действие по умолчанию: «Открыть» для узлов, «Свойства» для элементов
        return S_FALSE;
    case MMCN_DELETE:
        m_owner->deleteTarget(m_owner->targetOf(obj));
        return S_OK;
    case MMCN_RENAME:
        if (!param)
            return m_owner->canRename(m_owner->targetOf(obj)) ? S_OK : S_FALSE;
        return m_owner->renameTarget(m_owner->targetOf(obj), reinterpret_cast<LPOLESTR>(param)) ? S_OK : S_FALSE;
    case MMCN_REFRESH:
        m_owner->refreshTarget(m_owner->targetOf(obj));
        if (m_current && !m_current->file)
            populate();
        return S_OK;
    case MMCN_PROPERTY_CHANGE:
        m_owner->applyPropertyChange(param);
        return S_OK;
    case MMCN_COLUMN_CLICK:
    case MMCN_VIEW_CHANGE:
        return S_OK;
    default:
        return S_FALSE;
    }
}

void CComponent::show(Node *node)
{
    m_current = node;
    if (!node || !m_header)
        return;
    const auto columns = columnsFor(*node);
    for (int i = 0; i < int(columns.size()); ++i)
        m_header->InsertColumn(i, columns[size_t(i)].title, LVCFMT_LEFT, columns[size_t(i)].width);
    populate();
}

void CComponent::populate()
{
    if (!m_result)
        return;
    m_result->DeleteAllRsltItems();
    // прежние строки живут, пока MMC может запросить их текст
    m_rows = m_current ? buildRows(*m_current) : std::vector<std::shared_ptr<ResultRow>>();
    for (const auto &row : m_rows) {
        RESULTDATAITEM item{};
        item.mask = RDI_STR | RDI_IMAGE | RDI_PARAM;
        item.str = MMC_CALLBACK;
        item.nImage = row->image;
        item.lParam = reinterpret_cast<LPARAM>(row.get());
        m_result->InsertItem(&item);
    }
    if (m_current && m_current->file) {
        const InfFileDoc &f = *m_current->file;
        const std::wstring desc = (f.isUntitled() ? L"(не сохранён)" : f.path()) + (f.isModified() ? L" — изменён" : L"");
        m_result->SetDescBarText(const_cast<LPOLESTR>(desc.c_str()));
    } else if (m_result) {
        m_result->SetDescBarText(const_cast<LPOLESTR>(L""));
    }
}

void CComponent::setVerbs(const Target &t, bool select)
{
    if (!m_verb || !select)
        return;
    const bool isRow = t.row != nullptr;
    const bool props = m_owner->hasPages(t);
    m_verb->SetVerbState(MMC_VERB_PROPERTIES, ENABLED, props);
    m_verb->SetVerbState(MMC_VERB_REFRESH, ENABLED, TRUE);
    bool canDelete = false;
    if (isRow)
        canDelete = t.row->canDelete();
    else if (t.node)
        canDelete = t.node->type == NodeType::File || t.node->type == NodeType::Section || t.node->type == NodeType::StringSection;
    m_verb->SetVerbState(MMC_VERB_DELETE, ENABLED, canDelete);
    m_verb->SetVerbState(MMC_VERB_RENAME, ENABLED, m_owner->canRename(t));
    if (isRow)
        m_verb->SetDefaultVerb(props ? MMC_VERB_PROPERTIES : MMC_VERB_NONE);
    else
        m_verb->SetDefaultVerb(MMC_VERB_OPEN);
}

void CComponent::nodeRemoved(Node *node)
{
    if (m_current && m_current->isInside(node))
        m_current = nullptr;
}

void CComponent::systemChanged()
{
    if (m_current && isSystemNode(m_current->type))
        populate();
}

void CComponent::fileChanged(InfFileDoc *file)
{
    if (m_current && m_current->file == file)
        populate();
}

STDMETHODIMP CComponent::QueryDataObject(MMC_COOKIE cookie, DATA_OBJECT_TYPES type, LPDATAOBJECT *obj)
{
    trace(L"C::QueryDataObject cookie=%llx type=%d", (unsigned long long)cookie, int(type));
    MANAGE_STATE();
    if (type == CCT_RESULT) {
        for (const auto &row : m_rows)
            if (reinterpret_cast<MMC_COOKIE>(row.get()) == cookie)
                return CDataObject::create(type, m_current, row, obj);
    }
    return CDataObject::create(type, m_owner->nodeFromCookie(cookie), nullptr, obj);
}

STDMETHODIMP CComponent::GetResultViewType(MMC_COOKIE, LPOLESTR *viewType, long *options)
{
    if (viewType)
        *viewType = nullptr;
    if (options)
        *options = MMC_VIEW_OPTIONS_NONE;
    return S_FALSE;
}

STDMETHODIMP CComponent::GetDisplayInfo(RESULTDATAITEM *item)
{
    trace(L"C::GetDisplayInfo");
    MANAGE_STATE();
    if (!item)
        return E_POINTER;
    static thread_local std::wstring buffer;
    if (item->bScopeItem) {
        const Node *node = m_owner->nodeFromCookie(item->lParam);
        if (item->mask & RDI_STR) {
            buffer = scopeItemColumn(*node, item->nCol);
            item->str = buffer.data();
        }
        if (item->mask & RDI_IMAGE)
            item->nImage = node->image();
        return S_OK;
    }
    const auto *row = reinterpret_cast<const ResultRow *>(item->lParam);
    if (!row)
        return S_OK;
    if (item->mask & RDI_STR) {
        buffer = item->nCol >= 0 && item->nCol < int(row->columns.size()) ? row->columns[size_t(item->nCol)] : std::wstring();
        // в колонке списка переносы строк недопустимы
        for (wchar_t &c : buffer)
            if (c == L'\r' || c == L'\n' || c == L'\t')
                c = L' ';
        item->str = buffer.data();
    }
    if (item->mask & RDI_IMAGE)
        item->nImage = row->image;
    return S_OK;
}

STDMETHODIMP CComponent::CompareObjects(LPDATAOBJECT a, LPDATAOBJECT b)
{
    return m_owner->CompareObjects(a, b);
}

STDMETHODIMP CComponent::AddMenuItems(LPDATAOBJECT obj, LPCONTEXTMENUCALLBACK callback, long *allowed)
{
    MANAGE_STATE();
    return m_owner->addMenuItems(m_owner->targetOf(obj), callback, allowed);
}

STDMETHODIMP CComponent::Command(long id, LPDATAOBJECT obj)
{
    MANAGE_STATE();
    return m_owner->command(id, m_owner->targetOf(obj));
}

STDMETHODIMP CComponent::CreatePropertyPages(LPPROPERTYSHEETCALLBACK provider, LONG_PTR handle, LPDATAOBJECT obj)
{
    MANAGE_STATE();
    return m_owner->createPages(m_owner->targetOf(obj), provider, handle);
}

STDMETHODIMP CComponent::QueryPagesFor(LPDATAOBJECT obj)
{
    MANAGE_STATE();
    return m_owner->hasPages(m_owner->targetOf(obj)) ? S_OK : S_FALSE;
}

std::wstring CComponent::columnText(const RDITEMHDR *item, int column) const
{
    if (item->dwFlags & RDCI_ScopeItem)
        return scopeItemColumn(*m_owner->nodeFromCookie(item->cookie), column);
    const auto *row = reinterpret_cast<const ResultRow *>(item->cookie);
    return row && column < int(row->columns.size()) ? row->columns[size_t(column)] : std::wstring();
}

STDMETHODIMP CComponent::Compare(RDCOMPARE *compare, int *result)
{
    MANAGE_STATE();
    if (!compare || !result)
        return E_POINTER;
    const bool scope1 = (compare->prdch1->dwFlags & RDCI_ScopeItem) != 0;
    const bool scope2 = (compare->prdch2->dwFlags & RDCI_ScopeItem) != 0;
    if (scope1 != scope2) {
        // узлы дерева всегда выше элементов
        *result = scope1 ? -1 : 1;
        return S_OK;
    }
    const std::wstring a = columnText(compare->prdch1, compare->nColumn);
    const std::wstring b = columnText(compare->prdch2, compare->nColumn);
    double na = 0, nb = 0;
    if (asNumber(a, na) && asNumber(b, nb))
        *result = na < nb ? -1 : (na > nb ? 1 : 0);
    else
        *result = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, a.c_str(), int(a.size()),
                                  b.c_str(), int(b.size()), nullptr, nullptr, 0) - 2;
    return S_OK;
}
