#include "stdafx.h"

#include "componentdata.h"

#include "component.h"
#include "dataobject.h"
#include "dialogs.h"
#include "guids.h"
#include "images.h"
#include "infedit.h"
#include "drvstore.h"
#include "infgenerator.h"
#include "infhelp.h"
#include "infknowledge.h"
#include "inflexer.h"
#include "pages.h"
#include "texteditor.h"
#include "wizard.h"

#include <algorithm>
#include <format>
#include <set>

using inf::Analysis;
using inf::StringList;
using std::wstring;

namespace {

enum CommandId : long {
    CmdOpen = 1,
    CmdWizard,
    CmdNewEmpty,
    CmdSave,
    CmdSaveAs,
    CmdReload,
    CmdUndo,
    CmdRedo,
    CmdEditText,
    CmdNotepad,
    CmdInfVerif,
    CmdVerifOutput,
    CmdDriverVer,
    CmdAddDevice,
    CmdClose,
    CmdAddEntry,
    CmdEditSection,
    CmdAddSection,
    CmdAddString,
    CmdAddLanguage,
    CmdAddUndefined,
    CmdRemoveUnused,
    CmdAddFile,
    CmdGotoSection,
    CmdShowFolder,
    CmdDevOpenInf,
    CmdDevExport,
    CmdDevShowStore,
    CmdDevCreateInf,
    CmdDevCopyIds,
    CmdExportAll,
    CmdRefreshSystem,
    CmdShowHidden,
};

const wchar_t *kFilter = L"INF-файлы (*.inf)|*.inf|Все файлы (*.*)|*.*||";

std::wstring sectionForTarget(const Target &t)
{
    if (t.row)
        return t.row->section;
    if (!t.node)
        return {};
    switch (t.node->type) {
    case NodeType::Section:
    case NodeType::StringSection:
        return t.node->key;
    case NodeType::Version:
        return L"Version";
    case NodeType::Manufacturer:
        return L"Manufacturer";
    default:
        return {};
    }
}

// Следующая версия: последнее число увеличивается на единицу
wstring bumpVersion(const wstring &v)
{
    StringList parts = inf::split(inf::trim(v), L'.');
    if (parts.size() == 1 && parts[0].empty())
        parts = {L"1", L"0", L"0", L"0"};
    while (parts.size() < 4)
        parts.push_back(L"0");
    uint32_t n = 0;
    inf::parseUInt(parts.back(), n);
    parts.back() = std::to_wstring(n + 1);
    return inf::join(parts, L".");
}

// Пункт «Справка по <оснастка>» MMC добавляет в меню «Справка» сама, отдельного файла справки
// у оснастки нет. Меню MMC — всплывающее меню её основного потока: перехватчик сообщений этого
// потока удаляет пункт, когда меню открывается (WM_INITMENUPOPUP).
HHOOK g_menuHook = nullptr;
int g_menuHookUsers = 0;

void removeSnapinHelpItem(HMENU menu)
{
    for (int i = GetMenuItemCount(menu) - 1; i >= 0; --i) {
        wchar_t text[256] = {};
        if (GetMenuStringW(menu, UINT(i), text, int(std::size(text)), MF_BYPOSITION) <= 0)
            continue;
        const wstring item = inf::replaceAll(text, L"&", L"");
        // «О программе "Редактор INF"...» остаётся
        if (inf::contains(item, kSnapinName) && (inf::istartsWith(item, L"Справка") || inf::istartsWith(item, L"Help")))
            DeleteMenu(menu, UINT(i), MF_BYPOSITION);
    }
}

LRESULT CALLBACK menuHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION) {
        const auto *msg = reinterpret_cast<const CWPSTRUCT *>(lParam);
        if (msg->message == WM_INITMENUPOPUP && msg->wParam)
            removeSnapinHelpItem(reinterpret_cast<HMENU>(msg->wParam));
    }
    return CallNextHookEx(g_menuHook, code, wParam, lParam);
}

void installMenuHook()
{
    if (g_menuHookUsers++ == 0)
        g_menuHook = SetWindowsHookExW(WH_CALLWNDPROC, menuHookProc, nullptr, GetCurrentThreadId());
}

void removeMenuHook()
{
    if (g_menuHookUsers > 0 && --g_menuHookUsers == 0 && g_menuHook) {
        UnhookWindowsHookEx(g_menuHook);
        g_menuHook = nullptr;
    }
}

} // namespace

CComponentData::CComponentData()
{
    m_root = std::make_unique<Node>();
    m_root->type = NodeType::Root;
    // ветки устройств компьютера и пакетов драйверов идут перед открытыми файлами
    m_root->addChild(NodeType::Hardware, L"");
    m_root->addChild(NodeType::DriverPackages, L"");
    m_settings = SnapinSettings::load();
    setShowHiddenDevices(m_settings.showHiddenDevices);
}

CComponentData::~CComponentData()
{
    if (m_namespace)
        m_namespace->Release();
    if (m_console)
        m_console->Release();
}

STDMETHODIMP CComponentData::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IComponentData)
        *ppv = static_cast<IComponentData *>(this);
    else if (riid == IID_IExtendContextMenu)
        *ppv = static_cast<IExtendContextMenu *>(this);
    else if (riid == IID_IExtendPropertySheet || riid == IID_IExtendPropertySheet2)
        *ppv = static_cast<IExtendPropertySheet2 *>(this);
    else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

// ------------------------------------------------------------------ IComponentData

STDMETHODIMP CComponentData::Initialize(LPUNKNOWN unknown)
{
    trace(L"CD::Initialize");
    MANAGE_STATE();
    if (!unknown)
        return E_POINTER;
    HRESULT hr = unknown->QueryInterface(IID_IConsole2, reinterpret_cast<void **>(&m_console));
    if (FAILED(hr))
        return hr;
    hr = unknown->QueryInterface(IID_IConsoleNameSpace2, reinterpret_cast<void **>(&m_namespace));
    if (FAILED(hr))
        return hr;
    IImageList *images = nullptr;
    if (SUCCEEDED(m_console->QueryScopeImageList(&images))) {
        loadImages(images);
        images->Release();
    }
    installMenuHook();
    m_menuHook = true;
    return S_OK;
}

STDMETHODIMP CComponentData::CreateComponent(LPCOMPONENT *component)
{
    trace(L"CD::CreateComponent");
    MANAGE_STATE();
    if (!component)
        return E_POINTER;
    auto *c = new (std::nothrow) CComponent(this);
    if (!c)
        return E_OUTOFMEMORY;
    *component = c;
    return S_OK;
}

STDMETHODIMP CComponentData::Notify(LPDATAOBJECT obj, MMC_NOTIFY_TYPE event, LPARAM arg, LPARAM param)
{
    trace(L"CD::Notify event=%x arg=%llx param=%llx", unsigned(event), (unsigned long long)arg, (unsigned long long)param);
    MANAGE_STATE();
    switch (event) {
    case MMCN_EXPAND: {
        const Target t = targetOf(obj);
        Node *node = t.node ? t.node : m_root.get();
        if (arg)
            expand(node, HSCOPEITEM(param));
        return S_OK;
    }
    case MMCN_REMOVE_CHILDREN: {
        // MMC удаляет дочерние элементы (например, при закрытии окна)
        for (Node *n : {m_root.get()}) {
            std::vector<Node *> stack = {n};
            while (!stack.empty()) {
                Node *cur = stack.back();
                stack.pop_back();
                if (cur->hItem == HSCOPEITEM(arg)) {
                    forgetChildren(cur);
                    return S_OK;
                }
                for (auto &c : cur->children)
                    stack.push_back(c.get());
            }
        }
        return S_OK;
    }
    case MMCN_PROPERTY_CHANGE:
        applyPropertyChange(param);
        return S_OK;
    case MMCN_DELETE:
        deleteTarget(targetOf(obj));
        return S_OK;
    case MMCN_RENAME:
        // без нового имени MMC только спрашивает, разрешено ли переименование
        if (!param)
            return canRename(targetOf(obj)) ? S_OK : S_FALSE;
        return renameTarget(targetOf(obj), reinterpret_cast<LPOLESTR>(param)) ? S_OK : S_FALSE;
    case MMCN_REFRESH:
        refreshTarget(targetOf(obj));
        return S_OK;
    case MMCN_PRELOAD:
        return S_OK;
    default:
        return S_FALSE;
    }
}

