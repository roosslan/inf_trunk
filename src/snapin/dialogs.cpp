#include "stdafx.h"

#include "dialogs.h"

#include "infgenerator.h"
#include "resource.h"

#include <afxcmn.h>

using inf::StringList;
using std::wstring;

// ------------------------------------------------------------------ вспомогательные функции

wstring dlgText(CWnd *wnd, int id)
{
    CString s;
    wnd->GetDlgItemText(id, s);
    return wstring(s.GetString());
}

void setDlgText(CWnd *wnd, int id, const wstring &text)
{
    wnd->SetDlgItemText(id, text.c_str());
}

wstring comboText(CComboBox &combo)
{
    CString s;
    combo.GetWindowText(s);
    return wstring(s.GetString());
}

namespace {

// ------------------------------------------------------------------ добавить запись

class CAddEntryDlg : public CDialog {
public:
    CAddEntryDlg(CWnd *parent, const wstring &section) : CDialog(IDD_ADD_ENTRY, parent), m_section(section) {}

    wstring key, value;

protected:
    BOOL OnInitDialog() override
    {
        CDialog::OnInitDialog();
        setDlgText(this, IDC_SECTION, L"[" + m_section + L"]");
        return TRUE;
    }

    void OnOK() override
    {
        key = inf::trim(dlgText(this, IDC_KEY));
        value = inf::trim(dlgText(this, IDC_VALUE));
        if (key.empty() && value.empty()) {
            AfxMessageBox(L"Необходимо указать ключ или значение.", MB_ICONWARNING);
            return;
        }
        CDialog::OnOK();
    }

private:
    wstring m_section;
};

// ------------------------------------------------------------------ добавить секцию

class CAddSectionDlg : public CDialog {
public:
    CAddSectionDlg(CWnd *parent, const StringList &suggestions) : CDialog(IDD_ADD_SECTION, parent), m_suggestions(suggestions) {}

    wstring name, content;

protected:
    BOOL OnInitDialog() override
    {
        CDialog::OnInitDialog();
        auto *combo = static_cast<CComboBox *>(GetDlgItem(IDC_NAME));
        for (const wstring &s : m_suggestions)
            combo->AddString(s.c_str());
        return TRUE;
    }

    void OnOK() override
    {
        name = inf::trim(comboText(*static_cast<CComboBox *>(GetDlgItem(IDC_NAME))));
        if (name.size() > 2 && name.front() == L'[' && name.back() == L']')
            name = inf::trim(name.substr(1, name.size() - 2));
        if (name.empty() || name.find_first_of(L"[];\"") != wstring::npos) {
            AfxMessageBox(L"Недопустимое имя секции.", MB_ICONWARNING);
            return;
        }
        content = dlgText(this, IDC_CONTENT);
        CDialog::OnOK();
    }

private:
    StringList m_suggestions;
};

// ------------------------------------------------------------------ добавить устройство

class CAddDeviceDlg : public CDialog {
public:
    CAddDeviceDlg(CWnd *parent, const inf::Analysis &a) : CDialog(IDD_ADD_DEVICE, parent), m_a(a) {}

    NewDevice result;

protected:
    BOOL OnInitDialog() override
    {
        CDialog::OnInitDialog();
        m_models.SubclassDlgItem(IDC_MODELS_LIST, this);
        auto *install = static_cast<CComboBox *>(GetDlgItem(IDC_INSTALL));
        StringList installs;
        for (const inf::DeviceModel &m : m_a.model.models())
            if (!m.installBase.empty() && !inf::icontains(installs, m.installBase))
                installs.push_back(m.installBase);
        if (installs.empty())
            installs.push_back(L"Device_Install");
        for (const wstring &s : installs)
            install->AddString(s.c_str());
        install->SetCurSel(0);
        for (const wstring &s : m_a.model.modelSections()) {
            if (!m_a.doc.hasSection(s))
                continue;
            const int i = m_models.AddString(s.c_str());
            m_models.SetCheck(i, BST_CHECKED);
        }
        if (m_models.GetCount() == 0)
            setDlgText(this, IDC_HINT,
                       L"В файле нет секций моделей. Сначала необходимо добавить [Manufacturer] и секцию моделей.");
        return TRUE;
    }

    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override
    {
        // ключ строки подбирается по описанию, пока его не правили вручную
        if (LOWORD(wParam) == IDC_DESC && HIWORD(wParam) == EN_CHANGE && !m_keyEdited) {
            const wstring base = inf::toIdentifier(dlgText(this, IDC_DESC), L"Device");
            wstring key = base + L".DeviceDesc";
            int n = 2;
            while (m_a.model.hasString(key))
                key = base + std::to_wstring(n++) + L".DeviceDesc";
            m_settingKey = true;
            setDlgText(this, IDC_KEY, key);
            m_settingKey = false;
        }
        if (LOWORD(wParam) == IDC_KEY && HIWORD(wParam) == EN_CHANGE && !m_settingKey)
            m_keyEdited = true;
        return CDialog::OnCommand(wParam, lParam);
    }

