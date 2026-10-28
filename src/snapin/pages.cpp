#include "stdafx.h"

#include "pages.h"

#include "dialogs.h"
#include "infedit.h"
#include "infhelp.h"
#include "infknowledge.h"
#include "inflexer.h"
#include "infverif.h"
#include "resource.h"

#include <format>
#include <map>

using inf::Analysis;
using inf::StringList;
using std::wstring;

namespace {

// Снимок данных файла для страницы свойств
struct PageContext {
    int fileId = 0;
    std::shared_ptr<const Analysis> analysis;
    wstring fileName;
};

// ------------------------------------------------------------------ базовая страница

class CMmcPage : public CPropertyPage {
public:
    explicit CMmcPage(UINT id) : CPropertyPage(id)
    {
        // свой обработчик PSPCB_RELEASE удаляет объект страницы, даже если вкладку не открывали
        m_mfcCallback = m_psp.pfnCallback;
        m_psp.pfnCallback = &CMmcPage::pageCallback;
        m_psp.dwFlags |= PSP_USECALLBACK;
    }

    ~CMmcPage() override
    {
        if (m_ownsNotify && m_notify)
            MMCFreeNotifyHandle(m_notify);
    }

    HPROPSHEETPAGE create(LONG_PTR notify, bool ownsNotify)
    {
        m_notify = notify;
        m_ownsNotify = ownsNotify;
        // MMCPropPageCallback не используется: она из mmc.lib и несовместима со статической MFC.
        // Состояние модуля MFC переключает pageCallback.
        return CreatePropertySheetPage(&m_psp);
    }

protected:
    virtual void init() = 0;

    BOOL OnInitDialog() override
    {
        CPropertyPage::OnInitDialog();
        // окно свойств создаёт MMC; кнопка «?» в его заголовке не нужна
        if (CWnd *sheet = GetParent(); sheet && (sheet->GetExStyle() & WS_EX_CONTEXTHELP)) {
            sheet->ModifyStyleEx(WS_EX_CONTEXTHELP, 0);
            sheet->SetWindowPos(nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        init();
        m_ready = true;
        return TRUE;
    }

    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override
    {
        const BOOL r = CPropertyPage::OnCommand(wParam, lParam);
        const UINT code = HIWORD(wParam);
        if (m_ready && (code == EN_CHANGE || code == CBN_SELCHANGE || code == CBN_EDITCHANGE || code == BN_CLICKED))
            SetModified(TRUE);
        return r;
    }

    BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT *result) override
    {
        const auto *hdr = reinterpret_cast<NMHDR *>(lParam);
        if (m_ready && hdr->code == DTN_DATETIMECHANGE)
            SetModified(TRUE);
        return CPropertyPage::OnNotify(wParam, lParam, result);
    }

    // Выполнить действие в основном потоке MMC
    void post(std::function<void(CComponentData &)> action)
    {
        if (m_notify)
            MMCPropertyChangeNotify(m_notify, reinterpret_cast<LPARAM>(new PropertyChange{std::move(action)}));
    }

    void postTransform(int fileId, Transform t)
    {
        post([fileId, t](CComponentData &cd) {
            if (InfFileDoc *f = cd.fileById(fileId))
                cd.applyTransform(f, t);
        });
    }

    CComboBox &combo(int id) { return *static_cast<CComboBox *>(GetDlgItem(id)); }

    bool m_ready = false;

private:
    static UINT CALLBACK pageCallback(HWND hwnd, UINT msg, LPPROPSHEETPAGE ppsp)
    {
        // окно свойств MMC работает в своём потоке: страницы создаются в состоянии модуля оснастки
        MANAGE_STATE();
        auto *page = static_cast<CMmcPage *>(reinterpret_cast<CPropertyPage *>(ppsp->lParam));
        const UINT r = page->m_mfcCallback ? page->m_mfcCallback(hwnd, msg, ppsp) : 1;
        if (msg == PSPCB_RELEASE)
            delete page;
        return r;
    }

    LPFNPSPCALLBACK m_mfcCallback = nullptr;
    LONG_PTR m_notify = 0;
    bool m_ownsNotify = false;
};

// Значение записи в том виде, как оно написано после '=' (без комментария)
wstring rawValueText(const Analysis &a, int idx)
{
    const inf::InfLine &l = a.doc.lines()[size_t(idx)];
    if (l.firstLine == l.lastLine) {
        wstring line = a.doc.physicalLine(l.firstLine);
        if (!l.comment.empty() && line.size() >= l.comment.size()
            && line.compare(line.size() - l.comment.size(), l.comment.size(), l.comment) == 0)
            line.resize(line.size() - l.comment.size());
        if (l.hasKey) {
            const size_t eq = line.find(L'=', size_t(l.key.column + l.key.length));
            if (eq != wstring::npos)
                line = line.substr(eq + 1);
        }
        return inf::trim(line);
    }
    StringList parts;
    for (const inf::InfValue &v : l.values)
        parts.push_back(v.raw);
    return inf::join(parts, L", ");
}

wstring formatLine(const wstring &key, const wstring &value, const wstring &comment)
{
    wstring line = key.empty() ? value : key + L" = " + value;
    wstring c = inf::trim(comment);
    if (!c.empty()) {
        if (c.front() != L';')
            c = L"; " + c;
        line += L"  " + c;
    }
    return line;
}

// Замена записи с проверкой, что она не изменилась с момента открытия окна свойств
Transform replaceLineTransform(int logicalLine, const wstring &lineText, const wstring &newText, bool keepComment)
{
    return [logicalLine, lineText, newText, keepComment](const Analysis &a, wstring &text, wstring &error) {
        const int idx = locateLine(a, logicalLine, lineText);
        if (idx < 0) {
            error = L"Запись изменилась после открытия окна свойств. Изменения не применены.";
            return false;
        }
        text = inf::applyEdit(text, inf::replaceLineEdit(a.doc, idx, newText, keepComment));
        return true;
    };
}

// ------------------------------------------------------------------ Параметры

class CSettingsPage : public CMmcPage {
public:
    explicit CSettingsPage(const SnapinSettings &s) : CMmcPage(IDD_PAGE_SETTINGS), m_settings(s) {}

protected:
    void init() override
    {
        setDlgText(this, IDC_VERIF_PATH, m_settings.infVerifPath);
        const wstring found = inf::findInfVerif();
        setDlgText(this, IDC_VERIF_FOUND, found.empty() ? L"Автоматически: не найден (необходимо установить WDK или указать путь)"
                                                        : L"Автоматически: " + found);
        for (const inf::InfVerifMode &m : inf::infVerifModes()) {
            const int i = combo(IDC_VERIF_MODE).AddString(m.title.c_str());
            if (m.key == m_settings.infVerifMode)
                combo(IDC_VERIF_MODE).SetCurSel(i);
        }
        if (combo(IDC_VERIF_MODE).GetCurSel() < 0)
            combo(IDC_VERIF_MODE).SetCurSel(0);
    }

    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override
    {
        if (LOWORD(wParam) == IDC_BROWSE && HIWORD(wParam) == BN_CLICKED) {
            CFileDialog dlg(TRUE, L"exe", L"infverif.exe", OFN_FILEMUSTEXIST, L"InfVerif (infverif.exe)|infverif.exe|Программы (*.exe)|*.exe||", this);
            if (dlg.DoModal() == IDOK)
                SetDlgItemText(IDC_VERIF_PATH, dlg.GetPathName());
        }
        return CMmcPage::OnCommand(wParam, lParam);
    }