STDMETHODIMP CComponentData::Destroy()
{
    trace(L"CD::Destroy");
    MANAGE_STATE();
    if (m_menuHook) {
        removeMenuHook();
        m_menuHook = false;
    }
    // несохранённые изменения: закрыть консоль отменить нельзя, поэтому только «Да/Нет»
    for (auto &f : m_files) {
        if (!f->isModified())
            continue;
        if (message(std::format(L"Сохранить изменения в файле «{}»?", f->fileName()), MB_YESNO | MB_ICONQUESTION) == IDYES)
            saveFile(f.get(), false);
    }
    saveSession();
    m_components.clear();
    m_root->children.clear();
    m_files.clear();
    if (m_namespace) {
        m_namespace->Release();
        m_namespace = nullptr;
    }
    if (m_console) {
        m_console->Release();
        m_console = nullptr;
    }
    return S_OK;
}

STDMETHODIMP CComponentData::QueryDataObject(MMC_COOKIE cookie, DATA_OBJECT_TYPES type, LPDATAOBJECT *obj)
{
    trace(L"CD::QueryDataObject cookie=%llx type=%d", (unsigned long long)cookie, int(type));
    MANAGE_STATE();
    return CDataObject::create(type, nodeFromCookie(cookie), nullptr, obj);
}

STDMETHODIMP CComponentData::GetDisplayInfo(SCOPEDATAITEM *item)
{
    trace(L"CD::GetDisplayInfo");
    MANAGE_STATE();
    if (!item)
        return E_POINTER;
    Node *node = nodeFromCookie(item->lParam);
    if (item->mask & SDI_STR) {
        // MMC хранит указатель, поэтому строка должна жить до следующего вызова
        static thread_local std::wstring buffer;
        buffer = node->displayName();
        item->displayname = buffer.data();
    }
    if (item->mask & SDI_IMAGE)
        item->nImage = node->image();
    if (item->mask & SDI_OPENIMAGE)
        item->nOpenImage = node->openImage();
    return S_OK;
}

STDMETHODIMP CComponentData::CompareObjects(LPDATAOBJECT a, LPDATAOBJECT b)
{
    MANAGE_STATE();
    const CDataObject *da = extractDataObject(a);
    const CDataObject *db = extractDataObject(b);
    if (!da || !db)
        return S_FALSE;
    if (da->node() != db->node())
        return S_FALSE;
    if (!da->row() && !db->row())
        return S_OK;
    if (!da->row() || !db->row())
        return S_FALSE;
    const ResultRow &ra = *da->row(), &rb = *db->row();
    return ra.type == rb.type && ra.fileId == rb.fileId && ra.key == rb.key && ra.lineText == rb.lineText && ra.logicalLine == rb.logicalLine
                   && ra.diagnostic.message == rb.diagnostic.message
               ? S_OK
               : S_FALSE;
}

// ------------------------------------------------------------------ дерево

Node *CComponentData::nodeFromCookie(MMC_COOKIE cookie)
{
    return cookie ? reinterpret_cast<Node *>(cookie) : m_root.get();
}

Target CComponentData::targetOf(LPDATAOBJECT obj)
{
    Target t;
    const CDataObject *d = extractDataObject(obj);
    if (!d)
        return t;
    // объект данных мог пережить удалённый узел или закрытый файл
    if (d->row() && d->row()->file && !fileById(d->row()->fileId))
        return t;
    Node *node = d->node() ? d->node() : m_root.get();
    std::vector<Node *> stack = {m_root.get()};
    bool live = false;
    while (!stack.empty() && !live) {
        Node *cur = stack.back();
        stack.pop_back();
        live = cur == node;
        for (auto &c : cur->children)
            stack.push_back(c.get());
    }
    if (!live && !d->row())
        return t;
    t.node = live ? node : m_root.get();
    t.row = d->row();
    t.type = d->type();
    return t;
}

void CComponentData::expand(Node *node, HSCOPEITEM item)
{
    node->hItem = item;
    if (node->expanded)
        return;
    node->expanded = true;
    if (node->type == NodeType::Root) {
        for (auto &c : node->children)
            insertScopeItem(c.get());
        if (!m_sessionLoaded) {
            m_sessionLoaded = true;
            // openFile перезаписывает список сессии, поэтому обход идёт по копии
            const std::vector<wstring> session = m_settings.session;
            for (const wstring &path : session)
                if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                    openFile(path, false);
        }
        return;
    }
    for (const auto &d : desiredChildren(*node))
        insertScopeItem(node->addChild(d.first, d.second));
}

void CComponentData::insertScopeItem(Node *node)
{
    if (!m_namespace || !node->parent || !node->parent->hItem)
        return;
    SCOPEDATAITEM sdi{};
    sdi.mask = SDI_STR | SDI_PARAM | SDI_IMAGE | SDI_OPENIMAGE | SDI_PARENT | SDI_CHILDREN;
    sdi.displayname = MMC_CALLBACK;
    sdi.lParam = reinterpret_cast<LPARAM>(node);
    sdi.nImage = node->image();
    sdi.nOpenImage = node->openImage();
    sdi.relativeID = node->parent->hItem;
    sdi.cChildren = node->mayHaveChildren() ? 1 : 0;
    if (SUCCEEDED(m_namespace->InsertItem(&sdi)))
        node->hItem = sdi.ID;
}

void CComponentData::forgetChildren(Node *node)
{
    for (auto &c : node->children)
        for (CComponent *comp : m_components)
            comp->nodeRemoved(c.get());
    node->children.clear();
    node->expanded = false;
}

void CComponentData::removeScopeNode(Node *node)
{
    for (CComponent *comp : m_components)
        comp->nodeRemoved(node);
    if (node->hItem && m_namespace)
        m_namespace->DeleteItem(node->hItem, TRUE);
    node->hItem = 0;
}

void CComponentData::updateScopeItem(Node *node)
{
    if (!node->hItem || !m_namespace)
        return;
    SCOPEDATAITEM sdi{};
    sdi.mask = SDI_STR | SDI_IMAGE | SDI_OPENIMAGE;
    sdi.ID = node->hItem;
    sdi.displayname = MMC_CALLBACK;
    sdi.nImage = node->image();
    sdi.nOpenImage = node->openImage();
    if (!node->expanded) {
        sdi.mask |= SDI_CHILDREN;
        sdi.cChildren = node->mayHaveChildren() ? 1 : 0;
    }
    m_namespace->SetItem(&sdi);
}