    void OnOK() override
    {
        result.description = inf::trim(dlgText(this, IDC_DESC));
        result.stringKey = inf::trim(dlgText(this, IDC_KEY));
        result.hardwareId = inf::trim(dlgText(this, IDC_HWID));
        result.compatible = inf::trim(dlgText(this, IDC_COMPAT));
        result.install = inf::trim(comboText(*static_cast<CComboBox *>(GetDlgItem(IDC_INSTALL))));
        result.modelSections.clear();
        for (int i = 0; i < m_models.GetCount(); ++i) {
            if (m_models.GetCheck(i) != BST_CHECKED)
                continue;
            CString s;
            m_models.GetText(i, s);
            result.modelSections.push_back(s.GetString());
        }
        if (result.description.empty() || result.hardwareId.empty() || result.install.empty()) {
            AfxMessageBox(L"Необходимо указать описание, ID оборудования и секцию установки.", MB_ICONWARNING);
            return;
        }
        if (result.modelSections.empty()) {
            AfxMessageBox(L"Необходимо выбрать хотя бы одну секцию моделей.", MB_ICONWARNING);
            return;
        }
        CDialog::OnOK();
    }

private:
    const inf::Analysis &m_a;
    CCheckListBox m_models;
    bool m_keyEdited = false;
    bool m_settingKey = false;
};

// ------------------------------------------------------------------ добавить строку

class CAddStringDlg : public CDialog {
public:
    CAddStringDlg(CWnd *parent, const StringList &sections, const wstring &section)
        : CDialog(IDD_ADD_STRING, parent), m_sections(sections), section(section)
    {
    }

    StringList m_sections;
    wstring section, key, value;

protected:
    BOOL OnInitDialog() override
    {
        CDialog::OnInitDialog();
        auto *combo = static_cast<CComboBox *>(GetDlgItem(IDC_SECTION));
        for (const wstring &s : m_sections) {
            const int i = combo->AddString(s.c_str());
            if (inf::iequals(s, section))
                combo->SetCurSel(i);
        }
        if (combo->GetCurSel() < 0)
            combo->SetCurSel(0);
        return TRUE;
    }

    void OnOK() override
    {
        section = comboText(*static_cast<CComboBox *>(GetDlgItem(IDC_SECTION)));
        key = inf::trim(dlgText(this, IDC_KEY));
        value = dlgText(this, IDC_VALUE);
        if (key.empty() || key.find_first_of(L"%=,;\" ") != wstring::npos) {
            AfxMessageBox(L"Ключ строки не должен быть пустым и не должен содержать пробелы и символы % = , ; \"", MB_ICONWARNING);
            return;
        }
        CDialog::OnOK();
    }
};

// ------------------------------------------------------------------ добавить файл

class CAddFileDlg : public CDialog {
public:
    CAddFileDlg(CWnd *parent, const inf::Analysis &a) : CDialog(IDD_ADD_FILE, parent), m_a(a) {}

    NewFile result;

protected:
    BOOL OnInitDialog() override
    {
        CDialog::OnInitDialog();
        auto *combo = static_cast<CComboBox *>(GetDlgItem(IDC_LIST));
        StringList lists;
        for (const inf::InfSection &s : m_a.doc.sections())
            if (m_a.model.role(s.name) == inf::SectionRole::CopyFilesList)
                lists.push_back(s.name);
        for (const wstring &s : lists)
            combo->AddString(s.c_str());
        if (lists.empty())
            combo->SetWindowText(L"Driver_CopyFiles");
        else
            combo->SetCurSel(0);
        wstring disk = L"1";
        if (!m_a.model.sourceDisks().empty())
            disk = *m_a.model.sourceDisks().begin();
        setDlgText(this, IDC_DISK, disk);
        return TRUE;
    }

    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override
    {
        if (LOWORD(wParam) == IDC_BROWSE && HIWORD(wParam) == BN_CLICKED) {
            CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST, L"Файлы драйвера (*.sys;*.dll;*.cat;*.exe)|*.sys;*.dll;*.cat;*.exe|Все файлы (*.*)|*.*||", this);
            if (dlg.DoModal() == IDOK)
                SetDlgItemText(IDC_NAME, dlg.GetFileName());
            return TRUE;
        }
        return CDialog::OnCommand(wParam, lParam);
    }