    BOOL OnApply() override
    {
        SnapinSettings s = m_settings;
        s.infVerifPath = inf::trim(dlgText(this, IDC_VERIF_PATH));
        const int sel = combo(IDC_VERIF_MODE).GetCurSel();
        if (sel >= 0 && sel < int(inf::infVerifModes().size()))
            s.infVerifMode = inf::infVerifModes()[size_t(sel)].key;
        m_settings = s;
        post([s](CComponentData &cd) { cd.setSettings(s); });
        return CMmcPage::OnApply();
    }

private:
    SnapinSettings m_settings;
};

// ------------------------------------------------------------------ Файл

class CFilePage : public CMmcPage {
public:
    CFilePage(const PageContext &ctx, const wstring &path, const inf::FileFormat &format, const wstring &stats)
        : CMmcPage(IDD_PAGE_FILE), m_ctx(ctx), m_path(path), m_format(format), m_stats(stats)
    {
    }

protected:
    void init() override
    {
        setDlgText(this, IDC_PATH, m_path.empty() ? L"(файл ещё не сохранён)" : m_path);
        for (inf::Encoding e : {inf::Encoding::Ansi, inf::Encoding::Utf16LE, inf::Encoding::Utf8Bom, inf::Encoding::Utf8, inf::Encoding::Utf16BE}) {
            const int i = combo(IDC_ENCODING).AddString(inf::encodingName(e).c_str());
            combo(IDC_ENCODING).SetItemData(i, DWORD_PTR(e));
            if (e == m_format.encoding)
                combo(IDC_ENCODING).SetCurSel(i);
        }
        for (inf::LineEnding e : {inf::LineEnding::CRLF, inf::LineEnding::LF, inf::LineEnding::CR}) {
            const int i = combo(IDC_EOL).AddString(inf::lineEndingName(e).c_str());
            combo(IDC_EOL).SetItemData(i, DWORD_PTR(e));
            if (e == m_format.lineEnding)
                combo(IDC_EOL).SetCurSel(i);
        }
        CheckDlgButton(IDC_CTRLZ, m_format.trailingCtrlZ ? BST_CHECKED : BST_UNCHECKED);
        setDlgText(this, IDC_STATS, m_stats);
    }

