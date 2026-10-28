#include "drvstore.h"

#include "cabwriter.h"
#include "infanalysis.h"
#include "infedit.h"

#include <windows.h>
#include <initguid.h>
#include <devpkey.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#include <algorithm>
#include <format>
#include <map>

namespace inf {

namespace {

bool fileExists(const wstring &path)
{
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

wstring dirOf(const wstring &path)
{
    const size_t s = path.find_last_of(L"\\/");
    return s == wstring::npos ? wstring() : path.substr(0, s);
}

// Склейка частей пути с удалением пустых частей и лишних разделителей
wstring joinPath(const StringList &parts)
{
    StringList clean;
    for (const wstring &p : parts) {
        for (const wstring &piece : split(replaceAll(p, L"/", L"\\"), L'\\', true)) {
            const wstring t = trim(piece);
            if (t.empty() || t == L".")
                continue;
            clean.push_back(t);
        }
    }
    // первая часть может быть буквой диска или началом UNC-пути
    wstring out = join(clean, L"\\");
    if (!parts.empty() && parts[0].rfind(L"\\\\", 0) == 0)
        out = L"\\\\" + out;
    return out;
}

// Все файлы директории (рекурсивно), пути относительно base
void listFiles(const wstring &dir, const wstring &relative, std::vector<std::pair<wstring, wstring>> &out)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        const wstring name = fd.cFileName;
        if (name == L"." || name == L"..")
            continue;
        const wstring full = dir + L"\\" + name;
        const wstring rel = relative.empty() ? name : relative + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            listFiles(full, rel, out);
        else
            out.push_back({full, rel});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

bool sameContent(const wstring &a, const wstring &b)
{
    Bytes x, y;
    return readBytes(a, x) && readBytes(b, y) && x == y;
}

// ------------------------------------------------------------------ свойства устройства

bool rawProperty(HDEVINFO set, SP_DEVINFO_DATA &d, const DEVPROPKEY &key, DEVPROPTYPE &type, std::vector<BYTE> &buf)
{
    DWORD size = 0;
    type = DEVPROP_TYPE_EMPTY;
    SetupDiGetDevicePropertyW(set, &d, &key, &type, nullptr, 0, &size, 0);
    if (size == 0)
        return false;
    buf.assign(size, 0);
    return SetupDiGetDevicePropertyW(set, &d, &key, &type, buf.data(), size, &size, 0) != FALSE;
}

wstring stringProperty(HDEVINFO set, SP_DEVINFO_DATA &d, const DEVPROPKEY &key)
{
    DEVPROPTYPE type;
    std::vector<BYTE> buf;
    if (!rawProperty(set, d, key, type, buf) || type != DEVPROP_TYPE_STRING)
        return {};
    return wstring(reinterpret_cast<const wchar_t *>(buf.data()));
}

StringList listProperty(HDEVINFO set, SP_DEVINFO_DATA &d, const DEVPROPKEY &key)
{
    StringList out;
    DEVPROPTYPE type;
    std::vector<BYTE> buf;
    if (!rawProperty(set, d, key, type, buf) || type != DEVPROP_TYPE_STRING_LIST)
        return out;
    buf.push_back(0);
    buf.push_back(0);
    for (const wchar_t *p = reinterpret_cast<const wchar_t *>(buf.data()); *p; p += wcslen(p) + 1)
        out.push_back(p);
    return out;
}

} // namespace

bool isThirdPartyInf(const wstring &infName)
{
    const wstring n = lower(fileName(infName));
    // oem<цифры>.inf
    if (n.size() < 8 || n.compare(0, 3, L"oem") != 0 || !iendsWith(n, L".inf"))
        return false;
    for (size_t i = 3; i < n.size() - 4; ++i)
        if (n[i] < L'0' || n[i] > L'9')
            return false;
    return true;
}

bool DeviceInfo::thirdParty() const
{
    return isThirdPartyInf(infName);
}

wstring windowsInfDir()
{
    wchar_t buf[MAX_PATH];
    GetWindowsDirectoryW(buf, MAX_PATH);
    return wstring(buf) + L"\\INF\\";
}

std::vector<DeviceInfo> enumerateDevices(bool includeHidden, wstring *error)
{
    std::vector<DeviceInfo> out;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | (includeHidden ? 0 : DIGCF_PRESENT));
    if (set == INVALID_HANDLE_VALUE) {
        if (error)
            *error = lastErrorText(GetLastError());
        return out;
    }
    SP_DEVINFO_DATA d{};
    d.cbSize = sizeof(d);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &d); ++i) {
        DeviceInfo info;
        info.instanceId = stringProperty(set, d, DEVPKEY_Device_InstanceId);
        info.description = stringProperty(set, d, DEVPKEY_Device_FriendlyName);
        if (info.description.empty())
            info.description = stringProperty(set, d, DEVPKEY_Device_DeviceDesc);
        info.className = stringProperty(set, d, DEVPKEY_Device_Class);
        info.hardwareIds = listProperty(set, d, DEVPKEY_Device_HardwareIds);
        info.compatibleIds = listProperty(set, d, DEVPKEY_Device_CompatibleIds);
        info.manufacturer = stringProperty(set, d, DEVPKEY_Device_Manufacturer);
        info.driverProvider = stringProperty(set, d, DEVPKEY_Device_DriverProvider);
        info.driverVersion = stringProperty(set, d, DEVPKEY_Device_DriverVersion);
        info.infName = stringProperty(set, d, DEVPKEY_Device_DriverInfPath);
        info.infSection = stringProperty(set, d, DEVPKEY_Device_DriverInfSection);
        info.matchingId = stringProperty(set, d, DEVPKEY_Device_MatchingDeviceId);

        DEVPROPTYPE type;
        std::vector<BYTE> buf;
        if (rawProperty(set, d, DEVPKEY_Device_ClassGuid, type, buf) && type == DEVPROP_TYPE_GUID) {
            const GUID g = *reinterpret_cast<const GUID *>(buf.data());
            wchar_t s[64];
            StringFromGUID2(g, s, 64);
            info.classGuid = lower(s);
            wchar_t desc[256] = {};
            if (SetupDiGetClassDescriptionW(&g, desc, 256, nullptr))
                info.classDescription = desc;
        }
        if (rawProperty(set, d, DEVPKEY_Device_DriverDate, type, buf) && type == DEVPROP_TYPE_FILETIME) {
            SYSTEMTIME st{};
            if (FileTimeToSystemTime(reinterpret_cast<const FILETIME *>(buf.data()), &st))
                info.driverDate = Date{st.wYear, st.wMonth, st.wDay}.toInf();
        }
        if (rawProperty(set, d, DEVPKEY_Device_ProblemCode, type, buf) && type == DEVPROP_TYPE_UINT32)
            info.problem = *reinterpret_cast<const ULONG *>(buf.data());
        if (rawProperty(set, d, DEVPKEY_Device_DevNodeStatus, type, buf) && type == DEVPROP_TYPE_UINT32) {
            const ULONG status = *reinterpret_cast<const ULONG *>(buf.data());
            info.hasProblem = (status & DN_HAS_PROBLEM) != 0;
            info.hidden = (status & DN_NO_SHOW_IN_DM) != 0;
        }
        if (rawProperty(set, d, DEVPKEY_Device_IsPresent, type, buf) && type == DEVPROP_TYPE_BOOLEAN)
            info.present = *reinterpret_cast<const DEVPROP_BOOLEAN *>(buf.data()) != DEVPROP_FALSE;
        // у отключённого устройства нет узла: признак ошибки к нему не относится
        if (!info.present)
            info.hasProblem = false;
        if (!includeHidden && info.hidden)
            continue;
        if (info.classDescription.empty())
            info.classDescription = info.className.empty() ? L"Прочие устройства" : info.className;
        out.push_back(std::move(info));
    }
    SetupDiDestroyDeviceInfoList(set);
    std::sort(out.begin(), out.end(), [](const DeviceInfo &a, const DeviceInfo &b) {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, a.description.c_str(), -1, b.description.c_str(), -1,
                               nullptr, nullptr, 0) == CSTR_LESS_THAN;
    });
    return out;
}

