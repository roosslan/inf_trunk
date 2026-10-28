#include "stdafx.h"

#include "wizard.h"

#include "dialogs.h"
#include "images.h"
#include "infanalysis.h"
#include "infgenerator.h"
#include "infknowledge.h"
#include "resource.h"

#include <format>

using inf::DriverKind;
using inf::StringList;
using std::wstring;

namespace {

struct WizardData {
    inf::GeneratorParams p;
    wstring text;
};

// ------------------------------------------------------------------ базовая страница

class CWizPage : public CPropertyPage {
public:
    CWizPage(UINT id, WizardData &data, const wchar_t *title, const wchar_t *subtitle) : CPropertyPage(id), m_data(data)
    {
        m_psp.dwFlags |= PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
        m_psp.pszHeaderTitle = title;
        m_psp.pszHeaderSubTitle = subtitle;
    }

protected:
    virtual void load() {}            // данные -> элементы при каждом показе страницы
    virtual bool store() { return true; }  // элементы -> данные при уходе со страницы

    BOOL OnSetActive() override
    {
        auto *sheet = static_cast<CPropertySheet *>(GetParent());
        const int index = sheet->GetActiveIndex();
        const int count = sheet->GetPageCount();
        DWORD buttons = 0;
        if (index > 0)
            buttons |= PSWIZB_BACK;
        buttons |= index == count - 1 ? PSWIZB_FINISH : PSWIZB_NEXT;
        sheet->SetWizardButtons(buttons);
        load();
        return CPropertyPage::OnSetActive();
    }

    LRESULT OnWizardNext() override
    {
        if (!store())
            return -1;
        return CPropertyPage::OnWizardNext();
    }

    LRESULT OnWizardBack() override
    {
        store();
        return CPropertyPage::OnWizardBack();
    }

    CComboBox &combo(int id) { return *static_cast<CComboBox *>(GetDlgItem(id)); }

    WizardData &m_data;
};

// ------------------------------------------------------------------ 1. Тип пакета

class CKindPage : public CWizPage {
public:
    explicit CKindPage(WizardData &d)
        : CWizPage(IDD_WIZ_KIND, d, L"Тип пакета драйвера", L"От типа зависят создаваемые секции INF.")
    {
    }

protected:
    BOOL OnInitDialog() override
    {
        CWizPage::OnInitDialog();
        for (int i = 0; i < inf::kDriverKindCount; ++i)
            setDlgText(this, IDC_KIND0 + i, inf::driverKindName(DriverKind(i)));
        CheckRadioButton(IDC_KIND0, IDC_KIND6, IDC_KIND0 + int(m_data.p.kind));
        setDlgText(this, IDC_INFNAME, m_data.p.infName);
        CheckDlgButton(IDC_COMMENTS, m_data.p.comments ? BST_CHECKED : BST_UNCHECKED);
        updateHelp();
        return TRUE;
    }

    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override
    {
        const int id = LOWORD(wParam);
        if (id >= IDC_KIND0 && id <= IDC_KIND6 && HIWORD(wParam) == BN_CLICKED)
            updateHelp();
        return CWizPage::OnCommand(wParam, lParam);
    }