    BOOL OnApply() override
    {
        inf::FileFormat f = m_format;
        f.encoding = inf::Encoding(combo(IDC_ENCODING).GetItemData(combo(IDC_ENCODING).GetCurSel()));
        f.lineEnding = inf::LineEnding(combo(IDC_EOL).GetItemData(combo(IDC_EOL).GetCurSel()));
        f.trailingCtrlZ = IsDlgButtonChecked(IDC_CTRLZ) == BST_CHECKED;
        if (f.encoding == inf::Encoding::Utf16LE || f.encoding == inf::Encoding::Utf16BE)
            f.hasBom = true;
        f.mixedLineEndings = false;
        m_format = f;
        const int id = m_ctx.fileId;
        post([id, f](CComponentData &cd) {
            if (InfFileDoc *doc = cd.fileById(id)) {
                doc->setFormat(f);
                cd.fileChanged(doc);
            }
        });
        return CMmcPage::OnApply();
    }

private:
    PageContext m_ctx;
    wstring m_path;
    inf::FileFormat m_format;
    wstring m_stats;
};

// ------------------------------------------------------------------ [Version]

class CVersionPage : public CMmcPage {
public:
    explicit CVersionPage(const PageContext &ctx) : CMmcPage(IDD_PAGE_VERSION), m_ctx(ctx) {}

protected:
    void init() override
    {
        const Analysis &a = *m_ctx.analysis;
        m_date.SubclassDlgItem(IDC_DATE, this);
        m_date.SetFormat(L"MM/dd/yyyy");

        combo(IDC_SIGNATURE).AddString(L"$WINDOWS NT$");
        combo(IDC_SIGNATURE).AddString(L"$Chicago$");
        for (const inf::DeviceClassInfo &c : inf::deviceClasses())
            combo(IDC_CLASS).AddString(c.name.c_str());

        const wstring sig = inf::unquote(raw(L"Signature"));
        combo(IDC_SIGNATURE).SetWindowText(sig.empty() ? L"$WINDOWS NT$" : sig.c_str());
        combo(IDC_CLASS).SetWindowText(raw(L"Class").c_str());
        setDlgText(this, IDC_CLASSGUID, raw(L"ClassGuid"));
        setDlgText(this, IDC_EXTID, raw(L"ExtensionId"));
        setDlgText(this, IDC_PROVIDER, raw(L"Provider"));
        setDlgText(this, IDC_CATALOG, raw(L"CatalogFile"));
        CheckDlgButton(IDC_LOCKDOWN, inf::trim(a.doc.value(L"Version", L"PnpLockdown")) == L"1" ? BST_CHECKED : BST_UNCHECKED);

        inf::Date d = inf::Date::fromInf(a.model.resolve(a.doc.value(L"Version", L"DriverVer", 0)));
        if (!d.isValid())
            d = inf::Date::today();
        SYSTEMTIME st{};
        st.wYear = WORD(d.year);
        st.wMonth = WORD(d.month);
        st.wDay = WORD(d.day);
        m_date.SetTime(&st);
        setDlgText(this, IDC_VERSION, inf::trim(a.model.resolve(a.doc.value(L"Version", L"DriverVer", 1))));

        for (const wchar_t *k : {L"Signature", L"Class", L"ClassGuid", L"ExtensionId", L"Provider", L"DriverVer", L"CatalogFile", L"PnpLockdown"})
            m_existed[k] = a.doc.entry(L"Version", k) != nullptr;
        m_initial = values();
        updateInfo();
    }

    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override
    {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (code == BN_CLICKED) {
            switch (id) {
            case IDC_STD_GUID:
                if (const inf::DeviceClassInfo *c = inf::findClassByName(comboText(combo(IDC_CLASS))))
                    setDlgText(this, IDC_CLASSGUID, c->guid);
                break;
            case IDC_NEW_GUID: setDlgText(this, IDC_CLASSGUID, inf::newGuid()); break;
            case IDC_NEW_EXTID: setDlgText(this, IDC_EXTID, inf::newGuid()); break;
            case IDC_TODAY: {
                SYSTEMTIME st{};
                GetLocalTime(&st);
                m_date.SetTime(&st);
                break;
            }
            case IDC_BUMP: {
                StringList parts = inf::split(inf::trim(dlgText(this, IDC_VERSION)), L'.');
                if (parts.size() == 1 && parts[0].empty())
                    parts = {L"1", L"0", L"0", L"0"};
                while (parts.size() < 4)
                    parts.push_back(L"0");
                uint32_t n = 0;
                inf::parseUInt(parts.back(), n);
                parts.back() = std::to_wstring(n + 1);
                setDlgText(this, IDC_VERSION, inf::join(parts, L"."));
                break;
            }
            }
        }
        if (id == IDC_CLASS && (code == CBN_EDITCHANGE || code == CBN_SELCHANGE)) {
            if (code == CBN_SELCHANGE) {
                CString s;
                combo(IDC_CLASS).GetLBText(combo(IDC_CLASS).GetCurSel(), s);
                combo(IDC_CLASS).SetWindowText(s);
                // стандартному классу — стандартный GUID
                if (const inf::DeviceClassInfo *c = inf::findClassByName(s.GetString()))
                    setDlgText(this, IDC_CLASSGUID, c->guid);
            }
            updateInfo();
        }
        if (id == IDC_PROVIDER && code == EN_CHANGE)
            updateInfo();
        return CMmcPage::OnCommand(wParam, lParam);
    }

    BOOL OnApply() override
    {
        const auto now = values();
        std::vector<std::pair<wstring, wstring>> changes;  // ключ -> новое значение (пусто — удалить)
        for (const wchar_t *k : {L"Signature", L"Class", L"ClassGuid", L"ExtensionId", L"Provider", L"CatalogFile", L"DriverVer", L"PnpLockdown"}) {
            const wstring v = now.at(k);
            if (v == m_initial.at(k))
                continue;
            if (v.empty() && !m_existed[k])
                continue;
            changes.push_back({k, v});
            m_existed[k] = !v.empty();
        }
        m_initial = now;
        if (!changes.empty())
            postTransform(m_ctx.fileId, [changes](const Analysis &, wstring &text, wstring &) {
                for (const auto &c : changes) {
                    const inf::InfDocument doc = inf::InfDocument::parse(text);
                    text = inf::applyEdit(text, c.second.empty() ? inf::removeKeyEdit(doc, L"Version", c.first)
                                                                 : inf::setValueEdit(doc, L"Version", c.first, c.second));
                }
                return true;
            });
        return CMmcPage::OnApply();
    }

private:
    wstring raw(const wchar_t *key) const
    {
        const inf::InfLine *l = m_ctx.analysis->doc.entry(L"Version", key);
        if (!l)
            return {};
        StringList parts;
        for (const inf::InfValue &v : l->values)
            parts.push_back(inf::trim(v.raw));
        return inf::join(parts, L",");
    }