    void OnOK() override
    {
        result.name = inf::trim(dlgText(this, IDC_NAME));
        result.list = inf::trim(comboText(*static_cast<CComboBox *>(GetDlgItem(IDC_LIST))));
        result.disk = inf::trim(dlgText(this, IDC_DISK));
        result.subdir = inf::trim(dlgText(this, IDC_SUBDIR));
        if (result.name.empty() || result.list.empty() || result.disk.empty()) {
            AfxMessageBox(L"Необходимо указать имя файла, секцию копирования и диск.", MB_ICONWARNING);
            return;
        }
        if (result.name.find_first_of(L"\\/,;=\"") != wstring::npos) {
            AfxMessageBox(L"Имя файла указывается без пути.", MB_ICONWARNING);
            return;
        }
        CDialog::OnOK();
    }

private:
    const inf::Analysis &m_a;
};

// ------------------------------------------------------------------ ввод строки и вывод текста

class CInputDlg : public CDialog {
public:
    CInputDlg(CWnd *parent, const wstring &title, const wstring &prompt, const wstring &value)
        : CDialog(IDD_INPUT, parent), m_title(title), m_prompt(prompt), value(value)
    {
    }

    wstring m_title, m_prompt, value;

protected:
    BOOL OnInitDialog() override
    {
        CDialog::OnInitDialog();
        SetWindowText(m_title.c_str());
        setDlgText(this, IDC_PROMPT, m_prompt);
        setDlgText(this, IDC_VALUE, value);
        return TRUE;
    }

    void OnOK() override
    {
        value = dlgText(this, IDC_VALUE);
        CDialog::OnOK();
    }
};

class COutputDlg : public CDialog {
public:
    COutputDlg(CWnd *parent, const wstring &title, const wstring &text) : CDialog(IDD_OUTPUT, parent), m_title(title), m_text(text) {}

protected:
    BOOL OnInitDialog() override
    {
        CDialog::OnInitDialog();
        SetWindowText(m_title.c_str());
        setDlgText(this, IDC_OUTPUT, m_text);
        return TRUE;
    }

    afx_msg void OnSize(UINT type, int cx, int cy)
    {
        CDialog::OnSize(type, cx, cy);
        CWnd *out = GetDlgItem(IDC_OUTPUT);
        CWnd *ok = GetDlgItem(IDOK);
        if (!out || !ok)
            return;
        CRect r;
        ok->GetWindowRect(&r);
        const int m = 10;
        ok->SetWindowPos(nullptr, cx - r.Width() - m, cy - r.Height() - m, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        out->SetWindowPos(nullptr, m, m, cx - 2 * m, cy - r.Height() - 3 * m, SWP_NOZORDER);
    }

    DECLARE_MESSAGE_MAP()

private:
    wstring m_title, m_text;
};

BEGIN_MESSAGE_MAP(COutputDlg, CDialog)
    ON_WM_SIZE()
END_MESSAGE_MAP()

// ------------------------------------------------------------------ экспорт драйвера

class CExportDlg : public CDialog {
public:
    CExportDlg(CWnd *parent, const ExportOptions &o) : CDialog(IDD_EXPORT, parent), options(o) {}