    bool store() override
    {
        const int checked = GetCheckedRadioButton(IDC_KIND0, IDC_KIND6);
        m_data.p.kind = DriverKind(checked - IDC_KIND0);
        m_data.p.infName = inf::trim(dlgText(this, IDC_INFNAME));
        m_data.p.comments = IsDlgButtonChecked(IDC_COMMENTS) == BST_CHECKED;
        if (!inf::iendsWith(m_data.p.infName, L".inf") || m_data.p.infName.size() < 5
            || m_data.p.infName.find_first_of(L"\\/:*?\"<>|") != wstring::npos) {
            AfxMessageBox(L"Имя файла должно иметь расширение .inf и не содержать пути.", MB_ICONWARNING);
            return false;
        }
        return true;
    }

private:
    void updateHelp()
    {
        const int checked = GetCheckedRadioButton(IDC_KIND0, IDC_KIND6);
        if (checked >= IDC_KIND0)
            setDlgText(this, IDC_KIND_HELP, inf::driverKindHelp(DriverKind(checked - IDC_KIND0)));
    }
};

// ------------------------------------------------------------------ 2. Общие сведения

class CGeneralPage : public CWizPage {
public:
    explicit CGeneralPage(WizardData &d)
        : CWizPage(IDD_WIZ_GENERAL, d, L"Общие сведения", L"Поставщик, производитель, класс устройства и версия драйвера ([Version]).")
    {
    }

protected:
    BOOL OnInitDialog() override
    {
        CWizPage::OnInitDialog();
        m_date.SubclassDlgItem(IDC_DATE, this);
        m_date.SetFormat(L"MM/dd/yyyy");
        for (const inf::DeviceClassInfo &c : inf::deviceClasses())
            combo(IDC_CLASS).AddString(c.name.c_str());
        setDlgText(this, IDC_PROVIDER, m_data.p.provider);
        setDlgText(this, IDC_MANUFACTURER, m_data.p.manufacturer);
        setDlgText(this, IDC_VERSION, m_data.p.version);
        CheckDlgButton(IDC_LOCKDOWN, m_data.p.pnpLockdown ? BST_CHECKED : BST_UNCHECKED);
        // GUID нестандартного класса из начальных данных (например, класс устройства компьютера)
        if (!m_data.p.classGuid.empty())
            setDlgText(this, IDC_CLASSGUID, m_data.p.classGuid);
        SYSTEMTIME st{};
        GetLocalTime(&st);
        m_date.SetTime(&st);
        return TRUE;
    }

    void load() override
    {
        // класс зависит от типа пакета
        wstring cls = m_data.p.className.empty() ? L"System" : m_data.p.className;
        if (m_data.p.kind == DriverKind::Extension)
            cls = L"Extension";
        else if (m_data.p.kind == DriverKind::WinUsb && (cls == L"System" || cls == L"Extension"))
            cls = L"USBDevice";
        else if (cls == L"Extension")
            cls = L"System";
        combo(IDC_CLASS).SetWindowText(cls.c_str());
        combo(IDC_CLASS).EnableWindow(m_data.p.kind != DriverKind::Extension);
        classChanged();
    }

    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override
    {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id == IDC_CLASS && code == CBN_SELCHANGE) {
            CString s;
            combo(IDC_CLASS).GetLBText(combo(IDC_CLASS).GetCurSel(), s);
            combo(IDC_CLASS).SetWindowText(s);
            classChanged();
        } else if (id == IDC_CLASS && code == CBN_EDITCHANGE) {
            classChanged();
        } else if (id == IDC_NEW_GUID && code == BN_CLICKED) {
            setDlgText(this, IDC_CLASSGUID, inf::newGuid());
        }
        return CWizPage::OnCommand(wParam, lParam);
    }

    bool store() override
    {
        m_data.p.provider = inf::trim(dlgText(this, IDC_PROVIDER));
        m_data.p.manufacturer = inf::trim(dlgText(this, IDC_MANUFACTURER));
        m_data.p.className = inf::trim(comboText(combo(IDC_CLASS)));
        m_data.p.classGuid = inf::trim(dlgText(this, IDC_CLASSGUID));
        const bool custom = !inf::findClassByName(m_data.p.className);
        m_data.p.customClass = custom && IsDlgButtonChecked(IDC_CUSTOM_CLASS) == BST_CHECKED;
        m_data.p.customClassDescription = inf::trim(dlgText(this, IDC_CLASS_DESC));
        SYSTEMTIME st{};
        m_date.GetTime(&st);
        m_data.p.date = inf::Date{st.wYear, st.wMonth, st.wDay};
        m_data.p.version = inf::trim(dlgText(this, IDC_VERSION));
        m_data.p.catalogFile = inf::trim(dlgText(this, IDC_CATALOG));
        m_data.p.pnpLockdown = IsDlgButtonChecked(IDC_LOCKDOWN) == BST_CHECKED;
        if (m_data.p.manufacturer.empty())
            m_data.p.manufacturer = m_data.p.provider;
        if (m_data.p.provider.empty()) {
            AfxMessageBox(L"Необходимо указать поставщика.", MB_ICONWARNING);
            return false;
        }
        if (m_data.p.className.empty() || !inf::isGuid(m_data.p.classGuid)) {
            AfxMessageBox(L"Необходимо указать класс и ClassGuid в формате {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}.", MB_ICONWARNING);
            return false;
        }
        return true;
    }

private:
    void classChanged()
    {
        const wstring name = inf::trim(comboText(combo(IDC_CLASS)));
        const inf::DeviceClassInfo *c = inf::findClassByName(name);
        if (c) {
            setDlgText(this, IDC_CLASSGUID, c->guid);
            setDlgText(this, IDC_CLASS_INFO, c->description);
            CheckDlgButton(IDC_CUSTOM_CLASS, BST_UNCHECKED);
        } else {
            const wstring g = dlgText(this, IDC_CLASSGUID);
            if (!inf::isGuid(g) || inf::findClassByGuid(g))
                setDlgText(this, IDC_CLASSGUID, inf::newGuid());
            setDlgText(this, IDC_CLASS_INFO, L"Нестандартный класс");
            CheckDlgButton(IDC_CUSTOM_CLASS, BST_CHECKED);
        }
        static_cast<CEdit *>(GetDlgItem(IDC_CLASSGUID))->SetReadOnly(c != nullptr);
        GetDlgItem(IDC_NEW_GUID)->EnableWindow(c == nullptr);
        GetDlgItem(IDC_CUSTOM_CLASS)->EnableWindow(c == nullptr);
        GetDlgItem(IDC_CLASS_DESC)->EnableWindow(c == nullptr);
    }