    std::map<wstring, wstring> values()
    {
        std::map<wstring, wstring> v;
        const wstring sig = inf::trim(comboText(combo(IDC_SIGNATURE)));
        v[L"Signature"] = sig.empty() ? L"" : inf::quote(sig);
        v[L"Class"] = inf::trim(comboText(combo(IDC_CLASS)));
        v[L"ClassGuid"] = inf::trim(dlgText(this, IDC_CLASSGUID));
        v[L"ExtensionId"] = inf::trim(dlgText(this, IDC_EXTID));
        v[L"Provider"] = inf::trim(dlgText(this, IDC_PROVIDER));
        SYSTEMTIME st{};
        m_date.GetTime(&st);
        const wstring ver = inf::trim(dlgText(this, IDC_VERSION));
        v[L"DriverVer"] = inf::Date{st.wYear, st.wMonth, st.wDay}.toInf() + (ver.empty() ? L"" : L"," + ver);
        v[L"CatalogFile"] = inf::trim(dlgText(this, IDC_CATALOG));
        v[L"PnpLockdown"] = IsDlgButtonChecked(IDC_LOCKDOWN) == BST_CHECKED ? L"1" : L"";
        return v;
    }

    void updateInfo()
    {
        const wstring name = inf::trim(comboText(combo(IDC_CLASS)));
        const inf::DeviceClassInfo *c = inf::findClassByName(name);
        setDlgText(this, IDC_CLASS_INFO, c ? c->description + L", " + c->guid : (name.empty() ? L"" : L"Нестандартный класс"));
        GetDlgItem(IDC_STD_GUID)->EnableWindow(c != nullptr);
        const bool ext = inf::iequals(name, L"Extension");
        GetDlgItem(IDC_EXTID)->EnableWindow(ext || !inf::trim(dlgText(this, IDC_EXTID)).empty());
        GetDlgItem(IDC_NEW_EXTID)->EnableWindow(ext);
        const wstring p = dlgText(this, IDC_PROVIDER);
        const wstring r = m_ctx.analysis->model.resolve(p);
        setDlgText(this, IDC_PROVIDER_RESOLVED, r != p ? L"= " + r : L"");
    }

    PageContext m_ctx;
    CDateTimeCtrl m_date;
    std::map<wstring, wstring> m_initial;
    std::map<wstring, bool> m_existed;
};

// ------------------------------------------------------------------ Запись

class CEntryPage : public CMmcPage {
public:
    CEntryPage(const PageContext &ctx, int logicalLine) : CMmcPage(IDD_PAGE_ENTRY), m_ctx(ctx), m_line(logicalLine)
    {
        m_lineText = ctx.analysis->doc.lineText(logicalLine);
    }

protected:
    void init() override
    {
        const Analysis &a = *m_ctx.analysis;
        const inf::InfLine &l = a.doc.lines()[size_t(m_line)];
        const wstring section = inf::sectionOfLine(a, m_line);
        setDlgText(this, IDC_SECTION, section.empty() ? L"(вне секций)" : L"[" + section + L"]");
        setDlgText(this, IDC_LINE, l.firstLine == l.lastLine ? std::format(L"строка {}", l.firstLine + 1)
                                                             : std::format(L"строки {}–{}", l.firstLine + 1, l.lastLine + 1));
        setDlgText(this, IDC_KEY, l.hasKey ? inf::trim(l.key.raw) : L"");
        setDlgText(this, IDC_VALUE, rawValueText(a, m_line));
        setDlgText(this, IDC_COMMENT, l.comment);
        wstring help = inf::describeEntry(a, m_line);
        if (!l.error.empty())
            help = L"Синтаксическая ошибка: " + l.error + L"\r\n\r\n" + help;
        if (l.firstLine != l.lastLine)
            help += L"\r\n\r\nЗапись занимает несколько строк (продолжение через «\\»); после изменения она будет записана одной строкой.";
        setDlgText(this, IDC_HELPTEXT, help);
    }