    ExportOptions options;

protected:
    BOOL OnInitDialog() override
    {
        CDialog::OnInitDialog();
        SetWindowText(options.title.c_str());
        setDlgText(this, IDC_STATS, options.info);
        setDlgText(this, IDC_PROMPT, options.directory ? L"Директория для архивов:" : L"Файл архива:");
        setDlgText(this, IDC_PATH, options.path);
        CheckRadioButton(IDC_KIND0, IDC_KIND1, options.flatten ? IDC_KIND0 : IDC_KIND1);
        updateNote();
        return TRUE;
    }

    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override
    {
        const int id = LOWORD(wParam);
        if (HIWORD(wParam) == BN_CLICKED && (id == IDC_KIND0 || id == IDC_KIND1))
            updateNote();
        if (HIWORD(wParam) == BN_CLICKED && id == IDC_BROWSE) {
            const wstring current = dlgText(this, IDC_PATH);
            if (options.directory) {
                CFolderPickerDialog dlg(current.c_str(), 0, this);
                if (dlg.DoModal() == IDOK)
                    SetDlgItemText(IDC_PATH, dlg.GetPathName());
            } else {
                CFileDialog dlg(FALSE, L"cab", current.c_str(), OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST,
                                L"CAB-архивы (*.cab)|*.cab|Все файлы (*.*)|*.*||", this);
                if (dlg.DoModal() == IDOK)
                    SetDlgItemText(IDC_PATH, dlg.GetPathName());
            }
            return TRUE;
        }
        return CDialog::OnCommand(wParam, lParam);
    }

    void OnOK() override
    {
        options.path = inf::trim(dlgText(this, IDC_PATH));
        options.flatten = IsDlgButtonChecked(IDC_KIND0) == BST_CHECKED;
        if (options.path.empty()) {
            AfxMessageBox(L"Необходимо указать путь.", MB_ICONWARNING);
            return;
        }
        if (!options.directory && !inf::iendsWith(options.path, L".cab"))
            options.path += L".cab";
        CDialog::OnOK();
    }

private:
    void updateNote()
    {
        const bool flatten = IsDlgButtonChecked(IDC_KIND0) == BST_CHECKED;
        setDlgText(this, IDC_NOTE,
                   flatten ? L"В архив попадают INF, все файлы из [SourceDisksFiles] и каталог подписи — в корень. В INF очищаются "
                             L"пути [SourceDisksNames] и поддиректории [SourceDisksFiles]. Каталог .cat содержит хеш "
                             L"исходного INF, поэтому для изменённого INF подпись недействительна: перед установкой пакет "
                             L"необходимо переподписать (inf2cat и signtool) или включить тестовый режим."
                           : L"Архив повторяет директорию пакета в хранилище драйверов, INF не изменяется, подпись "
                             L"остаётся действительной. Для установки архив распаковывается с сохранением поддиректорий.");
    }
};

} // namespace

bool runExportDialog(CWnd *parent, ExportOptions &options)
{
    CExportDlg dlg(parent, options);
    if (dlg.DoModal() != IDOK)
        return false;
    options = dlg.options;
    return true;
}

namespace {

} // namespace

// ------------------------------------------------------------------ точки входа

bool runAddEntryDialog(CWnd *parent, const wstring &section, wstring &key, wstring &value)
{
    CAddEntryDlg dlg(parent, section);
    if (dlg.DoModal() != IDOK)
        return false;
    key = dlg.key;
    value = dlg.value;
    return true;
}

bool runAddSectionDialog(CWnd *parent, const StringList &suggestions, wstring &name, wstring &content)
{
    CAddSectionDlg dlg(parent, suggestions);
    if (dlg.DoModal() != IDOK)
        return false;
    name = dlg.name;
    content = dlg.content;
    return true;
}

bool runAddDeviceDialog(CWnd *parent, const inf::Analysis &a, NewDevice &out)
{
    CAddDeviceDlg dlg(parent, a);
    if (dlg.DoModal() != IDOK)
        return false;
    out = dlg.result;
    return true;
}

bool runAddStringDialog(CWnd *parent, const StringList &sections, wstring &section, wstring &key, wstring &value)
{
    CAddStringDlg dlg(parent, sections, section);
    if (dlg.DoModal() != IDOK)
        return false;
    section = dlg.section;
    key = dlg.key;
    value = dlg.value;
    return true;
}

bool runAddFileDialog(CWnd *parent, const inf::Analysis &a, NewFile &out)
{
    CAddFileDlg dlg(parent, a);
    if (dlg.DoModal() != IDOK)
        return false;
    out = dlg.result;
    return true;
}

bool runInputDialog(CWnd *parent, const wstring &title, const wstring &prompt, wstring &value)
{
    CInputDlg dlg(parent, title, prompt, value);
    if (dlg.DoModal() != IDOK)
        return false;
    value = dlg.value;
    return true;
}

void runOutputDialog(CWnd *parent, const wstring &title, const wstring &text)
{
    COutputDlg dlg(parent, title, text);
    dlg.DoModal();
}