void CComponentData::syncNode(Node *node)
{
    updateScopeItem(node);
    if (!node->expanded)
        return;
    if (node->type != NodeType::Root) {
        const auto desired = desiredChildren(*node);
        auto wanted = [&](const Node &c) {
            return std::any_of(desired.begin(), desired.end(), [&](const auto &d) { return d.first == c.type && d.second == c.key; });
        };
        for (auto it = node->children.begin(); it != node->children.end();) {
            if (!wanted(**it)) {
                removeScopeNode(it->get());
                it = node->children.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto &d : desired)
            if (!node->findChild(d.first, d.second))
                insertScopeItem(node->addChild(d.first, d.second));
    }
    for (auto &c : node->children)
        syncNode(c.get());
}

Node *CComponentData::fileNode(InfFileDoc *f) const
{
    for (auto &c : m_root->children)
        if (c->file == f)
            return c.get();
    return nullptr;
}

Node *CComponentData::ensureNode(InfFileDoc *f, NodeType type, const wstring &key)
{
    Node *fn = fileNode(f);
    if (!fn || !fn->hItem || !m_console)
        return nullptr;
    if (!fn->expanded)
        m_console->Expand(fn->hItem, TRUE);
    if (type == NodeType::File)
        return fn;
    NodeType folder = type;
    if (type == NodeType::Section)
        folder = NodeType::Sections;
    else if (type == NodeType::Manufacturer)
        folder = NodeType::Devices;
    else if (type == NodeType::StringSection)
        folder = NodeType::Strings;
    Node *fo = fn->findChild(folder, L"");
    if (!fo || folder == type)
        return fo;
    if (!fo->expanded && fo->hItem)
        m_console->Expand(fo->hItem, TRUE);
    for (auto &c : fo->children)
        if (c->type == type && inf::iequals(c->key, key))
            return c.get();
    return fo;
}

void CComponentData::selectNode(Node *node)
{
    if (node && node->hItem && m_console)
        m_console->SelectScopeItem(node->hItem);
}

void CComponentData::nodeSelected(Node *node)
{
    if (node && node->file && !m_checkingDisk)
        maybeReload(node->file);
}

// ------------------------------------------------------------------ файлы

InfFileDoc *CComponentData::fileById(int id) const
{
    for (auto &f : m_files)
        if (f->id() == id)
            return f.get();
    return nullptr;
}

HWND CComponentData::mainWindow() const
{
    HWND h = nullptr;
    if (m_console)
        m_console->GetMainWindow(&h);
    return h;
}

int CComponentData::message(const wstring &text, UINT flags) const
{
    return MessageBoxW(mainWindow(), text.c_str(), kSnapinName, flags);
}

InfFileDoc *CComponentData::addDocument(std::unique_ptr<InfFileDoc> doc, bool select)
{
    InfFileDoc *f = doc.get();
    m_files.push_back(std::move(doc));
    Node *n = m_root->addChild(NodeType::File, std::to_wstring(f->id()));
    n->file = f;
    if (m_root->expanded)
        insertScopeItem(n);
    if (select)
        selectNode(n);
    saveSession();
    return f;
}

InfFileDoc *CComponentData::openFile(const wstring &path, bool select)
{
    wchar_t full[MAX_PATH * 2];
    const wstring abs = GetFullPathNameW(path.c_str(), DWORD(std::size(full)), full, nullptr) ? full : path;
    for (auto &f : m_files) {
        if (inf::iequals(f->path(), abs)) {
            if (select)
                selectNode(fileNode(f.get()));
            return f.get();
        }
    }
    auto doc = std::make_unique<InfFileDoc>(m_nextId++);
    wstring error;
    if (!doc->load(abs, error)) {
        message(std::format(L"Не удалось открыть {}:\n{}", abs, error), MB_OK | MB_ICONWARNING);
        return nullptr;
    }
    if (doc->decodedLossy())
        message(std::format(L"Файл {} содержит байты, недопустимые для кодировки {}. При сохранении они будут заменены.", abs,
                            inf::encodingName(doc->format().encoding)),
                MB_OK | MB_ICONWARNING);
    return addDocument(std::move(doc), select);
}

bool CComponentData::saveFile(InfFileDoc *f, bool forceAs)
{
    if (!f)
        return false;
    wstring path = f->path();
    if (forceAs || path.empty()) {
        CFileDialog dlg(FALSE, L"inf", f->fileName().c_str(), OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST, kFilter,
                        CWnd::FromHandle(mainWindow()));
        if (!path.empty()) {
            static wstring dir;
            dir = path.substr(0, path.find_last_of(L"\\/"));
            dlg.m_ofn.lpstrInitialDir = dir.c_str();
        }
        if (dlg.DoModal() != IDOK)
            return false;
        path = dlg.GetPathName().GetString();
    }
    wstring error;
    const bool ok = path == f->path() ? f->save(error) : f->saveAs(path, error);
    if (!ok) {
        message(std::format(L"Не удалось сохранить {}:\n{}", path, error), MB_OK | MB_ICONERROR);
        return false;
    }
    fileChanged(f);
    saveSession();
    return true;
}

bool CComponentData::closeFile(InfFileDoc *f, bool ask)
{
    if (ask && f->isModified()) {
        const int r = message(std::format(L"Сохранить изменения в файле «{}»?", f->fileName()), MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL)
            return false;
        if (r == IDYES && !saveFile(f, false))
            return false;
    }
    if (Node *n = fileNode(f)) {
        if (m_root->hItem)
            selectNode(m_root.get());
        removeScopeNode(n);
        auto &ch = m_root->children;
        ch.erase(std::remove_if(ch.begin(), ch.end(), [n](const std::unique_ptr<Node> &p) { return p.get() == n; }), ch.end());
    }
    m_files.erase(std::remove_if(m_files.begin(), m_files.end(), [f](const std::unique_ptr<InfFileDoc> &p) { return p.get() == f; }),
                  m_files.end());
    saveSession();
    return true;
}

bool CComponentData::maybeReload(InfFileDoc *f)
{
    if (!f->changedOnDisk())
        return false;
    m_checkingDisk = true;
    const wstring text = f->isModified()
                             ? std::format(L"Файл «{}» изменён другой программой. Перечитать его и потерять изменения, сделанные в оснастке?", f->fileName())
                             : std::format(L"Файл «{}» изменён другой программой. Перечитать?", f->fileName());
    bool reloaded = false;
    if (message(text, MB_YESNO | MB_ICONQUESTION) == IDYES) {
        wstring error;
        if (f->load(f->path(), error)) {
            reloaded = true;
            fileChanged(f);
        } else {
            message(error, MB_OK | MB_ICONWARNING);
        }
    } else {
        // больше не спрашивать до следующего изменения на диске
        f->acknowledgeDiskChange();
    }
    m_checkingDisk = false;
    return reloaded;
}

void CComponentData::saveSession()
{
    m_settings.session.clear();
    for (auto &f : m_files)
        if (!f->isUntitled())
            m_settings.session.push_back(f->path());
    m_settings.save();
}

void CComponentData::setSettings(const SnapinSettings &s)
{
    const auto session = m_settings.session;
    m_settings = s;
    m_settings.session = session;
    m_settings.save();
}

bool CComponentData::applyTransform(InfFileDoc *f, const Transform &t, bool showError)
{
    if (!f)
        return false;
    wstring error;
    if (!f->apply(t, error)) {
        if (showError && !error.empty())
            message(error, MB_OK | MB_ICONWARNING);
        return false;
    }
    fileChanged(f);
    return true;
}

void CComponentData::fileChanged(InfFileDoc *f)
{
    if (Node *n = fileNode(f))
        syncNode(n);
    for (CComponent *c : m_components)
        c->fileChanged(f);
}

void CComponentData::registerComponent(CComponent *c)
{
    m_components.push_back(c);
}

void CComponentData::unregisterComponent(CComponent *c)
{
    m_components.erase(std::remove(m_components.begin(), m_components.end(), c), m_components.end());
}

void CComponentData::applyPropertyChange(LPARAM param)
{
    auto *change = reinterpret_cast<PropertyChange *>(param);
    if (!change)
        return;
    if (change->action)
        change->action(*this);
    delete change;
}

// ------------------------------------------------------------------ удаление, переименование, обновление

bool CComponentData::deleteTarget(const Target &t)
{
    InfFileDoc *f = t.file();
    if (!f)
        return false;
    if (t.row) {
        if (!t.row->canDelete())
            return false;
        const auto row = t.row;
        const wstring what = row->type == RowType::Device ? L"устройство «" + row->columns[0] + L"»"
                             : row->type == RowType::String ? L"строку " + row->key
                                                            : L"запись «" + inf::trim(row->lineText) + L"»";
        if (message(L"Удалить " + what + L"?", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return false;
        return applyTransform(f, [row](const Analysis &a, wstring &text, wstring &error) {
            const int idx = locateLine(a, row->logicalLine, row->lineText);
            if (idx < 0) {
                error = L"Запись уже изменена или удалена.";
                return false;
            }
            text = inf::applyEdit(text, inf::removeLineEdit(a.doc, idx));
            return true;
        });
    }
    switch (t.node->type) {
    case NodeType::File:
        return closeFile(f, true);
    case NodeType::Section:
    case NodeType::StringSection: {
        const wstring name = t.node->key;
        const auto a = f->analysis();
        const int refs = a->model.incomingCount(name);
        wstring text = std::format(L"Удалить секцию [{}] со всем содержимым?", name);
        if (refs > 0)
            text += std::format(L"\n\nНа секцию есть ссылки ({}): после удаления они станут ошибочными.", refs);
        if (message(text, MB_YESNO | MB_ICONQUESTION) != IDYES)
            return false;
        return applyTransform(f, [name](const Analysis &a, wstring &txt, wstring &) {
            txt = inf::applyEdits(txt, inf::removeSectionEdits(a.doc, name));
            return true;
        });
    }
    default:
        return false;
    }
}

bool CComponentData::canRename(const Target &t) const
{
    return !t.row && t.node && (t.node->type == NodeType::Section || t.node->type == NodeType::StringSection);
}

bool CComponentData::renameTarget(const Target &t, const wstring &newNameRaw)
{
    if (!canRename(t))
        return false;
    // MMC передаёт текст, введённый на месте; скобки допускаются для удобства
    wstring newName = inf::trim(newNameRaw);
    if (newName.size() > 2 && newName.front() == L'[' && newName.back() == L']')
        newName = inf::trim(newName.substr(1, newName.size() - 2));
    const size_t dash = newName.find(L" — ");
    if (dash != wstring::npos)
        newName = inf::trim(newName.substr(0, dash));
    const wstring old = t.node->key;
    if (newName.empty() || newName.find_first_of(L"[];\"") != wstring::npos) {
        message(L"Недопустимое имя секции.", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (newName == old)
        return false;
    InfFileDoc *f = t.file();
    if (f->analysis()->doc.hasSection(newName) && !inf::iequals(newName, old)) {
        message(std::format(L"Секция [{}] уже существует.", newName), MB_OK | MB_ICONWARNING);
        return false;
    }
    // узел остаётся тем же, меняется только ключ: синхронизация дерева его не пересоздаёт
    Node *node = t.node;
    node->key = newName;
    const bool ok = applyTransform(f, [old, newName](const Analysis &a, wstring &text, wstring &) {
        text = inf::applySpans(text, inf::renameSectionEdits(a.doc, a.model, old, newName));
        return true;
    });
    if (!ok) {
        node->key = old;
        fileChanged(f);
    }
    return ok;
}

void CComponentData::refreshTarget(const Target &t)
{
    const bool system = (t.node && isSystemNode(t.node->type))
                        || (t.row && (t.row->type == RowType::SysDevice || t.row->type == RowType::Package));
    if (system) {
        reloadSystemSnapshot();
        systemChanged();
        return;
    }
    InfFileDoc *f = t.file();
    if (!f) {
        for (auto &file : m_files)
            if (!maybeReload(file.get()))
                fileChanged(file.get());
        return;
    }
    if (!maybeReload(f))
        fileChanged(f);
}

// ------------------------------------------------------------------ контекстное меню

STDMETHODIMP CComponentData::AddMenuItems(LPDATAOBJECT obj, LPCONTEXTMENUCALLBACK callback, long *allowed)
{
    trace(L"CD::AddMenuItems");
    MANAGE_STATE();
    return addMenuItems(targetOf(obj), callback, allowed);
}

STDMETHODIMP CComponentData::Command(long id, LPDATAOBJECT obj)
{
    trace(L"CD::Command %ld", id);
    MANAGE_STATE();
    return command(id, targetOf(obj));
}

HRESULT CComponentData::addMenuItems(const Target &t, LPCONTEXTMENUCALLBACK callback, long *allowed)
{
    if (!callback || !t.node)
        return S_OK;
    const bool top = !allowed || (*allowed & CCM_INSERTIONALLOWED_TOP);
    const bool task = !allowed || (*allowed & CCM_INSERTIONALLOWED_TASK);
    const bool topMenu = top;

    auto add = [&](long id, const wchar_t *name, const wchar_t *status, bool enabled = true, long where = CCM_INSERTIONPOINTID_PRIMARY_TOP) {
        if (where == CCM_INSERTIONPOINTID_PRIMARY_TOP && !top)
            return;
        if (where == CCM_INSERTIONPOINTID_PRIMARY_TASK && !task)
            return;
        CONTEXTMENUITEM item{};
        item.strName = const_cast<LPWSTR>(name);
        item.strStatusBarText = const_cast<LPWSTR>(status);
        item.lCommandID = id;
        item.lInsertionPointID = where;
        item.fFlags = enabled ? 0 : MF_GRAYED;
        callback->AddItem(&item);
    };
    auto separator = [&] {
        if (!topMenu)
            return;
        CONTEXTMENUITEM item{};
        item.lInsertionPointID = CCM_INSERTIONPOINTID_PRIMARY_TOP;
        item.fFlags = MF_SEPARATOR;
        item.fSpecialFlags = CCM_SPECIAL_SEPARATOR;
        callback->AddItem(&item);
    };

    InfFileDoc *f = t.file();
    if (t.row && t.row->type == RowType::SysDevice) {
        const inf::DeviceInfo &d = t.row->device;
        const bool hasInf = !d.infName.empty();
        add(CmdDevOpenInf, L"Открыть INF драйвера", L"Открыть INF-файл установленного драйвера в редакторе", hasInf);
        add(CmdDevExport, L"Экспортировать драйвер в CAB...", L"Сохранить пакет драйвера в CAB-архив", hasInf);
        add(CmdDevShowStore, L"Показать пакет в Проводнике", L"Открыть директорию пакета в хранилище драйверов", hasInf);
        separator();
        add(CmdDevCreateInf, L"Создать INF для этого устройства...", L"Мастер создания INF с ID оборудования этого устройства");
        add(CmdDevCopyIds, L"Копировать ID оборудования", L"Скопировать идентификаторы устройства в буфер обмена");
        return S_OK;
    }
    if (t.row && t.row->type == RowType::Package) {
        add(CmdDevOpenInf, L"Открыть INF", L"Открыть INF пакета в редакторе");
        add(CmdDevExport, L"Экспортировать в CAB...", L"Сохранить пакет драйвера в CAB-архив");
        add(CmdDevShowStore, L"Показать пакет в Проводнике", L"Открыть директорию пакета в хранилище драйверов");
        return S_OK;
    }
    if (t.row) {
        if (t.row->type == RowType::Problem && !t.row->section.empty())
            add(CmdGotoSection, L"Перейти к секции", L"Показать секцию, к которой относится сообщение");
        if (!t.row->section.empty())
            add(CmdEditSection, L"Изменить текст секции...", L"Открыть текст секции в редакторе");
        add(CmdEditText, L"Изменить текст файла...", L"Открыть весь текст INF в редакторе");
        return S_OK;
    }

    switch (t.node->type) {
    case NodeType::Root:
        add(CmdOpen, L"Открыть INF-файл...", L"Открыть один или несколько INF-файлов");
        add(CmdWizard, L"Создать INF-файл с помощью мастера...", L"Мастер создания INF (по мотивам GenInf из Windows DDK)");
        add(CmdNewEmpty, L"Создать пустой INF-файл", L"Новый файл с минимальной секцией [Version]");
        return S_OK;
    case NodeType::File:
        add(CmdSave, L"Сохранить", L"Сохранить файл на диск");
        add(CmdSaveAs, L"Сохранить как...", L"Сохранить файл под другим именем");
        add(CmdReload, L"Перечитать с диска", L"Отменить изменения и загрузить файл заново", !f->isUntitled());
        separator();
        add(CmdUndo, L"Отменить изменение", L"Вернуть текст до последнего изменения", f->canUndo());
        add(CmdRedo, L"Повторить изменение", L"Повторить отменённое изменение", f->canRedo());
        separator();
        add(CmdEditText, L"Изменить текст...", L"Открыть весь текст INF в редакторе");
        add(CmdNotepad, L"Открыть в Блокноте", L"Открыть файл во внешнем редакторе", !f->isUntitled());
        add(CmdShowFolder, L"Показать в Проводнике", L"Открыть директорию с файлом", !f->isUntitled());
        separator();
        add(CmdInfVerif, L"Проверить в InfVerif", L"Запустить InfVerif из Windows Driver Kit");
        add(CmdVerifOutput, L"Вывод InfVerif...", L"Показать полный вывод последнего запуска InfVerif", f->verifRan());
        add(CmdDriverVer, L"Обновить DriverVer", L"Сегодняшняя дата и следующий номер версии");
        add(CmdAddDevice, L"Добавить устройство...", L"Добавить модель устройства в секции моделей");
        separator();
        add(CmdClose, L"Закрыть файл", L"Закрыть файл в оснастке");
        return S_OK;
    case NodeType::Version:
        add(CmdEditSection, L"Изменить текст секции...", L"Открыть текст [Version] в редакторе");
        add(CmdAddEntry, L"Добавить запись...", L"Добавить директиву в [Version]");
        add(CmdDriverVer, L"Обновить DriverVer", L"Сегодняшняя дата и следующий номер версии");
        break;
    case NodeType::Devices:
        add(CmdAddDevice, L"Добавить устройство...", L"Добавить модель устройства в секции моделей");
        break;
    case NodeType::Manufacturer:
        add(CmdAddDevice, L"Добавить устройство...", L"Добавить модель устройства в секции моделей");
        add(CmdEditSection, L"Изменить текст [Manufacturer]...", L"Открыть текст [Manufacturer] в редакторе");
        break;
    case NodeType::Sections:
        add(CmdAddSection, L"Добавить секцию...", L"Добавить новую секцию в конец файла");
        break;
    case NodeType::Section:
        add(CmdAddEntry, L"Добавить запись...", L"Добавить запись в конец секции");
        add(CmdEditSection, L"Изменить текст секции...", L"Открыть текст секции в редакторе");
        break;
    case NodeType::Strings:
    case NodeType::StringSection:
        add(CmdAddString, L"Добавить строку...", L"Добавить строковую константу");
        add(CmdAddLanguage, L"Добавить язык...", L"Создать локализованную секцию [Strings.LLLL]");
        add(CmdAddUndefined, L"Добавить неопределённые строки", L"Добавить строки %ключ%, которые используются, но не определены");
        add(CmdRemoveUnused, L"Удалить неиспользуемые строки", L"Удалить из [Strings] строки, на которые нет ссылок");
        if (t.node->type == NodeType::StringSection)
            add(CmdEditSection, L"Изменить текст секции...", L"Открыть текст секции в редакторе");
        break;
    case NodeType::Files:
        add(CmdAddFile, L"Добавить файл...", L"Добавить файл в пакет драйвера");
        break;
    case NodeType::Hardware:
    case NodeType::DeviceClass: {
        add(CmdRefreshSystem, L"Обновить список устройств", L"Заново получить список устройств и драйверов");
        if (top) {
            CONTEXTMENUITEM item{};
            item.strName = const_cast<LPWSTR>(L"Показать скрытые устройства");
            item.strStatusBarText = const_cast<LPWSTR>(L"Показывать отключённые и скрытые устройства, как в Диспетчере устройств");
            item.lCommandID = CmdShowHidden;
            item.lInsertionPointID = CCM_INSERTIONPOINTID_PRIMARY_TOP;
            item.fFlags = showHiddenDevices() ? MF_CHECKED : 0;
            callback->AddItem(&item);
        }
        return S_OK;
    }
    case NodeType::DriverPackages:
        add(CmdExportAll, L"Экспортировать все сторонние драйверы...", L"Сохранить каждый сторонний пакет в отдельный CAB");
        add(CmdRefreshSystem, L"Обновить список пакетов", L"Заново получить список пакетов драйверов");
        return S_OK;
    case NodeType::Problems:
        add(CmdInfVerif, L"Проверить в InfVerif", L"Запустить InfVerif из Windows Driver Kit");
        add(CmdVerifOutput, L"Вывод InfVerif...", L"Показать полный вывод последнего запуска InfVerif", f->verifRan());
        add(CmdAddUndefined, L"Добавить неопределённые строки", L"Добавить строки %ключ%, которые используются, но не определены");
        break;
    }
    if (f) {
        add(CmdSave, L"Сохранить файл", L"Сохранить INF-файл на диск", true, CCM_INSERTIONPOINTID_PRIMARY_TASK);
        add(CmdEditText, L"Изменить текст файла...", L"Открыть весь текст INF в редакторе", true, CCM_INSERTIONPOINTID_PRIMARY_TASK);
        add(CmdUndo, L"Отменить изменение", L"Вернуть текст до последнего изменения", f->canUndo(), CCM_INSERTIONPOINTID_PRIMARY_TASK);
    }
    return S_OK;
}

HRESULT CComponentData::command(long id, const Target &t)
{
    InfFileDoc *f = t.file();
    switch (id) {
    case CmdOpen: cmdOpen(); break;
    case CmdWizard: cmdWizard(); break;
    case CmdNewEmpty: cmdNewEmpty(); break;
    case CmdSave: saveFile(f, false); break;
    case CmdSaveAs: saveFile(f, true); break;
    case CmdReload:
        if (f && !f->isUntitled()) {
            if (f->isModified() && message(L"Отменить изменения и перечитать файл с диска?", MB_YESNO | MB_ICONQUESTION) != IDYES)
                break;
            wstring error;
            if (!f->load(f->path(), error))
                message(error, MB_OK | MB_ICONWARNING);
            fileChanged(f);
        }
        break;
    case CmdUndo:
        if (f && f->undo())
            fileChanged(f);
        break;
    case CmdRedo:
        if (f && f->redo())
            fileChanged(f);
        break;
    case CmdEditText: {
        int line = -1;
        if (t.row) {
            const auto a = f->analysis();
            line = t.row->type == RowType::Problem ? t.row->diagnostic.line
                                                   : (t.row->logicalLine >= 0 ? a->doc.lines()[size_t(t.row->logicalLine)].firstLine : -1);
        }
        cmdEditText(f, L"", line);
        break;
    }
    case CmdEditSection: {
        int line = -1;
        if (t.row && f) {
            const auto a = f->analysis();
            const int abs = t.row->type == RowType::Problem ? t.row->diagnostic.line
                                                            : (t.row->logicalLine >= 0 ? a->doc.lines()[size_t(t.row->logicalLine)].firstLine : -1);
            if (const inf::InfSection *s = a->doc.section(t.row->section))
                line = abs >= 0 ? abs - s->headerLine() : -1;
        }
        cmdEditText(f, sectionForTarget(t), line);
        break;
    }
    case CmdNotepad: cmdNotepad(f); break;
    case CmdShowFolder:
        if (f && !f->isUntitled())
            cmdShowInExplorer(f->path());
        break;
    case CmdInfVerif: cmdInfVerif(f); break;
    case CmdVerifOutput: cmdVerifOutput(f); break;
    case CmdDriverVer: cmdDriverVer(f); break;
    case CmdAddDevice: cmdAddDevice(f); break;
    case CmdClose:
        if (f)
            closeFile(f, true);
        break;
    case CmdAddEntry: cmdAddEntry(f, sectionForTarget(t)); break;
    case CmdAddSection: cmdAddSection(f); break;
    case CmdAddString: cmdAddString(f, sectionForTarget(t)); break;
    case CmdAddLanguage: cmdAddLanguage(f); break;
    case CmdAddUndefined: cmdAddUndefined(f); break;
    case CmdRemoveUnused: cmdRemoveUnused(f); break;
    case CmdAddFile: cmdAddFile(f); break;
    case CmdDevOpenInf:
        if (t.row && t.row->type == RowType::SysDevice)
            cmdOpenSystemInf(t.row->device.infName);
        else if (t.row && t.row->type == RowType::Package)
            cmdOpenSystemInf(t.row->package.publishedName);
        break;
    case CmdDevExport:
        if (t.row && t.row->type == RowType::SysDevice)
            cmdExportPackage(t.row->device.infName);
        else if (t.row && t.row->type == RowType::Package)
            cmdExportPackage(t.row->package.publishedName);
        break;
    case CmdDevShowStore:
        if (t.row) {
            inf::DriverPackage p;
            const wstring name = t.row->type == RowType::SysDevice ? t.row->device.infName : t.row->package.publishedName;
            if (inf::driverPackage(name, p))
                cmdShowInExplorer(p.storeInf.empty() ? p.publishedPath : p.storeInf);
        }
        break;
    case CmdDevCreateInf:
        if (t.row && t.row->type == RowType::SysDevice)
            cmdCreateInfForDevice(t.row->device);
        break;
    case CmdDevCopyIds:
        if (t.row && t.row->type == RowType::SysDevice)
            cmdCopyIds(t.row->device);
        break;
    case CmdExportAll: cmdExportAll(); break;
    case CmdRefreshSystem:
        reloadSystemSnapshot();
        systemChanged();
        break;
    case CmdShowHidden:
        m_settings.showHiddenDevices = !showHiddenDevices();
        m_settings.save();
        setShowHiddenDevices(m_settings.showHiddenDevices);
        systemChanged();
        break;
    case CmdGotoSection:
        if (f && t.row)
            selectNode(ensureNode(f, NodeType::Section, t.row->section));
        break;
    default:
        return S_FALSE;
    }
    return S_OK;
}

// ------------------------------------------------------------------ команды

void CComponentData::cmdOpen()
{
    CFileDialog dlg(TRUE, L"inf", nullptr, OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT, kFilter, CWnd::FromHandle(mainWindow()));
    if (dlg.DoModal() != IDOK)
        return;
    POSITION pos = dlg.GetStartPosition();
    InfFileDoc *last = nullptr;
    while (pos) {
        const CString path = dlg.GetNextPathName(pos);
        if (InfFileDoc *f = openFile(path.GetString(), false))
            last = f;
    }
    if (last)
        selectNode(fileNode(last));
}

void CComponentData::cmdWizard()
{
    wstring text, name;
    if (!runNewInfWizard(CWnd::FromHandle(mainWindow()), text, name, nullptr))
        return;
    auto doc = std::make_unique<InfFileDoc>(m_nextId++);
    doc->setNew(text, name);
    InfFileDoc *f = addDocument(std::move(doc), true);
    if (message(L"Файл создан. Сохранить его на диск сейчас?", MB_YESNO | MB_ICONQUESTION) == IDYES)
        saveFile(f, true);
}

void CComponentData::cmdNewEmpty()
{
    inf::GeneratorParams p;
    const wstring text = L"[Version]\nSignature   = \"$WINDOWS NT$\"\nClass       = System\n"
                         L"ClassGuid   = {4d36e97d-e325-11ce-bfc1-08002be10318}\nProvider    = %ProviderName%\n"
                         L"DriverVer   = " + inf::Date::today().toInf() + L",1.0.0.0\nPnpLockdown = 1\n\n[Strings]\n"
                         L"ProviderName = \"Provider\"\n";
    auto doc = std::make_unique<InfFileDoc>(m_nextId++);
    doc->setNew(text, L"Без имени.inf");
    addDocument(std::move(doc), true);
}

void CComponentData::cmdEditText(InfFileDoc *f, const wstring &section, int line)
{
    if (!f)
        return;
    const auto a = f->analysis();
    if (section.empty()) {
        wstring text = f->text();
        if (!runTextEditor(CWnd::FromHandle(mainWindow()), f->fileName(), text, line))
            return;
        applyTransform(f, [text](const Analysis &, wstring &out, wstring &) {
            out = text;
            return true;
        });
        return;
    }
    if (!a->doc.hasSection(section)) {
        message(std::format(L"Секции [{}] нет в файле.", section), MB_OK | MB_ICONWARNING);
        return;
    }
    const wstring realName = a->doc.section(section)->name;
    const StringList original = inf::sectionText(a->doc, realName);
    wstring text = inf::join(original, L"\n");
    wstring title = f->fileName() + L" — [" + realName + L"]";
    if (a->doc.section(realName)->chunks.size() > 1)
        title += L" (первый фрагмент)";
    if (!runTextEditor(CWnd::FromHandle(mainWindow()), title, text, line))
        return;
    applyTransform(f, [realName, original, text](const Analysis &an, wstring &out, wstring &error) {
        if (inf::sectionText(an.doc, realName) != original) {
            error = L"Секция изменилась, пока открыт редактор. Изменения не применены.";
            return false;
        }
        StringList lines = inf::split(text, L'\n');
        while (!lines.empty() && inf::trim(lines.back()).empty())
            lines.pop_back();
        // пустой текст удаляет первый фрагмент секции
        out = inf::applyEdit(out, inf::replaceSectionEdit(an.doc, realName, lines));
        return true;
    });
}

void CComponentData::cmdNotepad(InfFileDoc *f)
{
    if (!f || f->isUntitled())
        return;
    if (f->isModified()) {
        const int r = message(L"В оснастке есть несохранённые изменения. Сохранить файл перед открытием в Блокноте?",
                              MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL || (r == IDYES && !saveFile(f, false)))
            return;
    }
    const wstring args = L"\"" + f->path() + L"\"";
    ShellExecuteW(mainWindow(), L"open", L"notepad.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void CComponentData::cmdInfVerif(InfFileDoc *f)
{
    if (!f)
        return;
    const wstring tool = m_settings.resolvedInfVerif();
    if (tool.empty() || GetFileAttributesW(tool.c_str()) == INVALID_FILE_ATTRIBUTES) {
        message(L"InfVerif.exe не найден. Он входит в Windows Driver Kit (WDK).\n"
                L"Путь к нему задаётся в свойствах узла «Редактор INF».",
                MB_OK | MB_ICONWARNING);
        return;
    }
    // проверяется текущий текст, включая несохранённые изменения, через временную копию
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const wstring dir = wstring(tmp) + L"inf_editor_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(f->id());
    CreateDirectoryW(dir.c_str(), nullptr);
    wstring name = f->fileName();
    if (!inf::iendsWith(name, L".inf"))
        name += L".inf";
    const wstring path = dir + L"\\" + name;
    wstring error;
    if (!inf::writeFile(path, f->text(), f->format(), &error)) {
        message(L"Не удалось создать временный файл: " + error, MB_OK | MB_ICONERROR);
        return;
    }
    CWaitCursor wait;
    const inf::InfVerifResult r = inf::runInfVerif(tool, m_settings.infVerifMode, path);
    DeleteFileW(path.c_str());
    RemoveDirectoryW(dir.c_str());
    f->setVerifResult(r);
    fileChanged(f);
    if (!r.started) {
        message(L"Не удалось запустить InfVerif: " + r.error, MB_OK | MB_ICONERROR);
        return;
    }
    selectNode(ensureNode(f, NodeType::Problems, L""));
    int errors = 0;
    for (const inf::Diagnostic &d : r.diagnostics)
        if (d.severity == inf::Severity::Error)
            ++errors;
    if (m_console)
        m_console->SetStatusText(const_cast<LPOLESTR>((r.valid ? std::format(L"InfVerif: INF корректен, сообщений: {}", r.diagnostics.size())
                                                               : std::format(L"InfVerif: INF НЕ КОРРЕКТЕН, ошибок: {}", errors)).c_str()));
}

void CComponentData::cmdVerifOutput(InfFileDoc *f)
{
    if (f && f->verifRan())
        runOutputDialog(CWnd::FromHandle(mainWindow()), L"Вывод InfVerif — " + f->fileName(), f->verifOutput());
}

void CComponentData::cmdDriverVer(InfFileDoc *f)
{
    if (!f)
        return;
    applyTransform(f, [](const Analysis &a, wstring &text, wstring &) {
        const wstring version = bumpVersion(a.model.resolve(a.doc.value(L"Version", L"DriverVer", 1)));
        text = inf::applyEdit(text, inf::setValueEdit(a.doc, L"Version", L"DriverVer", inf::Date::today().toInf() + L"," + version));
        return true;
    });
}

void CComponentData::cmdAddDevice(InfFileDoc *f)
{
    if (!f)
        return;
    NewDevice d;
    if (!runAddDeviceDialog(CWnd::FromHandle(mainWindow()), *f->analysis(), d))
        return;
    applyTransform(f, [d](const Analysis &a, wstring &text, wstring &) {
        const wstring descRef = d.stringKey.empty() ? inf::quote(d.description) : L"%" + d.stringKey + L"%";
        StringList ids = {d.hardwareId};
        for (const wstring &c : inf::split(d.compatible, L',', true))
            if (!inf::trim(c).empty())
                ids.push_back(inf::trim(c));
        const wstring line = descRef + L" = " + d.install + L", " + inf::join(ids, L", ");
        for (const wstring &sec : d.modelSections) {
            const inf::InfDocument doc = inf::InfDocument::parse(text);
            text = inf::applyEdit(text, inf::appendToSectionEdit(doc, sec, {line}));
        }
        if (!d.stringKey.empty()) {
            const inf::InfDocument doc = inf::InfDocument::parse(text);
            const wstring strings = a.model.baseStringSection().empty() ? L"Strings" : a.model.baseStringSection();
            text = inf::applyEdit(text, inf::setValueEdit(doc, strings, d.stringKey, inf::quote(d.description)));
        }
        return true;
    });
}

void CComponentData::cmdAddEntry(InfFileDoc *f, const wstring &section)
{
    if (!f || section.empty())
        return;
    wstring key, value;
    if (!runAddEntryDialog(CWnd::FromHandle(mainWindow()), section, key, value))
        return;
    applyTransform(f, [section, key, value](const Analysis &a, wstring &text, wstring &) {
        const wstring line = key.empty() ? value : key + L" = " + value;
        text = inf::applyEdit(text, inf::appendToSectionEdit(a.doc, section, {line}));
        return true;
    });
}

void CComponentData::cmdAddSection(InfFileDoc *f)
{
    if (!f)
        return;
    const auto a = f->analysis();
    StringList missing;
    for (const inf::SectionRef &r : a->model.references())
        if (!r.exists && !inf::icontains(missing, r.target))
            missing.push_back(r.target);
    for (const wstring &s : inf::standardSectionNames())
        if (!a->doc.hasSection(s) && !inf::icontains(missing, s))
            missing.push_back(s);
    wstring name, content;
    if (!runAddSectionDialog(CWnd::FromHandle(mainWindow()), missing, name, content))
        return;
    if (a->doc.hasSection(name)) {
        message(std::format(L"Секция [{}] уже существует.", name), MB_OK | MB_ICONWARNING);
        return;
    }
    if (applyTransform(f, [name, content](const Analysis &an, wstring &text, wstring &) {
            StringList lines = {L"[" + name + L"]"};
            for (const wstring &l : inf::split(inf::replaceAll(content, L"\r\n", L"\n"), L'\n'))
                lines.push_back(l);
            while (lines.size() > 1 && inf::trim(lines.back()).empty())
                lines.pop_back();
            text = inf::applyEdit(text, inf::appendSectionEdit(an.doc, lines));
            return true;
        }))
        selectNode(ensureNode(f, NodeType::Section, name));
}

void CComponentData::cmdAddString(InfFileDoc *f, const wstring &section)
{
    if (!f)
        return;
    const auto a = f->analysis();
    StringList sections = a->model.stringSections();
    if (sections.empty())
        sections.push_back(L"Strings");
    wstring sec = inf::icontains(sections, section) ? section : sections.front();
    wstring key, value;
    if (!runAddStringDialog(CWnd::FromHandle(mainWindow()), sections, sec, key, value))
        return;
    applyTransform(f, [sec, key, value](const Analysis &an, wstring &text, wstring &) {
        text = inf::applyEdit(text, inf::setValueEdit(an.doc, sec, key, inf::quote(value)));
        return true;
    });
}

void CComponentData::cmdAddLanguage(InfFileDoc *f)
{
    if (!f)
        return;
    wstring code = L"0419";
    if (!runInputDialog(CWnd::FromHandle(mainWindow()), L"Добавить язык",
                        L"Код языка (LCID, четыре шестнадцатеричные цифры), например 0419 — русский, 0409 — английский:", code))
        return;
    code = inf::trim(code);
    bool ok = code.size() == 4;
    for (wchar_t c : code)
        ok = ok && iswxdigit(c);
    if (!ok) {
        message(L"Код языка должен состоять из четырёх шестнадцатеричных цифр.", MB_OK | MB_ICONWARNING);
        return;
    }
    const wstring sec = L"Strings." + code;
    if (f->analysis()->doc.hasSection(sec)) {
        selectNode(ensureNode(f, NodeType::StringSection, sec));
        return;
    }
    if (applyTransform(f, [sec](const Analysis &a, wstring &text, wstring &) {
            // строки базовой секции — отправная точка для перевода
            StringList lines = {L"[" + sec + L"]"};
            const wstring base = a.model.baseStringSection();
            if (!base.empty())
                for (const inf::StringDef &d : a.model.strings(base))
                    lines.push_back(d.key + L" = " + inf::quote(d.value));
            text = inf::applyEdit(text, inf::appendSectionEdit(a.doc, lines));
            return true;
        }))
        selectNode(ensureNode(f, NodeType::StringSection, sec));
}

void CComponentData::cmdAddUndefined(InfFileDoc *f)
{
    if (!f)
        return;
    const auto a = f->analysis();
    StringList keys;
    for (const inf::Diagnostic &d : a->diagnostics) {
        if (d.code != L"T001")
            continue;
        const wstring token = a->doc.physicalLine(d.line).substr(size_t(d.column), size_t(d.length));
        if (token.size() >= 3 && !inf::icontains(keys, token.substr(1, token.size() - 2)))
            keys.push_back(token.substr(1, token.size() - 2));
    }
    if (keys.empty()) {
        message(L"Все используемые строки определены.", MB_OK | MB_ICONINFORMATION);
        return;
    }
    applyTransform(f, [keys](const Analysis &an, wstring &text, wstring &) {
        const wstring sec = an.model.baseStringSection().empty() ? L"Strings" : an.model.baseStringSection();
        StringList lines;
        for (const wstring &k : keys)
            lines.push_back(k + L" = \"" + k + L"\"");
        text = inf::applyEdit(text, inf::appendToSectionEdit(an.doc, sec, lines));
        return true;
    });
    message(std::format(L"Добавлено строк: {}. Значения равны ключам — их нужно заменить настоящим текстом.", keys.size()),
            MB_OK | MB_ICONINFORMATION);
}

void CComponentData::cmdRemoveUnused(InfFileDoc *f)
{
    if (!f)
        return;
    const auto a = f->analysis();
    const wstring base = a->model.baseStringSection();
    StringList unused;
    if (!base.empty())
        for (const inf::StringDef &d : a->model.strings(base))
            if (a->model.stringUses(d.key) == 0)
                unused.push_back(d.key);
    if (unused.empty()) {
        message(L"Неиспользуемых строк нет.", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (message(std::format(L"Удалить неиспользуемые строки ({}):\n{}?", unused.size(), inf::join(unused, L", ")),
                MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    applyTransform(f, [base](const Analysis &an, wstring &text, wstring &) {
        std::vector<inf::TextEdit> edits;
        for (const inf::StringDef &d : an.model.strings(base))
            if (an.model.stringUses(d.key) == 0)
                edits.push_back(inf::removeLineEdit(an.doc, d.logicalLine));
        text = inf::applyEdits(text, edits);
        return true;
    });
}

void CComponentData::cmdAddFile(InfFileDoc *f)
{
    if (!f)
        return;
    NewFile nf;
    if (!runAddFileDialog(CWnd::FromHandle(mainWindow()), *f->analysis(), nf))
        return;
    applyTransform(f, [nf](const Analysis &a0, wstring &text, wstring &) {
        const bool newList = !a0.doc.hasSection(nf.list);
        // 1. Секция копирования
        {
            const inf::InfDocument doc = inf::InfDocument::parse(text);
            text = inf::applyEdit(text, inf::appendToSectionEdit(doc, nf.list, {nf.name}));
        }
        // 2. Подключение новой секции копирования к секциям установки
        if (newList) {
            for (const wstring &inst : a0.model.installSections()) {
                const inf::InfDocument doc = inf::InfDocument::parse(text);
                if (const inf::InfLine *cf = doc.entry(inst, L"CopyFiles")) {
                    StringList vals;
                    for (const inf::InfValue &v : cf->values)
                        vals.push_back(v.raw);
                    vals.push_back(nf.list);
                    text = inf::applyEdit(text, inf::setValueEdit(doc, inst, L"CopyFiles", inf::join(vals, L",")));
                } else {
                    text = inf::applyEdit(text, inf::appendToSectionEdit(doc, inst, {L"CopyFiles = " + nf.list}));
                }
            }
        }
        // 3. Директория назначения
        {
            const auto a = Analysis::run(text);
            const auto &dd = a->model.destinationDirs();
            if (!dd.count(L"defaultdestdir") && !dd.count(inf::lower(nf.list)))
                text = inf::applyEdit(text, inf::appendToSectionEdit(a->doc, L"DestinationDirs", {nf.list + L" = 13"}));
        }
        // 4. Исходный диск и файл
        {
            const auto a = Analysis::run(text);
            if (!a->model.sourceDisks().count(inf::lower(nf.disk)))
                text = inf::applyEdit(text, inf::appendToSectionEdit(a->doc, L"SourceDisksNames", {nf.disk + L" = \"Installation Disk\",,,\"\""}));
        }
        {
            const auto a = Analysis::run(text);
            if (!a->model.sourceFiles().count(inf::lower(nf.name))) {
                const wstring entry = nf.name + L" = " + nf.disk + L"," + nf.subdir + L",";
                text = inf::applyEdit(text, inf::appendToSectionEdit(a->doc, L"SourceDisksFiles", {entry}));
            }
        }
        return true;
    });
}

// ------------------------------------------------------------------ устройства и пакеты драйверов

void CComponentData::systemChanged()
{
    for (auto &c : m_root->children)
        if (isSystemNode(c->type))
            syncNode(c.get());
    for (CComponent *c : m_components)
        c->systemChanged();
}

void CComponentData::cmdOpenSystemInf(const wstring &publishedName)
{
    if (publishedName.empty())
        return;
    const wstring path = publishedName.find_first_of(L"\\/") == wstring::npos ? inf::windowsInfDir() + publishedName : publishedName;
    openFile(path, true);
}

void CComponentData::cmdShowInExplorer(const wstring &path)
{
    const wstring args = L"/select,\"" + path + L"\"";
    ShellExecuteW(mainWindow(), L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void CComponentData::cmdExportPackage(const wstring &publishedName)
{
    inf::DriverPackage p;
    wstring error;
    if (!inf::driverPackage(publishedName, p, &error) || p.storeInf.empty()) {
        message(L"Пакет драйвера " + publishedName + L" не найден в хранилище драйверов. " + error, MB_OK | MB_ICONWARNING);
        return;
    }
    ExportOptions opt;
    opt.title = L"Экспорт драйвера " + p.publishedName + L" (" + p.originalName + L")";
    opt.info = std::format(L"Поставщик: {}\r\nКласс: {}\r\nВерсия: {}\r\nХранилище: {}", p.provider, p.className, p.driverVer, p.storeDir);
    wchar_t docs[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs);
    opt.path = wstring(docs) + L"\\" + inf::suggestedCabName(p);
    if (!runExportDialog(CWnd::FromHandle(mainWindow()), opt))
        return;
    CWaitCursor wait;
    const inf::ExportPlan plan = inf::planExport(p.storeInf, opt.flatten);
    if (!plan.errors.empty()) {
        message(L"Экспорт невозможен:\n\n" + inf::join(plan.errors, L"\n\n"), MB_OK | MB_ICONERROR);
        return;
    }
    if (!inf::exportToCab(plan, opt.path, &error)) {
        message(L"Не удалось создать архив:\n" + error, MB_OK | MB_ICONERROR);
        return;
    }
    wstring text = std::format(L"Создан архив {}\nФайлов: {} (INF и {} файлов пакета).", opt.path, plan.files.size() + 1, plan.files.size());
    if (!plan.warnings.empty())
        text += L"\n\n" + inf::join(plan.warnings, L"\n\n");
    message(text, MB_OK | (plan.warnings.empty() ? MB_ICONINFORMATION : MB_ICONWARNING));
}

void CComponentData::cmdExportAll()
{
    const auto &packages = systemSnapshot().packages;
    if (packages.empty()) {
        message(L"Сторонних пакетов драйверов нет.", MB_OK | MB_ICONINFORMATION);
        return;
    }
    ExportOptions opt;
    opt.directory = true;
    opt.title = std::format(L"Экспорт всех сторонних драйверов ({})", packages.size());
    opt.info = L"Каждый пакет сохраняется в отдельный CAB-архив в выбранной директории.";
    wchar_t docs[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs);
    opt.path = wstring(docs) + L"\\Drivers";
    if (!runExportDialog(CWnd::FromHandle(mainWindow()), opt))
        return;
    SHCreateDirectoryExW(nullptr, opt.path.c_str(), nullptr);
    CWaitCursor wait;
    int ok = 0;
    StringList failed, warned;
    for (const inf::DriverPackage &p : packages) {
        const inf::ExportPlan plan = inf::planExport(p.storeInf, opt.flatten);
        wstring error;
        if (!plan.errors.empty()) {
            failed.push_back(p.publishedName + L" (" + p.originalName + L"): " + plan.errors.front());
            continue;
        }
        const wstring cab = opt.path + L"\\" + inf::baseName(p.publishedName) + L"_" + inf::suggestedCabName(p);
        if (!inf::exportToCab(plan, cab, &error)) {
            failed.push_back(p.publishedName + L": " + error);
            continue;
        }
        ++ok;
        for (const wstring &w : plan.warnings)
            if (w.find(L"Пути в INF") == wstring::npos)
                warned.push_back(p.publishedName + L": " + w);
    }
    wstring text = std::format(L"Экспортировано пакетов: {} из {}\nДиректория: {}", ok, packages.size(), opt.path);
    if (opt.flatten)
        text += L"\n\nПути в INF исправлены: подпись каталогов для изменённых INF недействительна.";
    if (!failed.empty())
        text += L"\n\nНе экспортированы:\n" + inf::join(failed, L"\n");
    if (!warned.empty())
        text += L"\n\nЗамечания:\n" + inf::join(warned, L"\n");
    runOutputDialog(CWnd::FromHandle(mainWindow()), L"Экспорт драйверов", inf::replaceAll(text, L"\n", L"\r\n"));
}

void CComponentData::cmdCreateInfForDevice(const inf::DeviceInfo &d)
{
    inf::GeneratorParams p;
    p.className = d.className;
    p.classGuid = d.classGuid;
    p.manufacturer = d.manufacturer;
    p.provider = d.manufacturer;
    inf::GeneratorDevice dev;
    dev.description = d.description;
    dev.hardwareId = d.hardwareIds.empty() ? d.instanceId : d.hardwareIds.front();
    for (size_t i = 1; i < d.hardwareIds.size(); ++i)
        dev.compatibleIds.push_back(d.hardwareIds[i]);
    for (const wstring &c : d.compatibleIds)
        dev.compatibleIds.push_back(c);
    p.devices = {dev};
    p.serviceName = inf::toIdentifier(d.description, L"MyDriver");
    if (p.serviceName.size() > 24)
        p.serviceName.resize(24);
    p.binaryName = inf::lower(p.serviceName) + L".sys";
    p.infName = inf::lower(p.serviceName) + L".inf";
    if (inf::istartsWith(dev.hardwareId, L"USB\\"))
        p.kind = inf::DriverKind::WinUsb;
    wstring text, name;
    if (!runNewInfWizard(CWnd::FromHandle(mainWindow()), text, name, &p))
        return;
    auto doc = std::make_unique<InfFileDoc>(m_nextId++);
    doc->setNew(text, name);
    InfFileDoc *f = addDocument(std::move(doc), true);
    if (message(L"Файл создан. Сохранить его на диск сейчас?", MB_YESNO | MB_ICONQUESTION) == IDYES)
        saveFile(f, true);
}

void CComponentData::cmdCopyIds(const inf::DeviceInfo &d)
{
    StringList lines = d.hardwareIds;
    lines.insert(lines.end(), d.compatibleIds.begin(), d.compatibleIds.end());
    if (lines.empty())
        lines.push_back(d.instanceId);
    const wstring text = inf::join(lines, L"\r\n");
    if (!OpenClipboard(mainWindow()))
        return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (mem) {
        memcpy(GlobalLock(mem), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(mem);
        if (!SetClipboardData(CF_UNICODETEXT, mem))
            GlobalFree(mem);
    }
    CloseClipboard();
}

// ------------------------------------------------------------------ окна свойств

STDMETHODIMP CComponentData::CreatePropertyPages(LPPROPERTYSHEETCALLBACK provider, LONG_PTR handle, LPDATAOBJECT obj)
{
    trace(L"CD::CreatePropertyPages");
    MANAGE_STATE();
    return createPages(targetOf(obj), provider, handle);
}

STDMETHODIMP CComponentData::QueryPagesFor(LPDATAOBJECT obj)
{
    trace(L"CD::QueryPagesFor");
    MANAGE_STATE();
    return hasPages(targetOf(obj)) ? S_OK : S_FALSE;
}

STDMETHODIMP CComponentData::GetWatermarks(LPDATAOBJECT, HBITMAP *watermark, HBITMAP *header, HPALETTE *palette, BOOL *stretch)
{
    if (watermark)
        *watermark = nullptr;
    if (header)
        *header = nullptr;
    if (palette)
        *palette = nullptr;
    if (stretch)
        *stretch = FALSE;
    return S_OK;
}

bool CComponentData::hasPages(const Target &t) const
{
    return targetHasPages(t);
}

HRESULT CComponentData::createPages(const Target &t, LPPROPERTYSHEETCALLBACK provider, LONG_PTR handle)
{
    return addPropertyPages(t, *this, provider, handle);
}