    BOOL OnApply() override
    {
        const wstring key = inf::trim(dlgText(this, IDC_KEY));
        const wstring value = inf::trim(dlgText(this, IDC_VALUE));
        if (key.empty() && value.empty()) {
            AfxMessageBox(L"Запись не может быть пустой. Для удаления записи используется команда «Удалить».", MB_ICONWARNING);
            return FALSE;
        }
        const wstring newText = formatLine(key, value, dlgText(this, IDC_COMMENT));
        postTransform(m_ctx.fileId, replaceLineTransform(m_line, m_lineText, newText, false));
        // дальнейшие изменения на этой странице относятся уже к новому тексту
        const inf::InfLine &l = m_ctx.analysis->doc.lines()[size_t(m_line)];
        const wstring first = m_ctx.analysis->doc.physicalLine(l.firstLine);
        m_lineText = first.substr(0, first.find_first_not_of(L" \t")) + newText;
        if (m_lineText.find_first_not_of(L" \t") == wstring::npos)
            m_lineText = newText;
        return CMmcPage::OnApply();
    }

private:
    PageContext m_ctx;
    int m_line;
    wstring m_lineText;
};

// ------------------------------------------------------------------ Устройство

class CDevicePage : public CMmcPage {
public:
    CDevicePage(const PageContext &ctx, int logicalLine) : CMmcPage(IDD_PAGE_DEVICE), m_ctx(ctx), m_line(logicalLine)
    {
        m_lineText = ctx.analysis->doc.lineText(logicalLine);
    }

protected:
    void init() override
    {
        const Analysis &a = *m_ctx.analysis;
        const inf::DeviceModel *dm = nullptr;
        for (const inf::DeviceModel &m : a.model.models())
            if (m.logicalLine == m_line)
                dm = &m;
        if (!dm)
            return;
        setDlgText(this, IDC_DESC, dm->description);
        setDlgText(this, IDC_HWID, dm->hardwareIds.empty() ? L"" : dm->hardwareIds.front());
        setDlgText(this, IDC_COMPAT, inf::join(dm->compatibleIds, L", "));
        StringList installs;
        for (const inf::DeviceModel &m : a.model.models())
            if (!m.installBase.empty() && !inf::icontains(installs, m.installBase))
                installs.push_back(m.installBase);
        for (const wstring &s : installs)
            combo(IDC_INSTALL).AddString(s.c_str());
        combo(IDC_INSTALL).SetWindowText(dm->installBase.c_str());
        setDlgText(this, IDC_MODELS, L"[" + dm->modelsSection + L"]");
        setDlgText(this, IDC_ARCH, dm->architecture.empty() ? L"любая" : dm->architecture);
        setDlgText(this, IDC_HINT, dm->installSection.empty() ? L"секция установки не найдена" : L"[" + dm->installSection + L"]");

        // описание вида %Ключ% можно править прямо здесь
        const auto tokens = inf::InfModel::stringTokens(dm->description);
        if (tokens.size() == 1 && tokens[0].length == int(dm->description.size()) && !tokens[0].numeric
            && a.model.hasString(tokens[0].name)) {
            m_stringKey = tokens[0].name;
            m_stringValue = a.model.stringValue(m_stringKey);
            setDlgText(this, IDC_STRING_VALUE, m_stringValue);
            setDlgText(this, IDC_DESC_RESOLVED, L"строка %" + m_stringKey + L"% в [" + a.model.baseStringSection() + L"]");
        } else {
            GetDlgItem(IDC_STRING_VALUE)->EnableWindow(FALSE);
            setDlgText(this, IDC_DESC_RESOLVED, L"= " + dm->displayName);
        }
    }

    BOOL OnApply() override
    {
        const wstring desc = inf::trim(dlgText(this, IDC_DESC));
        const wstring hwid = inf::trim(dlgText(this, IDC_HWID));
        const wstring install = inf::trim(comboText(combo(IDC_INSTALL)));
        if (desc.empty() || hwid.empty() || install.empty()) {
            AfxMessageBox(L"Необходимо указать описание, ID оборудования и секцию установки.", MB_ICONWARNING);
            return FALSE;
        }
        StringList ids = {hwid};
        for (const wstring &c : inf::split(dlgText(this, IDC_COMPAT), L',', true))
            if (!inf::trim(c).empty())
                ids.push_back(inf::trim(c));
        const wstring newText = desc + L" = " + install + L", " + inf::join(ids, L", ");
        Transform line = replaceLineTransform(m_line, m_lineText, newText, true);
        const wstring stringValue = dlgText(this, IDC_STRING_VALUE);
        const wstring key = m_stringKey;
        const bool stringChanged = !key.empty() && stringValue != m_stringValue;
        m_stringValue = stringValue;
        postTransform(m_ctx.fileId, [line, key, stringValue, stringChanged](const Analysis &a, wstring &text, wstring &error) {
            if (!line(a, text, error))
                return false;
            if (stringChanged) {
                const auto b = Analysis::run(text);
                const wstring sec = b->model.baseStringSection().empty() ? L"Strings" : b->model.baseStringSection();
                text = inf::applyEdit(text, inf::setValueEdit(b->doc, sec, key, inf::quote(stringValue)));
            }
            return true;
        });
        const inf::InfLine &l = m_ctx.analysis->doc.lines()[size_t(m_line)];
        const wstring first = m_ctx.analysis->doc.physicalLine(l.firstLine);
        const size_t indent = first.find_first_not_of(L" \t");
        m_lineText = (indent == wstring::npos ? L"" : first.substr(0, indent)) + newText + (l.comment.empty() ? L"" : L" " + l.comment);
        return CMmcPage::OnApply();
    }

private:
    PageContext m_ctx;
    int m_line;
    wstring m_lineText;
    wstring m_stringKey;
    wstring m_stringValue;
};

// ------------------------------------------------------------------ Строка

class CStringPage : public CMmcPage {
public:
    CStringPage(const PageContext &ctx, int logicalLine) : CMmcPage(IDD_PAGE_STRING), m_ctx(ctx), m_line(logicalLine)
    {
        m_lineText = ctx.analysis->doc.lineText(logicalLine);
    }

protected:
    void init() override
    {
        const Analysis &a = *m_ctx.analysis;
        const inf::InfLine &l = a.doc.lines()[size_t(m_line)];
        m_section = inf::sectionOfLine(a, m_line);
        m_key = inf::trim(l.key.text);
        setDlgText(this, IDC_SECTION, L"[" + m_section + L"]");
        setDlgText(this, IDC_KEY, m_key);
        StringList vals;
        for (const inf::InfValue &v : l.values)
            vals.push_back(v.text);
        setDlgText(this, IDC_VALUE, inf::join(vals, L","));
        setDlgText(this, IDC_USES, std::format(L"Используется: {} раз(а)", a.model.stringUses(m_key)));
        StringList others;
        for (const wstring &sec : a.model.stringSections()) {
            if (inf::iequals(sec, m_section))
                continue;
            for (const inf::StringDef &d : a.model.strings(sec))
                if (inf::iequals(d.key, m_key))
                    others.push_back(stringSectionTitle(sec) + L":\r\n    " + d.value);
        }
        setDlgText(this, IDC_OTHERS, others.empty() ? L"(нет)" : inf::join(others, L"\r\n"));
    }