    CDateTimeCtrl m_date;
};

// ------------------------------------------------------------------ 3. Платформы

class CPlatformPage : public CWizPage {
public:
    explicit CPlatformPage(WizardData &d)
        : CWizPage(IDD_WIZ_PLATFORM, d, L"Платформы", L"Архитектуры, для которых создаются секции моделей, и минимальная версия Windows.")
    {
    }

protected:
    BOOL OnInitDialog() override
    {
        CWizPage::OnInitDialog();
        CheckDlgButton(IDC_ARCH_AMD64, BST_CHECKED);
        CheckDlgButton(IDC_ARCH_ARM64, BST_CHECKED);
        static const wchar_t *items[] = {L"(без ограничения)", L"10.0...16299 — Windows 10 1709", L"10.0...17763 — Windows 10 1809",
                                         L"10.0...19041 — Windows 10 2004", L"10.0...22000 — Windows 11"};
        for (const wchar_t *s : items)
            combo(IDC_OSVER).AddString(s);
        combo(IDC_OSVER).SetCurSel(1);
        return TRUE;
    }

    bool store() override
    {
        m_data.p.architectures.clear();
        if (IsDlgButtonChecked(IDC_ARCH_AMD64) == BST_CHECKED)
            m_data.p.architectures.push_back(L"amd64");
        if (IsDlgButtonChecked(IDC_ARCH_ARM64) == BST_CHECKED)
            m_data.p.architectures.push_back(L"arm64");
        if (IsDlgButtonChecked(IDC_ARCH_X86) == BST_CHECKED)
            m_data.p.architectures.push_back(L"x86");
        wstring os = inf::trim(comboText(combo(IDC_OSVER)));
        if (os.empty() || os.front() == L'(')
            os.clear();
        else
            os = inf::trim(os.substr(0, os.find(L' ')));
        m_data.p.targetOsVersion = os;
        if (m_data.p.architectures.empty()) {
            AfxMessageBox(L"Необходимо выбрать хотя бы одну архитектуру.", MB_ICONWARNING);
            return false;
        }
        return true;
    }
};

// ------------------------------------------------------------------ 4. Устройства

class CDeviceRowDlg : public CDialog {
public:
    CDeviceRowDlg(CWnd *parent, const inf::GeneratorDevice &d) : CDialog(IDD_DEVICE_ROW, parent), device(d) {}

    inf::GeneratorDevice device;

protected:
    BOOL OnInitDialog() override
    {
        CDialog::OnInitDialog();
        setDlgText(this, IDC_DESC, device.description);
        setDlgText(this, IDC_HWID, device.hardwareId);
        setDlgText(this, IDC_COMPAT, inf::join(device.compatibleIds, L", "));
        return TRUE;
    }

