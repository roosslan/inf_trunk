#include "infgenerator.h"

#include "infknowledge.h"
#include "inflexer.h"

#include <algorithm>

namespace inf {

wstring driverKindName(DriverKind kind)
{
    switch (kind) {
    case DriverKind::Kmdf: return L"Драйвер KMDF";
    case DriverKind::Umdf: return L"Драйвер UMDF 2";
    case DriverKind::Wdm: return L"Драйвер WDM";
    case DriverKind::WinUsb: return L"Устройство USB с драйвером WinUSB";
    case DriverKind::NullDriver: return L"Устройство без драйвера";
    case DriverKind::Extension: return L"Extension INF";
    case DriverKind::NonPnp: return L"Служба без Plug and Play (DefaultInstall)";
    }
    return {};
}

wstring driverKindHelp(DriverKind kind)
{
    switch (kind) {
    case DriverKind::Kmdf: return L"Драйвер режима ядра на Kernel-Mode Driver Framework: секции DDInstall, .Services и .Wdf.";
    case DriverKind::Umdf: return L"Драйвер пользовательского режима UMDF 2: служба WUDFRd и секция .Wdf с UmdfService.";
    case DriverKind::Wdm: return L"Классический драйвер WDM без WDF: DDInstall и .Services.";
    case DriverKind::WinUsb: return L"USB-устройство с системным драйвером WinUSB.sys и GUID интерфейса устройства для приложений.";
    case DriverKind::NullDriver: return L"Устройство без функционального драйвера: убирает «неизвестное устройство» из диспетчера.";
    case DriverKind::Extension: return L"Extension INF: дополняет основной драйвер (параметры реестра) без замены его INF.";
    case DriverKind::NonPnp: return L"Служба или драйвер без Plug and Play: установка через [DefaultInstall] правым щелчком.";
    }
    return {};
}

wstring toIdentifier(const wstring &text, const wstring &fallback)
{
    wstring out;
    for (wchar_t c : text) {
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_')
            out += c;
        else if (isSpace(c) || c == L'-' || c == L'.')
            out += L'_';
    }
    while (contains(out, L"__"))
        out = replaceAll(out, L"__", L"_");
    if (!out.empty() && out.front() == L'_')
        out.erase(0, 1);
    if (!out.empty() && out.back() == L'_')
        out.pop_back();
    if (out.empty())
        return fallback;
    if (out.front() >= L'0' && out.front() <= L'9')
        out.insert(out.begin(), L'_');
    return out;
}

namespace {

struct Entry {
    wstring key;
    wstring value;
    wstring comment;
};

wstring padRight(const wstring &s, size_t width)
{
    return s.size() >= width ? s : s + wstring(width - s.size(), L' ');
}

class Writer {
public:
    explicit Writer(bool comments) : m_comments(comments) {}

    void blank() { m_out.push_back(wstring()); }
    void raw(const wstring &line) { m_out.push_back(line); }

    void header(const wstring &name, const wstring &note = wstring())
    {
        if (!m_out.empty() && !m_out.back().empty())
            blank();
        if (m_comments && !note.empty())
            m_out.push_back(L"[" + name + L"] ; " + note);
        else
            m_out.push_back(L"[" + name + L"]");
    }

    void section(const wstring &name, const std::vector<Entry> &entries, const wstring &note = wstring())
    {
        header(name, note);
        size_t width = 0;
        for (const Entry &e : entries)
            if (!e.key.empty())
                width = std::max(width, e.key.size());
        size_t valueWidth = 0;
        for (const Entry &e : entries)
            if (!e.comment.empty())
                valueWidth = std::max(valueWidth, e.value.size());
        for (const Entry &e : entries) {
            wstring line = e.key.empty() ? e.value : padRight(e.key, width) + L" = " + e.value;
            if (m_comments && !e.comment.empty()) {
                const size_t pad = e.key.empty() ? valueWidth : width + 3 + valueWidth;
                line = padRight(line, pad) + L"  ; " + e.comment;
            }
            m_out.push_back(line);
        }
    }