    BOOL OnApply() override
    {
        const wstring key = inf::trim(dlgText(this, IDC_KEY));
        if (key.empty() || key.find_first_of(L"%=,;\" ") != wstring::npos) {
            AfxMessageBox(L"Ключ строки не должен быть пустым и не должен содержать пробелы и символы % = , ; \"", MB_ICONWARNING);
            return FALSE;
        }
        const wstring value = inf::replaceAll(inf::replaceAll(dlgText(this, IDC_VALUE), L"\r\n", L" "), L"\n", L" ");
        const wstring newText = key + L" = " + inf::quote(value);
        Transform line = replaceLineTransform(m_line, m_lineText, newText, true);
        const wstring oldKey = m_key;
        postTransform(m_ctx.fileId, [line, oldKey, key](const Analysis &a, wstring &text, wstring &error) {
            if (!line(a, text, error))
                return false;
            if (oldKey == key)
                return true;
            // переименование ключа: ссылки %старый% заменяются на %новый%
            const auto b = Analysis::run(text);
            std::vector<inf::SpanEdit> spans;
            for (const inf::InfLine &l : b->doc.lines()) {
                if (l.kind != inf::InfLine::Kind::Entry)
                    continue;
                if (l.section >= 0 && b->model.role(b->doc.sections()[size_t(l.section)].name) == inf::SectionRole::Strings)
                    continue;
                auto scan = [&](const inf::InfValue &v) {
                    for (const inf::StringToken &t : inf::InfModel::stringTokens(v.raw))
                        if (!t.numeric && inf::iequals(t.name, oldKey) && t.start + t.length <= v.length)
                            spans.push_back({v.line, v.column + t.start, t.length, L"%" + key + L"%"});
                };
                if (l.hasKey)
                    scan(l.key);
                for (const inf::InfValue &v : l.values)
                    scan(v);
            }
            text = inf::applySpans(text, spans);
            return true;
        });
        m_key = key;
        const inf::InfLine &l = m_ctx.analysis->doc.lines()[size_t(m_line)];
        const wstring first = m_ctx.analysis->doc.physicalLine(l.firstLine);
        const size_t indent = first.find_first_not_of(L" \t");
        m_lineText = (indent == wstring::npos ? L"" : first.substr(0, indent)) + newText + (l.comment.empty() ? L"" : L" " + l.comment);
        return CMmcPage::OnApply();
    }

private:
    PageContext m_ctx;
    int m_line;
    wstring m_lineText;
    wstring m_section;
    wstring m_key;
};

// ------------------------------------------------------------------ Секция

class CSectionPage : public CMmcPage {
public:
    CSectionPage(const PageContext &ctx, const wstring &section) : CMmcPage(IDD_PAGE_SECTION), m_ctx(ctx), m_section(section) {}

protected:
    void init() override
    {
        const Analysis &a = *m_ctx.analysis;
        const inf::InfSection *s = a.doc.section(m_section);
        if (!s)
            return;
        setDlgText(this, IDC_NAME, s->name);
        StringList ranges;
        for (const inf::InfSectionChunk &c : s->chunks)
            ranges.push_back(std::format(L"{}–{}", c.headerLine + 1, c.lastContentLine + 1));
        setDlgText(this, IDC_LINE, std::format(L"Строки: {}; записей: {}", inf::join(ranges, L", "), s->entries.size()));
        setDlgText(this, IDC_HELPTEXT, inf::describeSection(a, s->name));
    }

private:
    PageContext m_ctx;
    wstring m_section;
};

// ------------------------------------------------------------------ Проблема

class CProblemPage : public CMmcPage {
public:
    CProblemPage(const PageContext &ctx, const ResultRow &row) : CMmcPage(IDD_PAGE_PROBLEM), m_ctx(ctx), m_row(row) {}

protected:
    void init() override
    {
        const inf::Diagnostic &d = m_row.diagnostic;
        setDlgText(this, IDC_SEVERITY, inf::severityName(d.severity));
        setDlgText(this, IDC_CODE, d.code);
        setDlgText(this, IDC_SOURCE, d.source);
        setDlgText(this, IDC_LINE, std::format(L"{}, столбец {}", d.line + 1, d.column + 1));
        setDlgText(this, IDC_SECTION, m_row.section.empty() ? L"(нет)" : L"[" + m_row.section + L"]");
        setDlgText(this, IDC_MESSAGE, d.message);
        const Analysis &a = *m_ctx.analysis;
        StringList lines;
        for (int i = std::max(0, d.line - 1); i <= std::min(int(a.doc.physicalLines().size()) - 1, d.line + 1); ++i)
            lines.push_back(std::format(L"{:5} {} {}", i + 1, i == d.line ? L'>' : L' ', a.doc.physicalLine(i)));
        setDlgText(this, IDC_LINETEXT, inf::join(lines, L"\r\n"));
    }

private:
    PageContext m_ctx;
    ResultRow m_row;
};

// ------------------------------------------------------------------ Сведения (только чтение)

// Пара «свойство — значение»; пустая пара — разделитель групп
using InfoRows = std::vector<std::pair<wstring, wstring>>;

class CInfoPage : public CMmcPage {
public:
    CInfoPage(const wstring &title, InfoRows rows) : CMmcPage(IDD_PAGE_INFO), m_title(title), m_rows(std::move(rows))
    {
        m_psp.dwFlags |= PSP_USETITLE;
        m_psp.pszTitle = m_title.c_str();
    }

protected:
    void init() override
    {
        // список в шрифте диалога, как на вкладках свойств Диспетчера устройств
        m_list.SubclassDlgItem(IDC_OUTPUT, this);
        m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_DOUBLEBUFFER);
        SetWindowTheme(m_list.GetSafeHwnd(), L"Explorer", nullptr);
        CRect r;
        m_list.GetClientRect(&r);
        CDC *dc = m_list.GetDC();
        CFont *old = dc->SelectObject(m_list.GetFont());
        int nameWidth = 0;
        for (const auto &row : m_rows)
            nameWidth = std::max(nameWidth, int(dc->GetTextExtent(row.first.c_str()).cx));
        dc->SelectObject(old);
        m_list.ReleaseDC(dc);
        nameWidth = std::min(nameWidth + 16, r.Width() / 2);
        m_list.InsertColumn(0, L"Свойство", LVCFMT_LEFT, nameWidth);
        m_list.InsertColumn(1, L"Значение", LVCFMT_LEFT, r.Width() - nameWidth - GetSystemMetrics(SM_CXVSCROLL));
        for (int i = 0; i < int(m_rows.size()); ++i) {
            m_list.InsertItem(i, m_rows[size_t(i)].first.c_str());
            m_list.SetItemText(i, 1, m_rows[size_t(i)].second.c_str());
        }
    }

    BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT *result) override
    {
        // Ctrl+C копирует значение выбранной строки
        const auto *hdr = reinterpret_cast<NMHDR *>(lParam);
        if (hdr->idFrom == IDC_OUTPUT && hdr->code == LVN_KEYDOWN) {
            const auto *key = reinterpret_cast<const NMLVKEYDOWN *>(lParam);
            const int i = m_list.GetNextItem(-1, LVNI_SELECTED);
            if (key->wVKey == 'C' && (GetKeyState(VK_CONTROL) & 0x8000) && i >= 0)
                copyText(m_rows[size_t(i)].second.empty() ? m_rows[size_t(i)].first : m_rows[size_t(i)].second);
        }
        return CMmcPage::OnNotify(wParam, lParam, result);
    }

private:
    void copyText(const wstring &text)
    {
        if (!OpenClipboard())
            return;
        EmptyClipboard();
        if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t))) {
            memcpy(GlobalLock(mem), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem))
                GlobalFree(mem);
        }
        CloseClipboard();
    }

    wstring m_title;
    InfoRows m_rows;
    CListCtrl m_list;
};

InfoRows deviceRows(const inf::DeviceInfo &d)
{
    InfoRows s;
    auto row = [&](const wchar_t *name, const wstring &value) {
        if (!value.empty())
            s.push_back({name, value});
    };
    auto list = [&](const wchar_t *name, const inf::StringList &values) {
        for (size_t i = 0; i < values.size(); ++i)
            s.push_back({i == 0 ? wstring(name) : wstring(), values[i]});
    };
    row(L"Устройство", d.description);
    row(L"Экземпляр", d.instanceId);
    row(L"Класс", d.classDescription + (d.className.empty() ? L"" : L" (" + d.className + L")"));
    row(L"GUID класса", d.classGuid);
    row(L"Производитель", d.manufacturer);
    row(L"Состояние", !d.present   ? wstring(L"не подключено")
                      : d.hasProblem ? std::format(L"ошибка, код {}", d.problem)
                                     : wstring(L"работает"));
    if (d.hidden)
        row(L"Видимость", L"скрыто в Диспетчере устройств");
    s.push_back({});
    row(L"Поставщик драйвера", d.driverProvider);
    row(L"Версия драйвера", d.driverVersion);
    row(L"Дата драйвера", d.driverDate);
    row(L"INF", d.infName.empty() ? wstring(L"драйвер не установлен")
                                  : d.infName + (d.thirdParty() ? L" (сторонний пакет)" : L" (входит в Windows)"));
    row(L"Секция установки", d.infSection);
    row(L"Совпавший ID", d.matchingId);
    if (!d.hardwareIds.empty()) {
        s.push_back({});
        list(L"ID оборудования", d.hardwareIds);
    }
    if (!d.compatibleIds.empty()) {
        s.push_back({});
        list(L"Совместимые ID", d.compatibleIds);
    }
    return s;
}

InfoRows packageRows(const inf::DriverPackage &p)
{
    InfoRows s;
    auto row = [&](const wchar_t *name, const wstring &value) {
        if (!value.empty())
            s.push_back({name, value});
    };
    row(L"Опубликован как", p.publishedPath);
    row(L"Исходный INF", p.originalName);
    row(L"Хранилище", p.storeDir);
    row(L"Поставщик", p.provider);
    row(L"Класс", p.className);
    row(L"DriverVer", p.driverVer);
    row(L"Каталог", p.catalog);
    row(L"Устройств", std::to_wstring(p.devices));
    return s;
}