    void OnOK() override
    {
        device.description = inf::trim(dlgText(this, IDC_DESC));
        device.hardwareId = inf::trim(dlgText(this, IDC_HWID));
        device.compatibleIds.clear();
        for (const wstring &c : inf::split(dlgText(this, IDC_COMPAT), L',', true))
            if (!inf::trim(c).empty())
                device.compatibleIds.push_back(inf::trim(c));
        if (device.description.empty() || device.hardwareId.empty()) {
            AfxMessageBox(L"Необходимо указать описание и ID оборудования.", MB_ICONWARNING);
            return;
        }
        CDialog::OnOK();
    }
};

class CDevicesPage : public CWizPage {
public:
    explicit CDevicesPage(WizardData &d)
        : CWizPage(IDD_WIZ_DEVICES, d, L"Устройства", L"Модели устройств: описание и идентификаторы оборудования.")
    {
    }

protected:
    BOOL OnInitDialog() override
    {
        CWizPage::OnInitDialog();
        m_list.SubclassDlgItem(IDC_DEVICES, this);
        m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        m_list.InsertColumn(0, L"Описание", LVCFMT_LEFT, 130);
        m_list.InsertColumn(1, L"ID оборудования", LVCFMT_LEFT, 160);
        m_list.InsertColumn(2, L"Совместимые ID", LVCFMT_LEFT, 100);
        if (m_data.p.devices.empty())
            m_data.p.devices.push_back({L"My Device", L"Root\\MyDevice", {}});
        return TRUE;
    }

    void load() override
    {
        if (m_data.p.kind == DriverKind::WinUsb && m_data.p.devices.size() == 1 && m_data.p.devices[0].hardwareId == L"Root\\MyDevice")
            m_data.p.devices[0].hardwareId = L"USB\\VID_1234&PID_5678";
        refresh();
    }

    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override
    {
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam)) {
            case IDC_ADD: {
                CDeviceRowDlg dlg(this, {});
                if (dlg.DoModal() == IDOK) {
                    m_data.p.devices.push_back(dlg.device);
                    refresh();
                }
                return TRUE;
            }
            case IDC_EDIT:
                editSelected();
                return TRUE;
            case IDC_REMOVE: {
                const int i = m_list.GetNextItem(-1, LVNI_SELECTED);
                if (i >= 0 && i < int(m_data.p.devices.size())) {
                    m_data.p.devices.erase(m_data.p.devices.begin() + i);
                    refresh();
                }
                return TRUE;
            }
            }
        }
        return CWizPage::OnCommand(wParam, lParam);
    }

    BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT *result) override
    {
        const auto *hdr = reinterpret_cast<NMHDR *>(lParam);
        if (hdr->idFrom == IDC_DEVICES && hdr->code == NM_DBLCLK) {
            editSelected();
            return TRUE;
        }
        return CWizPage::OnNotify(wParam, lParam, result);
    }

    bool store() override
    {
        if (m_data.p.kind != DriverKind::NonPnp && m_data.p.devices.empty()) {
            AfxMessageBox(L"Необходимо добавить хотя бы одно устройство.", MB_ICONWARNING);
            return false;
        }
        return true;
    }

private:
    void editSelected()
    {
        const int i = m_list.GetNextItem(-1, LVNI_SELECTED);
        if (i < 0 || i >= int(m_data.p.devices.size()))
            return;
        CDeviceRowDlg dlg(this, m_data.p.devices[size_t(i)]);
        if (dlg.DoModal() == IDOK) {
            m_data.p.devices[size_t(i)] = dlg.device;
            refresh();
        }
    }

    void refresh()
    {
        m_list.DeleteAllItems();
        for (int i = 0; i < int(m_data.p.devices.size()); ++i) {
            const auto &d = m_data.p.devices[size_t(i)];
            m_list.InsertItem(i, d.description.c_str());
            m_list.SetItemText(i, 1, d.hardwareId.c_str());
            m_list.SetItemText(i, 2, inf::join(d.compatibleIds, L", ").c_str());
        }
        if (!m_data.p.devices.empty())
            m_list.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }

    CListCtrl m_list;
};

// ------------------------------------------------------------------ 5. Служба и файлы