    wstring text() const { return join(m_out, L"\n") + L"\n"; }

private:
    bool m_comments;
    StringList m_out;
};

wstring startTypeComment(int t)
{
    switch (t) {
    case 0: return L"SERVICE_BOOT_START";
    case 1: return L"SERVICE_SYSTEM_START";
    case 2: return L"SERVICE_AUTO_START";
    case 3: return L"SERVICE_DEMAND_START";
    case 4: return L"SERVICE_DISABLED";
    }
    return {};
}

wstring errorControlComment(int t)
{
    switch (t) {
    case 0: return L"SERVICE_ERROR_IGNORE";
    case 1: return L"SERVICE_ERROR_NORMAL";
    case 2: return L"SERVICE_ERROR_SEVERE";
    case 3: return L"SERVICE_ERROR_CRITICAL";
    }
    return {};
}

wstring dirPath(int dirId, const wstring &file)
{
    return L"%" + std::to_wstring(dirId) + L"%\\" + file;
}

} // namespace

wstring generateInf(const GeneratorParams &p)
{
    Writer w(p.comments);
    const wstring svc = toIdentifier(p.serviceName, L"MyDriver");
    const wstring install = svc + L"_Device";
    const wstring infBase = baseName(p.infName);
    const wstring catalog = p.catalogFile.empty() ? infBase + L".cat" : p.catalogFile;
    const bool pnp = p.kind != DriverKind::NonPnp;
    const bool hasService = p.kind == DriverKind::Kmdf || p.kind == DriverKind::Umdf || p.kind == DriverKind::Wdm
                            || p.kind == DriverKind::NonPnp;
    const bool copiesFiles = hasService || !p.extraFiles.empty();

    StringList files;
    if (hasService && !p.binaryName.empty())
        files.push_back(p.binaryName);
    for (const wstring &f : p.extraFiles)
        if (!trim(f).empty() && !icontains(files, trim(f)))
            files.push_back(trim(f));

    std::vector<Entry> strings;
    auto addString = [&](const wstring &key, const wstring &value) {
        strings.push_back(Entry{key, quote(value), wstring()});
        return L"%" + key + L"%";
    };

    // ---------------------------------------------------------------- заголовок
    w.raw(L";");
    w.raw(L"; " + p.infName);
    w.raw(L";");
    w.raw(L"; " + driverKindName(p.kind));
    w.raw(L";");

    // ---------------------------------------------------------------- [Version]
    wstring className = p.className;
    wstring classGuid = p.classGuid;
    if (p.kind == DriverKind::Extension) {
        className = L"Extension";
        classGuid = findClassByName(className)->guid;
    } else if (p.kind == DriverKind::WinUsb && className.empty()) {
        className = L"USBDevice";
        classGuid = findClassByName(className)->guid;
    }
    if (classGuid.empty())
        if (const DeviceClassInfo *c = findClassByName(className))
            classGuid = c->guid;

    std::vector<Entry> version;
    version.push_back({L"Signature", L"\"$WINDOWS NT$\"", L""});
    if (!className.empty())
        version.push_back({L"Class", className, L""});
    if (!classGuid.empty())
        version.push_back({L"ClassGuid", classGuid, L""});
    if (p.kind == DriverKind::Extension)
        version.push_back({L"ExtensionId", p.extensionId.empty() ? newGuid() : p.extensionId, L"уникален для каждого extension INF"});
    version.push_back({L"Provider", L"%ProviderName%", L""});
    version.push_back({L"CatalogFile", catalog, L""});
    version.push_back({L"DriverVer", p.date.toInf() + L"," + p.version, L""});
    if (p.pnpLockdown)
        version.push_back({L"PnpLockdown", L"1", L""});
    w.blank();
    w.section(L"Version", version);
    strings.insert(strings.begin(), Entry{L"ProviderName", quote(p.provider.empty() ? L"Provider" : p.provider), L""});

    // ---------------------------------------------------------------- файлы
    if (copiesFiles) {
        w.section(L"DestinationDirs", {{L"DefaultDestDir", std::to_wstring(p.destinationDirId), dirIdDescription(p.destinationDirId)}});
        const wstring disk = addString(L"DiskName", (p.manufacturer.empty() ? p.provider : p.manufacturer) + L" Installation Disk");
        w.section(L"SourceDisksNames", {{L"1", disk + L",,,\"\"", L""}});
        std::vector<Entry> sdf;
        for (const wstring &f : files)
            sdf.push_back({f, L"1,,", L""});
        w.section(L"SourceDisksFiles", sdf);
    }

    // ---------------------------------------------------------------- класс
    if (p.customClass && pnp && p.kind != DriverKind::Extension) {
        w.section(L"ClassInstall32", {{L"AddReg", L"ClassInstall_AddReg", L""}}, L"установка нового класса устройств");
        const wstring clsDesc = addString(L"ClassName", p.customClassDescription.empty() ? className : p.customClassDescription);
        w.section(L"ClassInstall_AddReg", {{L"", L"HKR,,,0," + clsDesc, L""}, {L"", L"HKR,,Icon,,-5", L""}});
    }

    // ---------------------------------------------------------------- без PnP
    if (!pnp) {
        const wstring svcDesc = addString(svc + L".SvcDesc", p.serviceDescription.empty() ? p.serviceName : p.serviceDescription);
        for (const wstring &arch : p.architectures) {
            const wstring di = L"DefaultInstall.NT" + arch;
            w.section(di, {{L"CopyFiles", svc + L"_Files", L""}});
            w.section(di + L".Services", {{L"AddService", svc + L",," + svc + L"_Service_Inst", L""}});
        }
        std::vector<Entry> lst;
        for (const wstring &f : files)
            lst.push_back({L"", f, L""});
        w.section(svc + L"_Files", lst);
        w.section(svc + L"_Service_Inst",
                  {{L"DisplayName", svcDesc, L""},
                   {L"ServiceType", L"1", L"SERVICE_KERNEL_DRIVER"},
                   {L"StartType", std::to_wstring(p.startType), startTypeComment(p.startType)},
                   {L"ErrorControl", std::to_wstring(p.errorControl), errorControlComment(p.errorControl)},
                   {L"ServiceBinary", dirPath(p.destinationDirId, p.binaryName), L""}});
        w.blank();
        w.section(L"Strings", strings);
        return w.text();
    }

    // ---------------------------------------------------------------- производитель и модели
    strings.push_back({L"ManufacturerName", quote(p.manufacturer.empty() ? L"Manufacturer" : p.manufacturer), L""});
    // DirID 13 поддерживается начиная с Windows 10 1709 (сборка 16299)
    wstring osVersion = p.targetOsVersion;
    if (osVersion.empty() && copiesFiles && p.destinationDirId == 13)
        osVersion = L"10.0...16299";
    StringList decorations;
    for (const wstring &arch : p.architectures)
        decorations.push_back(L"NT" + arch + (osVersion.empty() ? wstring() : L"." + osVersion));
    w.section(L"Manufacturer", {{L"%ManufacturerName%", L"Standard," + join(decorations, L","), L""}});

    std::vector<Entry> models;
    int n = 1;
    std::vector<GeneratorDevice> devices = p.devices;
    if (devices.empty())
        devices.push_back({L"Device", L"Root\\" + svc, {}});
    for (const GeneratorDevice &d : devices) {
        const wstring key = L"Device" + std::to_wstring(n++) + L".DeviceDesc";
        StringList ids = {trim(d.hardwareId)};
        for (const wstring &c : d.compatibleIds)
            if (!trim(c).empty())
                ids.push_back(trim(c));
        models.push_back({L"%" + key + L"%", install + L", " + join(ids, L", "), L""});
        strings.push_back({key, quote(d.description.empty() ? L"Device" : d.description), L""});
    }
    for (const wstring &dec : decorations)
        w.section(L"Standard." + dec, models);

    // ---------------------------------------------------------------- DDInstall
    const wstring ddi = install + L".NT";
    std::vector<Entry> ddiEntries;
    if (p.kind == DriverKind::WinUsb) {
        ddiEntries.push_back({L"Include", L"winusb.inf", L""});
        ddiEntries.push_back({L"Needs", L"WINUSB.NT", L""});
    }
    if (copiesFiles)
        ddiEntries.push_back({L"CopyFiles", install + L"_CopyFiles", L""});
    if (p.kind == DriverKind::Extension)
        ddiEntries.push_back({L"AddReg", install + L"_AddReg", L""});
    w.section(ddi, ddiEntries);

    if (copiesFiles) {
        std::vector<Entry> lst;
        for (const wstring &f : files)
            lst.push_back({L"", f, L""});
        w.section(install + L"_CopyFiles", lst);
    }

    if (p.kind == DriverKind::Extension)
        w.section(install + L"_AddReg", {{L"", L"HKR,,\"MyValue\",0x00010001,1", L"REG_DWORD"}});

    if (p.kind == DriverKind::WinUsb) {
        const wstring guid = p.deviceInterfaceGuid.empty() ? newGuid() : p.deviceInterfaceGuid;
        w.section(ddi + L".HW", {{L"AddReg", install + L"_AddReg", L""}});
        w.section(install + L"_AddReg", {{L"", L"HKR,,DeviceInterfaceGUIDs,0x10000,\"" + guid + L"\"", L"REG_MULTI_SZ"}});
        // служба WinUSB устанавливается секциями системного winusb.inf
        w.section(ddi + L".Services", {{L"Include", L"winusb.inf", L""}, {L"Needs", L"WINUSB.NT.Services", L""}});
    } else if (p.kind == DriverKind::NullDriver) {
        w.section(ddi + L".Services", {{L"AddService", L",0x00000002", L"устройство без драйвера"}});
    } else if (p.kind == DriverKind::Kmdf || p.kind == DriverKind::Wdm) {
        const wstring svcDesc = addString(svc + L".SvcDesc", p.serviceDescription.empty() ? p.serviceName : p.serviceDescription);
        w.section(ddi + L".Services", {{L"AddService", svc + L",0x00000002," + svc + L"_Service_Inst", L"SPSVCINST_ASSOCSERVICE"}});
        w.section(svc + L"_Service_Inst",
                  {{L"DisplayName", svcDesc, L""},
                   {L"ServiceType", L"1", L"SERVICE_KERNEL_DRIVER"},
                   {L"StartType", std::to_wstring(p.startType), startTypeComment(p.startType)},
                   {L"ErrorControl", std::to_wstring(p.errorControl), errorControlComment(p.errorControl)},
                   {L"ServiceBinary", dirPath(p.destinationDirId, p.binaryName), L""}});
        if (p.kind == DriverKind::Kmdf) {
            w.section(ddi + L".Wdf", {{L"KmdfService", svc + L", " + svc + L"_wdfsect", L""}});
            w.section(svc + L"_wdfsect", {{L"KmdfLibraryVersion", p.kmdfVersion, L""}});
        }
    } else if (p.kind == DriverKind::Umdf) {
        const wstring rdDesc = addString(L"WudfRd.SvcDesc", L"Windows Driver Foundation - User-mode Driver Framework Reflector");
        w.section(ddi + L".Services", {{L"AddService", L"WUDFRd,0x000001fa,WUDFRD_ServiceInstall", L""}});
        w.section(L"WUDFRD_ServiceInstall", {{L"DisplayName", rdDesc, L""},
                                             {L"ServiceType", L"1", L"SERVICE_KERNEL_DRIVER"},
                                             {L"StartType", L"3", L"SERVICE_DEMAND_START"},
                                             {L"ErrorControl", L"1", L"SERVICE_ERROR_NORMAL"},
                                             {L"ServiceBinary", L"%12%\\WUDFRd.sys", L""}});
        w.section(ddi + L".Wdf", {{L"UmdfService", svc + L", " + svc + L"_Install", L""}, {L"UmdfServiceOrder", svc, L""}});
        w.section(svc + L"_Install", {{L"UmdfLibraryVersion", p.umdfVersion, L""},
                                      {L"ServiceBinary", dirPath(p.destinationDirId, p.binaryName), L""}});
    }

    w.blank();
    w.section(L"Strings", strings);
    return w.text();
}

} // namespace inf