wstring fileStats(const InfFileDoc &f)
{
    const Analysis &a = *f.analysis();
    StringList s;
    s.push_back(std::format(L"Строк текста: {}", a.doc.physicalLines().size()));
    s.push_back(std::format(L"Секций: {}", a.doc.sections().size()));
    s.push_back(std::format(L"Производителей: {}, устройств: {}", a.model.manufacturers().size(), a.model.models().size()));
    s.push_back(std::format(L"Строковых констант: {} (секций строк: {})", a.model.allStrings().size(), a.model.stringSections().size()));
    s.push_back(std::format(L"Копируемых файлов: {}", a.model.copiedFiles().size()));
    s.push_back(std::format(L"Ошибок: {}, предупреждений: {}, замечаний: {}", f.count(inf::Severity::Error),
                            f.count(inf::Severity::Warning), f.count(inf::Severity::Info)));
    if (f.verifRan())
        s.push_back(f.verifValid() ? L"InfVerif: INF корректен" : L"InfVerif: INF не корректен");
    s.push_back(f.isModified() ? L"Есть несохранённые изменения" : L"Изменений нет");
    if (f.format().mixedLineEndings)
        s.push_back(L"В файле смешаны концы строк");
    return inf::join(s, L"\r\n");
}

int manufacturerLine(const Analysis &a, const wstring &key)
{
    if (const inf::Manufacturer *m = manufacturerForKey(a, key))
        return m->logicalLine;
    return -1;
}

} // namespace

bool targetHasPages(const Target &t)
{
    // при добавлении оснастки MMC спрашивает страницы мастера настройки — их нет
    if (t.type == CCT_SNAPIN_MANAGER)
        return false;
    if (t.row) {
        switch (t.row->type) {
        case RowType::SysDevice:
        case RowType::Package: return true;
        case RowType::CopiedFile: return t.row->logicalLine >= 0;
        default: return t.row->logicalLine >= 0 || t.row->type == RowType::Problem;
        }
    }
    if (!t.node)
        return false;
    switch (t.node->type) {
    case NodeType::Root:
    case NodeType::File:
    case NodeType::Version:
    case NodeType::Section:
    case NodeType::StringSection:
        return true;
    case NodeType::Manufacturer:
        return manufacturerLine(*t.node->file->analysis(), t.node->key) >= 0;
    default:
        return false;
    }
}

HRESULT addPropertyPages(const Target &t, CComponentData &owner, LPPROPERTYSHEETCALLBACK provider, LONG_PTR handle)
{
    if (!provider)
        return E_POINTER;
    if (!targetHasPages(t))
        return S_FALSE;
    std::vector<CMmcPage *> pages;
    InfFileDoc *f = t.file();
    PageContext ctx;
    if (f) {
        ctx.fileId = f->id();
        ctx.analysis = f->analysis();
        ctx.fileName = f->fileName();
    }

    if (t.row && t.row->type == RowType::SysDevice) {
        pages.push_back(new CInfoPage(L"Устройство", deviceRows(t.row->device)));
        inf::DriverPackage p;
        if (!t.row->device.infName.empty() && inf::driverPackage(t.row->device.infName, p))
            pages.push_back(new CInfoPage(L"Пакет драйвера", packageRows(p)));
    } else if (t.row && t.row->type == RowType::Package) {
        pages.push_back(new CInfoPage(L"Пакет драйвера", packageRows(t.row->package)));
    } else if (t.row && f) {
        const ResultRow &r = *t.row;
        const int idx = r.type == RowType::Problem ? r.logicalLine : locateLine(*ctx.analysis, r.logicalLine, r.lineText);
        switch (r.type) {
        case RowType::Problem:
            pages.push_back(new CProblemPage(ctx, r));
            break;
        case RowType::Device:
            if (idx >= 0) {
                pages.push_back(new CDevicePage(ctx, idx));
                pages.push_back(new CEntryPage(ctx, idx));
            }
            break;
        case RowType::String:
            if (idx >= 0)
                pages.push_back(new CStringPage(ctx, idx));
            break;
        case RowType::Entry:
        case RowType::CopiedFile:
            if (idx >= 0)
                pages.push_back(new CEntryPage(ctx, idx));
            break;
        case RowType::SysDevice:
        case RowType::Package:
            break;
        }
    } else if (t.node) {
        switch (t.node->type) {
        case NodeType::Root:
            pages.push_back(new CSettingsPage(owner.settings()));
            break;
        case NodeType::File:
            pages.push_back(new CFilePage(ctx, f->path(), f->format(), fileStats(*f)));
            pages.push_back(new CVersionPage(ctx));
            break;
        case NodeType::Version:
            pages.push_back(new CVersionPage(ctx));
            break;
        case NodeType::Section:
        case NodeType::StringSection:
            pages.push_back(new CSectionPage(ctx, t.node->key));
            break;
        case NodeType::Manufacturer: {
            const int idx = manufacturerLine(*ctx.analysis, t.node->key);
            if (idx >= 0)
                pages.push_back(new CEntryPage(ctx, idx));
            break;
        }
        default:
            break;
        }
    }
    if (pages.empty())
        return S_FALSE;
    bool first = true;
    for (CMmcPage *p : pages) {
        HPROPSHEETPAGE h = p->create(handle, first);
        first = false;
        if (h)
            provider->AddPage(h);
        else
            delete p;
    }
    return S_OK;
}