bool driverPackage(const wstring &publishedName, DriverPackage &p, wstring *error)
{
    p = DriverPackage();
    p.publishedName = fileName(publishedName);
    p.publishedPath = publishedName.find_first_of(L"\\/") == wstring::npos ? windowsInfDir() + p.publishedName : publishedName;
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD req = 0;
    if (SetupGetInfDriverStoreLocationW(p.publishedPath.c_str(), nullptr, nullptr, buf, DWORD(std::size(buf)), &req)) {
        p.storeInf = buf;
        p.storeDir = dirOf(p.storeInf);
        p.originalName = fileName(p.storeInf);
    }
    const wstring infToRead = p.storeInf.empty() ? p.publishedPath : p.storeInf;
    DecodedFile f;
    if (!readFile(infToRead, f, error))
        return false;
    const auto a = Analysis::run(f.text);
    p.provider = trim(a->model.resolve(a->doc.value(L"Version", L"Provider")));
    p.className = trim(a->model.resolve(a->doc.value(L"Version", L"Class")));
    const InfLine *dv = a->doc.entry(L"Version", L"DriverVer");
    if (dv) {
        StringList parts;
        for (const InfValue &v : dv->values)
            parts.push_back(trim(a->model.resolve(v.text)));
        p.driverVer = join(parts, L", ");
    }
    if (const InfSection *ver = a->doc.section(L"Version"))
        for (const InfLine *l : a->doc.entries(*ver))
            if (l->hasKey && istartsWith(l->key.text, L"CatalogFile") && p.catalog.empty())
                p.catalog = trim(l->value(0));
    if (p.originalName.empty())
        p.originalName = p.publishedName;
    if (p.storeInf.empty() && error)
        *error = L"INF не найден в хранилище драйверов";
    return true;
}