class CFilesPage : public CWizPage {
public:
    explicit CFilesPage(WizardData &d)
        : CWizPage(IDD_WIZ_FILES, d, L"Служба и файлы", L"Параметры службы драйвера и копируемые файлы.")
    {
    }

protected:
    BOOL OnInitDialog() override
    {
        CWizPage::OnInitDialog();
        setDlgText(this, IDC_SERVICE, m_data.p.serviceName);
        setDlgText(this, IDC_BINARY, m_data.p.binaryName);
        for (int i = 0; i <= 4; ++i)
            combo(IDC_STARTTYPE).AddString(std::format(L"{} — {}", i, inf::explainValue(L"StartType", std::to_wstring(i))).c_str());
        combo(IDC_STARTTYPE).SetCurSel(m_data.p.startType);
        for (int i = 0; i <= 3; ++i)
            combo(IDC_ERRORCONTROL).AddString(std::format(L"{} — {}", i, inf::explainValue(L"ErrorControl", std::to_wstring(i))).c_str());
        combo(IDC_ERRORCONTROL).SetCurSel(m_data.p.errorControl);
        combo(IDC_DESTDIR).AddString(L"13 — Driver Store");
        combo(IDC_DESTDIR).AddString(L"12 — System32\\drivers");
        combo(IDC_DESTDIR).AddString(L"11 — System32");
        combo(IDC_DESTDIR).SetCurSel(0);
        setDlgText(this, IDC_KMDF, m_data.p.kmdfVersion);
        setDlgText(this, IDC_UMDF, m_data.p.umdfVersion);
        setDlgText(this, IDC_IFACE_GUID, inf::newGuid());
        return TRUE;
    }

    void load() override
    {
        const DriverKind k = m_data.p.kind;
        const bool service = k == DriverKind::Kmdf || k == DriverKind::Umdf || k == DriverKind::Wdm || k == DriverKind::NonPnp;
        for (int id : {IDC_SERVICE, IDC_SERVICE_DESC, IDC_BINARY, IDC_STARTTYPE, IDC_ERRORCONTROL, IDC_DESTDIR})
            GetDlgItem(id)->EnableWindow(service);
        GetDlgItem(IDC_KMDF)->EnableWindow(k == DriverKind::Kmdf);
        GetDlgItem(IDC_UMDF)->EnableWindow(k == DriverKind::Umdf);
        GetDlgItem(IDC_IFACE_GUID)->EnableWindow(k == DriverKind::WinUsb);
        GetDlgItem(IDC_NEW_IFACE)->EnableWindow(k == DriverKind::WinUsb);
        wstring bin = dlgText(this, IDC_BINARY);
        if (k == DriverKind::Umdf && inf::iendsWith(bin, L".sys"))
            bin = bin.substr(0, bin.size() - 4) + L".dll";
        else if (k != DriverKind::Umdf && inf::iendsWith(bin, L".dll"))
            bin = bin.substr(0, bin.size() - 4) + L".sys";
        setDlgText(this, IDC_BINARY, bin);
        wstring hint;
        if (k == DriverKind::WinUsb)
            hint = L"Служба WinUSB подключается из системного winusb.inf; GUID интерфейса нужен приложениям для открытия устройства.";
        else if (k == DriverKind::NullDriver || k == DriverKind::Extension)
            hint = L"Для этого типа пакета служба не устанавливается; при необходимости указываются только дополнительные файлы.";
        setDlgText(this, IDC_HINT, hint);
    }

    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override
    {
        if (LOWORD(wParam) == IDC_NEW_IFACE && HIWORD(wParam) == BN_CLICKED)
            setDlgText(this, IDC_IFACE_GUID, inf::newGuid());
        return CWizPage::OnCommand(wParam, lParam);
    }