std::vector<DriverPackage> enumerateDriverPackages(const std::vector<DeviceInfo> &devices, bool thirdPartyOnly)
{
    std::vector<DriverPackage> out;
    WIN32_FIND_DATAW fd;
    const wstring dir = windowsInfDir();
    HANDLE h = FindFirstFileW((dir + (thirdPartyOnly ? L"oem*.inf" : L"*.inf")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return out;
    do {
        if (thirdPartyOnly && !isThirdPartyInf(fd.cFileName))
            continue;
        DriverPackage p;
        if (!driverPackage(dir + fd.cFileName, p) || p.storeInf.empty())
            continue;
        for (const DeviceInfo &d : devices)
            if (iequals(d.infName, p.publishedName))
                ++p.devices;
        out.push_back(std::move(p));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const DriverPackage &a, const DriverPackage &b) {
        // по номеру oemNN, а не по строке: oem2 раньше oem10
        const long na = wcstol(a.publishedName.c_str() + 3, nullptr, 10), nb = wcstol(b.publishedName.c_str() + 3, nullptr, 10);
        return na != nb ? na < nb : lower(a.publishedName) < lower(b.publishedName);
    });
    return out;
}

// ------------------------------------------------------------------ экспорт

wstring flattenInfPaths(const wstring &text, int *changedLines)
{
    const InfDocument doc = InfDocument::parse(text);
    std::vector<TextEdit> edits;
    auto rewrite = [&](const wstring &sectionPrefix, size_t index) {
        for (const wstring &secName : doc.sectionsWithPrefix(sectionPrefix)) {
            for (int idx : doc.section(secName)->entries) {
                const InfLine &l = doc.lines()[size_t(idx)];
                if (!l.hasKey || l.values.size() <= index || trim(l.values[index].text).empty())
                    continue;
                StringList vals;
                for (size_t i = 0; i < l.values.size(); ++i)
                    vals.push_back(i == index ? (index == 3 ? wstring(L"\"\"") : wstring()) : trim(l.values[i].raw));
                // начало строки до первого значения сохраняется как есть («1=» или «1 = »)
                wstring prefix = trim(l.key.raw) + L" = ";
                const wstring first = doc.physicalLine(l.firstLine);
                if (l.values[0].line == l.firstLine) {
                    const size_t indent = first.find_first_not_of(L" \t");
                    prefix = first.substr(indent, size_t(l.values[0].column) - indent);
                }
                edits.push_back(replaceLineEdit(doc, idx, prefix + join(vals, L","), true));
            }
        }
    };
    // [SourceDisksNames]: id = описание,[файл-тега],[не используется],путь
    rewrite(L"SourceDisksNames", 3);
    // [SourceDisksFiles]: файл = id-диска,[поддиректория],[размер]
    rewrite(L"SourceDisksFiles", 1);
    if (changedLines)
        *changedLines = int(edits.size());
    return applyEdits(text, edits);
}

ExportPlan planExport(const wstring &infPath, bool flatten)
{
    ExportPlan plan;
    plan.infName = fileName(infPath);
    const wstring dir = dirOf(infPath);
    DecodedFile f;
    wstring error;
    if (!readFile(infPath, f, &error)) {
        plan.errors.push_back(L"Не удалось прочитать " + infPath + L": " + error);
        return plan;
    }
    plan.infText = f.text;
    plan.infFormat = f.format;

    std::vector<std::pair<wstring, wstring>> all;  // полный путь, относительный путь
    listFiles(dir, L"", all);

    if (!flatten) {
        // пакет целиком, как он лежит в хранилище
        for (const auto &file : all) {
            if (iequals(file.second, plan.infName) || iendsWith(file.second, L".pnf"))
                continue;
            plan.files.push_back({file.first, file.second});
        }
        return plan;
    }

    const auto a = Analysis::run(f.text);
    std::map<wstring, wstring> diskPath;  // id диска -> путь
    for (const wstring &sec : a->doc.sectionsWithPrefix(L"SourceDisksNames"))
        for (const InfLine *l : a->doc.entries(*a->doc.section(sec)))
            if (l->hasKey && !diskPath.count(lower(trim(l->key.text))))
                diskPath[lower(trim(l->key.text))] = trim(a->model.resolve(l->value(3)));

    auto findByName = [&](const wstring &name) -> wstring {
        for (const auto &file : all)
            if (iequals(fileName(file.second), name))
                return file.first;
        return {};
    };

    std::map<wstring, ExportFile> chosen;  // имя в нижнем регистре -> файл
    auto addFile = [&](const wstring &name, const wstring &source) {
        const wstring key = lower(name);
        auto it = chosen.find(key);
        if (it == chosen.end()) {
            chosen[key] = ExportFile{source, name};
            return;
        }
        if (iequals(it->second.source, source) || sameContent(it->second.source, source))
            return;
        plan.errors.push_back(std::format(L"Файл {} встречается в разных поддиректориях с разным содержимым ({} и {}); "
                                          L"уложить его в корень архива нельзя — пакет следует экспортировать без изменения путей.",
                                          name, it->second.source, source));
    };

    StringList missing;
    for (const wstring &sec : a->doc.sectionsWithPrefix(L"SourceDisksFiles")) {
        for (const InfLine *l : a->doc.entries(*a->doc.section(sec))) {
            if (!l->hasKey)
                continue;
            const wstring name = trim(l->key.text);
            const wstring disk = lower(trim(l->value(0)));
            const wstring subdir = trim(a->model.resolve(l->value(1)));
            wstring source = joinPath({dir, diskPath.count(disk) ? diskPath[disk] : wstring(), subdir, name});
            if (!fileExists(source))
                source = findByName(name);
            if (source.empty()) {
                if (!icontains(missing, name))
                    missing.push_back(name);
                continue;
            }
            addFile(name, source);
        }
    }
    // каталоги подписи
    if (const InfSection *ver = a->doc.section(L"Version")) {
        for (const InfLine *l : a->doc.entries(*ver)) {
            if (!l->hasKey || !istartsWith(l->key.text, L"CatalogFile"))
                continue;
            const wstring cat = trim(a->model.resolve(l->value(0)));
            const wstring source = fileExists(joinPath({dir, cat})) ? joinPath({dir, cat}) : findByName(cat);
            if (source.empty()) {
                if (!icontains(missing, cat))
                    missing.push_back(cat);
            } else {
                addFile(cat, source);
            }
        }
    }
    for (auto &c : chosen)
        plan.files.push_back(c.second);
    if (!missing.empty())
        plan.warnings.push_back(L"Файлов нет в пакете (обычно это файлы для других платформ: в хранилище драйверов "
                                L"Windows оставляет только нужные этой системе): " + join(missing, L", "));

    // файлы пакета, которые INF не упоминает
    StringList unused;
    for (const auto &file : all) {
        const wstring n = fileName(file.second);
        if (iequals(n, plan.infName) || iendsWith(n, L".pnf") || chosen.count(lower(n)))
            continue;
        unused.push_back(file.second);
    }
    if (!unused.empty())
        plan.warnings.push_back(L"В пакете есть файлы, не упомянутые в INF (не включены в архив): " + join(unused, L", "));

    int changed = 0;
    plan.infText = flattenInfPaths(f.text, &changed);
    plan.infChanged = changed > 0;
    if (plan.infChanged)
        plan.warnings.push_back(std::format(L"Пути в INF исправлены (строк: {}). Каталог подписи содержит хеш исходного INF, "
                                            L"поэтому для изменённого INF подпись недействительна: для установки пакет "
                                            L"необходимо переподписать (inf2cat и signtool) или использовать тестовый режим.",
                                            changed));
    return plan;
}

bool exportToCab(const ExportPlan &plan, const wstring &cabPath, wstring *error)
{
    if (!plan.errors.empty()) {
        if (error)
            *error = join(plan.errors, L"\n");
        return false;
    }
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const wstring dir = wstring(tmp) + L"inf_export_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    CreateDirectoryW(dir.c_str(), nullptr);
    const wstring infTemp = dir + L"\\" + plan.infName;
    bool ok = writeFile(infTemp, plan.infText, plan.infFormat, error);
    if (ok) {
        std::vector<CabEntry> entries = {{infTemp, plan.infName}};
        for (const ExportFile &f : plan.files)
            entries.push_back({f.source, f.nameInCab});
        ok = writeCab(cabPath, entries, error);
    }
    DeleteFileW(infTemp.c_str());
    RemoveDirectoryW(dir.c_str());
    return ok;
}

wstring suggestedCabName(const DriverPackage &p)
{
    wstring name = baseName(p.originalName.empty() ? p.publishedName : p.originalName);
    const StringList parts = split(p.driverVer, L',');
    if (parts.size() > 1 && !trim(parts[1]).empty())
        name += L"_" + trim(parts[1]);
    for (wchar_t &c : name)
        if (wcschr(L"\\/:*?\"<>| ", c))
            c = L'_';
    return name + L".cab";
}

} // namespace inf