    bool store() override
    {
        m_data.p.serviceName = inf::trim(dlgText(this, IDC_SERVICE));
        m_data.p.serviceDescription = inf::trim(dlgText(this, IDC_SERVICE_DESC));
        m_data.p.binaryName = inf::trim(dlgText(this, IDC_BINARY));
        m_data.p.startType = combo(IDC_STARTTYPE).GetCurSel();
        m_data.p.errorControl = combo(IDC_ERRORCONTROL).GetCurSel();
        static const int dirs[] = {13, 12, 11};
        m_data.p.destinationDirId = dirs[std::max(0, combo(IDC_DESTDIR).GetCurSel())];
        m_data.p.extraFiles.clear();
        for (const wstring &f : inf::split(inf::replaceAll(dlgText(this, IDC_EXTRA), L"\r\n", L"\n"), L'\n', true))
            if (!inf::trim(f).empty())
                m_data.p.extraFiles.push_back(inf::trim(f));
        m_data.p.kmdfVersion = inf::trim(dlgText(this, IDC_KMDF));
        m_data.p.umdfVersion = inf::trim(dlgText(this, IDC_UMDF));
        m_data.p.deviceInterfaceGuid = inf::trim(dlgText(this, IDC_IFACE_GUID));
        const DriverKind k = m_data.p.kind;
        const bool service = k == DriverKind::Kmdf || k == DriverKind::Umdf || k == DriverKind::Wdm || k == DriverKind::NonPnp;
        if (service && (m_data.p.serviceName.empty() || m_data.p.binaryName.empty())) {
            AfxMessageBox(L"Необходимо указать имя службы и файл драйвера.", MB_ICONWARNING);
            return false;
        }
        if (k == DriverKind::WinUsb && !inf::isGuid(m_data.p.deviceInterfaceGuid)) {
            AfxMessageBox(L"GUID интерфейса должен иметь вид {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}.", MB_ICONWARNING);
            return false;
        }
        return true;
    }
};

// ------------------------------------------------------------------ 6. Просмотр

class CPreviewPage : public CWizPage {
public:
    explicit CPreviewPage(WizardData &d)
        : CWizPage(IDD_WIZ_PREVIEW, d, L"Просмотр", L"Созданный INF откроется в оснастке, где его можно доработать.")
    {
    }

protected:
    BOOL OnInitDialog() override
    {
        CWizPage::OnInitDialog();
        return TRUE;
    }

    void load() override
    {
        m_data.text = inf::generateInf(m_data.p);
        setDlgText(this, IDC_PREVIEW, inf::replaceAll(m_data.text, L"\n", L"\r\n"));
        const auto a = inf::Analysis::run(m_data.text);
        int errors = 0, warnings = 0;
        for (const inf::Diagnostic &d : a->diagnostics) {
            if (d.severity == inf::Severity::Error)
                ++errors;
            else if (d.severity == inf::Severity::Warning)
                ++warnings;
        }
        setDlgText(this, IDC_STATUS, std::format(L"Проверка: ошибок — {}, предупреждений — {}", errors, warnings));
    }

    BOOL OnWizardFinish() override
    {
        if (m_data.text.empty())
            m_data.text = inf::generateInf(m_data.p);
        return CWizPage::OnWizardFinish();
    }
};

HBITMAP headerBitmap()
{
    static HBITMAP bmp = iconBitmap(ImgApp, 48, GetSysColor(COLOR_WINDOW));
    return bmp;
}

} // namespace

bool runNewInfWizard(CWnd *parent, wstring &text, wstring &fileName, const inf::GeneratorParams *initial)
{
    WizardData data;
    if (initial)
        data.p = *initial;
    CKindPage kind(data);
    CGeneralPage general(data);
    CPlatformPage platform(data);
    CDevicesPage devices(data);
    CFilesPage files(data);
    CPreviewPage preview(data);

    CPropertySheet sheet(L"Мастер создания INF", parent);
    sheet.AddPage(&kind);
    sheet.AddPage(&general);
    sheet.AddPage(&platform);
    sheet.AddPage(&devices);
    sheet.AddPage(&files);
    sheet.AddPage(&preview);
    sheet.SetWizardMode();
    sheet.m_psh.dwFlags |= PSH_WIZARD97 | PSH_HEADER | PSH_USEHBMHEADER;
    sheet.m_psh.dwFlags &= ~PSH_HASHELP;
    sheet.m_psh.dwFlags |= PSH_NOCONTEXTHELP;
    sheet.m_psh.hbmHeader = headerBitmap();
    if (sheet.DoModal() != ID_WIZFINISH)
        return false;
    text = data.text;
    fileName = data.p.infName;
    return true;
}
