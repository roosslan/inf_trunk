#include "infvalidator.h"

#include "infknowledge.h"

#include <algorithm>
#include <format>
#include <set>
#include <unordered_map>

namespace inf {

wstring severityName(Severity s)
{
    switch (s) {
    case Severity::Error: return L"Ошибка";
    case Severity::Warning: return L"Предупреждение";
    case Severity::Info: return L"Замечание";
    }
    return {};
}

namespace {

class Checker {
public:
    Checker(const InfDocument &d, const InfModel &m) : doc(d), model(m) {}

    std::vector<Diagnostic> run()
    {
        checkSyntax();
        checkVersion();
        checkStrings();
        checkReferences();
        checkManufacturers();
        checkModels();
        checkInstallSections();
        checkServices();
        checkFiles();
        checkRegistry();
        std::stable_sort(out.begin(), out.end(), [](const Diagnostic &a, const Diagnostic &b) { return a.line < b.line; });
        return out;
    }

private:
    const InfDocument &doc;
    const InfModel &model;
    std::vector<Diagnostic> out;

    void add(Severity s, const wchar_t *code, int line, int col, int len, const wstring &msg)
    {
        Diagnostic d;
        d.severity = s;
        d.code = code;
        d.line = std::max(0, line);
        d.column = col;
        d.length = len;
        d.message = msg;
        out.push_back(d);
    }

    void addAt(Severity s, const wchar_t *code, const InfValue &v, const wstring &msg)
    {
        add(s, code, v.line, v.column, v.length, msg);
    }

    void addSection(Severity s, const wchar_t *code, const InfSection &sec, const wstring &msg)
    {
        const InfLine &h = doc.lines()[size_t(sec.chunks.front().logicalHeader)];
        addAt(s, code, h.sectionName, msg);
    }

    int lineLength(int line) const { return int(doc.physicalLine(line).size()); }

    bool isDeviceInf() const { return doc.hasSection(L"Manufacturer"); }
    wstring versionValue(const wstring &key) const { return doc.value(L"Version", key); }
    bool pnpLockdown() const { return trim(versionValue(L"PnpLockdown")) == L"1"; }
    wstring className() const { return trim(versionValue(L"Class")); }

    // ---------------------------------------------------------------- синтаксис
    void checkSyntax()
    {
        for (const InfLine &l : doc.lines())
            if (!l.error.empty())
                add(Severity::Error, L"S001", l.errorLine, l.errorColumn, l.errorLength, l.error);

        for (int idx : doc.entriesOutsideSections()) {
            const InfLine &l = doc.lines()[size_t(idx)];
            add(Severity::Error, L"S002", l.firstLine, 0, lineLength(l.firstLine),
                L"Запись вне секции: перед ней нет заголовка [секция]");
        }

        for (const InfSection &s : doc.sections()) {
            for (size_t i = 1; i < s.chunks.size(); ++i) {
                const InfLine &h = doc.lines()[size_t(s.chunks[i].logicalHeader)];
                addAt(Severity::Info, L"S003", h.sectionName,
                      std::format(L"Секция [{}] уже встречалась в строке {}; Windows объединит фрагменты", s.name,
                                  s.chunks.front().headerLine + 1));
            }
        }
    }

    // ---------------------------------------------------------------- [Version]
    void checkVersion()
    {
        const InfSection *ver = doc.section(L"Version");
        if (!ver) {
            add(Severity::Error, L"V001", 0, 0, 0, L"Нет обязательной секции [Version]");
            return;
        }

        const InfLine *sig = doc.entry(L"Version", L"Signature");
        if (!sig) {
            addSection(Severity::Error, L"V002", *ver, L"В [Version] нет директивы Signature");
        } else {
            const wstring v = lower(trim(sig->value(0)));
            if (v == L"$chicago$" || v == L"$windows 95$")
                addAt(Severity::Warning, L"V003", sig->valueAt(0),
                      std::format(L"Подпись {} устарела; для современных Windows используется \"$Windows NT$\"", sig->value(0)));
            else if (v != L"$windows nt$")
                addAt(Severity::Error, L"V004", sig->valueAt(0),
                      std::format(L"Недопустимое значение Signature: \"{}\"", sig->value(0)));
        }

        const InfLine *cls = doc.entry(L"Version", L"Class");
        const InfLine *guid = doc.entry(L"Version", L"ClassGuid");
        const Severity missingSev = isDeviceInf() ? Severity::Warning : Severity::Info;
        if (!cls)
            addSection(missingSev, L"V005", *ver, L"В [Version] нет директивы Class");
        if (!guid)
            addSection(missingSev, L"V006", *ver, L"В [Version] нет директивы ClassGuid");
        if (guid) {
            const wstring g = trim(model.resolve(guid->value(0)));
            if (!isGuid(g)) {
                addAt(Severity::Error, L"V007", guid->valueAt(0),
                      L"ClassGuid не является GUID в формате {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}");
            } else if (cls) {
                const wstring name = trim(model.resolve(cls->value(0)));
                const DeviceClassInfo *byName = findClassByName(name);
                const DeviceClassInfo *byGuid = findClassByGuid(g);
                if (byName && byName->guid != lower(g))
                    addAt(Severity::Error, L"V008", guid->valueAt(0),
                          std::format(L"Для класса {} стандартный ClassGuid — {}", byName->name, byName->guid));
                else if (!byName && byGuid)
                    addAt(Severity::Error, L"V009", cls->valueAt(0),
                          std::format(L"ClassGuid {} принадлежит стандартному классу {}", g, byGuid->name));
            }
        }

        if (iequals(className(), L"Extension") && !doc.entry(L"Version", L"ExtensionId"))
            addSection(Severity::Error, L"V010", *ver, L"Для Class=Extension обязательна директива ExtensionId");

        if (!doc.entry(L"Version", L"Provider"))
            addSection(Severity::Warning, L"V011", *ver, L"В [Version] нет директивы Provider");

        const InfLine *dv = doc.entry(L"Version", L"DriverVer");
        if (!dv)
            addSection(isDeviceInf() ? Severity::Warning : Severity::Info, L"V012", *ver, L"В [Version] нет директивы DriverVer");
        else
            checkDriverVer(*dv);

        bool hasCatalog = false;
        for (const InfLine *l : doc.entries(*ver))
            if (l->hasKey && istartsWith(l->key.text, L"CatalogFile"))
                hasCatalog = true;
        if (!hasCatalog && isDeviceInf() && versionValue(L"LayoutFile").empty())
            addSection(Severity::Warning, L"V013", *ver, L"Нет директивы CatalogFile: пакет драйвера нельзя будет подписать");

        const InfLine *pl = doc.entry(L"Version", L"PnpLockdown");
        if (!pl) {
            if (isDeviceInf())
                addSection(Severity::Info, L"V014", *ver, L"Рекомендуется добавить PnpLockdown=1");
        } else {
            const wstring v = trim(pl->value(0));
            if (v != L"0" && v != L"1")
                addAt(Severity::Warning, L"V015", pl->valueAt(0), L"PnpLockdown допускает только значения 0 и 1");
        }
    }

    void checkDriverVer(const InfLine &dv)
    {
        const wstring date = trim(model.resolve(dv.value(0)));
        const Date d = Date::fromInf(date);
        if (d.year == 0) {
            addAt(Severity::Error, L"V016", dv.valueAt(0), L"Дата DriverVer должна быть в формате мм/дд/гггг");
        } else if (!d.isValid()) {
            addAt(Severity::Error, L"V017", dv.valueAt(0),
                  std::format(L"Недопустимая дата DriverVer: {} (месяц/день/год)", date));
        } else if (d > Date::today().addDays(1)) {
            addAt(Severity::Warning, L"V018", dv.valueAt(0), L"Дата DriverVer в будущем");
        }
        if (dv.values.size() > 1) {
            const wstring ver = trim(model.resolve(dv.value(1)));
            const StringList parts = split(ver, L'.');
            bool ok = !parts.empty() && parts.size() <= 4;
            for (const wstring &p : parts) {
                uint32_t n = 0;
                if (!parseUInt(p, n) || n > 65535)
                    ok = false;
            }
            if (!ok)
                addAt(Severity::Error, L"V019", dv.values[1],
                      L"Версия DriverVer должна иметь вид w.x.y.z, каждое число от 0 до 65535");
        } else {
            addAt(Severity::Warning, L"V020", dv.valueAt(0), L"В DriverVer не указан номер версии");
        }
    }

    // ---------------------------------------------------------------- строки
    void checkStrings()
    {
        for (const InfLine &l : doc.lines()) {
            if (l.kind != InfLine::Kind::Entry)
                continue;
            if (l.section >= 0 && model.role(doc.sections()[size_t(l.section)].name) == SectionRole::Strings)
                continue;
            auto scan = [&](const InfValue &v) {
                for (const StringToken &t : InfModel::stringTokens(v.raw)) {
                    if (t.numeric || model.hasString(t.name))
                        continue;
                    // в INF без устройств (шаблоны безопасности и т. п.) %имя% бывает переменной окружения
                    add(isDeviceInf() ? Severity::Error : Severity::Warning, L"T001", v.line, v.column + t.start, t.length,
                        std::format(L"Строка %{}% не определена в [Strings]", t.name));
                }
            };
            if (l.hasKey)
                scan(l.key);
            for (const InfValue &v : l.values)
                scan(v);
        }

        for (const wstring &secName : model.stringSections()) {
            const InfSection *sec = doc.section(secName);
            std::unordered_map<wstring, int> seen;
            for (const InfLine *l : doc.entries(*sec)) {
                if (!l->hasKey) {
                    add(Severity::Error, L"T002", l->firstLine, 0, lineLength(l->firstLine),
                        L"В секции строк запись должна иметь вид ключ = \"значение\"");
                    continue;
                }
                const wstring k = lower(l->key.text);
                auto it = seen.find(k);
                if (it != seen.end())
                    addAt(Severity::Warning, L"T003", l->key,
                          std::format(L"Строка {} уже определена в строке {}", l->key.text, it->second + 1));
                else
                    seen.emplace(k, l->firstLine);
                if (l->values.size() > 1)
                    addAt(Severity::Warning, L"T004", l->values[1],
                          L"Значение строки содержит запятую — его следует заключить в кавычки");
            }
        }

        // неиспользуемые строки (только в базовой секции)
        const wstring base = model.baseStringSection();
        if (base.empty())
            return;
        for (const InfLine *l : doc.entries(*doc.section(base)))
            if (l->hasKey && model.stringUses(l->key.text) == 0)
                addAt(Severity::Info, L"T005", l->key, std::format(L"Строка {} нигде не используется", l->key.text));

        // локализованные секции, в которых не хватает строк
        std::set<wstring> baseKeys;
        for (const StringDef &d : model.strings(base))
            baseKeys.insert(lower(d.key));
        for (const wstring &sec : model.stringSections()) {
            if (iequals(sec, base))
                continue;
            std::set<wstring> keys;
            for (const StringDef &d : model.strings(sec))
                keys.insert(lower(d.key));
            int missing = 0;
            for (const wstring &k : baseKeys)
                if (!keys.count(k))
                    ++missing;
            if (missing > 0)
                addSection(Severity::Info, L"T006", *doc.section(sec),
                           std::format(L"В секции [{}] нет {} строк из [{}]; будут использованы значения из [{}]", sec,
                                       missing, base, base));
        }
    }

    // ---------------------------------------------------------------- ссылки
    // INF-файлы, подключённые директивами Include (их секции доступны через Needs)
    StringList includedFiles() const
    {
        StringList files;
        for (const InfLine &l : doc.lines())
            if (l.kind == InfLine::Kind::Entry && l.hasKey && iequals(trim(l.key.text), L"Include"))
                for (const InfValue &v : l.values)
                    if (!trim(v.text).empty() && !icontains(files, trim(v.text)))
                        files.push_back(trim(v.text));
        return files;
    }

    void checkReferences()
    {
        const StringList includes = includedFiles();
        for (const SectionRef &r : model.references()) {
            if (r.exists || r.implicit || r.line < 0)
                continue;
            if (includes.empty())
                add(Severity::Error, L"R001", r.line, r.column, r.length,
                    std::format(L"Секция [{}] ({}) не найдена", r.target, r.directive));
            else
                add(Severity::Info, L"R001", r.line, r.column, r.length,
                    std::format(L"Секции [{}] ({}) нет в файле; возможно, она находится во включённом INF: {}", r.target,
                                r.directive, join(includes, L", ")));
        }
        // в INF без [Manufacturer] (библиотеки секций, установка без PnP) секции используются извне
        const Severity unused = isDeviceInf() ? Severity::Warning : Severity::Info;
        for (const InfSection &s : doc.sections()) {
            if (model.role(s.name) != SectionRole::Unknown || model.incomingCount(s.name) > 0)
                continue;
            addSection(unused, L"R002", s, std::format(L"Секция [{}] нигде не используется", s.name));
        }
    }

    // ---------------------------------------------------------------- производители
    void checkManufacturers()
    {
        const InfSection *mfg = doc.section(L"Manufacturer");
        if (!mfg)
            return;
        if (model.manufacturers().empty())
            addSection(Severity::Warning, L"M001", *mfg, L"Секция [Manufacturer] пуста");

        // DirID 13 допустим только в пакетах для Windows 10 1709 (сборка 16299) и новее
        bool uses13 = false;
        for (const wstring &dd : doc.sectionsWithPrefix(L"DestinationDirs"))
            for (const InfLine *l : doc.entries(*doc.section(dd)))
                if (trim(l->value(0)) == L"13")
                    uses13 = true;
        for (const InfSection &s : doc.sections())
            if (model.role(s.name) == SectionRole::ServiceInstall && istartsWith(trim(doc.value(s.name, L"ServiceBinary")), L"%13%"))
                uses13 = true;

        for (const InfLine *l : doc.entries(*mfg)) {
            if (l->values.empty() || trim(l->value(0)).empty()) {
                add(Severity::Error, L"M002", l->firstLine, 0, lineLength(l->firstLine),
                    L"Запись производителя должна указывать секцию моделей: %Mfg% = Models[,NTamd64...]");
                continue;
            }
            bool has64 = false;
            for (size_t i = 1; i < l->values.size(); ++i) {
                const wstring d = trim(l->values[i].text);
                const wstring arch = InfModel::architectureOf(d);
                if (arch == L"amd64" || arch == L"arm64" || arch == L"ia64")
                    has64 = true;
                if (d.empty())
                    continue;
                if (!istartsWith(d, L"NT")) {
                    addAt(Severity::Warning, L"M003", l->values[i],
                          L"Декорация платформы должна начинаться с NT (NTx86, NTamd64, NTarm64...)");
                    continue;
                }
                if (!arch.empty() && !icontains(knownArchitectures(), arch))
                    addAt(Severity::Warning, L"M004", l->values[i], std::format(L"Неизвестная архитектура в декорации: {}", arch));
                if (uses13) {
                    // NTamd64.10.0...16299: старшая.младшая.тип.маска.сборка
                    const StringList parts = split(d, L'.');
                    const int major = parts.size() > 1 ? toInt(parts[1]) : 0;
                    const int build = parts.size() > 5 ? toInt(parts[5]) : 0;
                    if (major < 10 || (major == 10 && build < 16299))
                        addAt(Severity::Warning, L"M006", l->values[i],
                              std::format(L"DirID 13 поддерживается с Windows 10 1709: декорацию следует ограничить, например {}.10.0...16299",
                                          d.substr(0, d.find(L'.'))));
                }
            }
            if (!has64)
                addAt(Severity::Warning, L"M005", l->values[0],
                      L"Нет декорации для 64-разрядных систем (NTamd64 / NTarm64): на них устройство не установится");
        }
    }

    // ---------------------------------------------------------------- модели
    void checkModels()
    {
        std::unordered_map<wstring, std::unordered_map<wstring, int>> hwidsBySection;
        for (const DeviceModel &m : model.models()) {
            const int len = lineLength(m.line);
            if (m.description.empty())
                add(Severity::Error, L"D001", m.line, 0, len,
                    L"Запись модели должна иметь описание: %DeviceDesc% = секция-установки, ID-оборудования");
            if (m.hardwareIds.empty()) {
                add(Severity::Info, L"D002", m.line, 0, len,
                    L"В записи модели нет идентификатора оборудования: устройство можно установить только вручную");
                continue;
            }
            const wstring hw = m.hardwareIds.front();
            auto &seen = hwidsBySection[lower(m.modelsSection)];
            auto it = seen.find(lower(hw));
            if (it != seen.end())
                add(Severity::Warning, L"D003", m.line, m.hardwareIdColumn, m.hardwareIdLength,
                    std::format(L"Идентификатор {} уже указан в строке {} этой секции", hw, it->second + 1));
            else
                seen.emplace(lower(hw), m.line);
            if (contains(hw, L' '))
                add(Severity::Warning, L"D004", m.line, m.hardwareIdColumn, m.hardwareIdLength,
                    L"Идентификатор оборудования содержит пробел");
            else if (!contains(hw, L'\\') && hw[0] != L'*' && hw[0] != L'{')
                add(Severity::Info, L"D005", m.line, m.hardwareIdColumn, m.hardwareIdLength,
                    L"Необычный идентификатор оборудования: ожидается вид ШИНА\\ID (например, USB\\VID_xxxx&PID_xxxx)");
            if (!m.description.empty() && !contains(m.description, L'%'))
                add(Severity::Info, L"D006", m.line, 0, int(m.description.size()),
                    L"Описание устройства лучше вынести в [Strings] для локализации");
        }
    }

    // ---------------------------------------------------------------- DDInstall
    void checkInstallSections()
    {
        const bool extension = iequals(className(), L"Extension");
        for (const wstring &inst : model.installSections()) {
            const InfSection *s = doc.section(inst);
            if (!s || doc.entry(inst, L"Needs") || doc.entry(inst, L"Include"))
                continue;
            const wstring servicesName = inst + L".Services";
            const InfSection *svc = doc.section(servicesName);
            if (!svc) {
                if (!extension)
                    addSection(Severity::Info, L"I001", *s,
                               std::format(L"Нет секции [{}]: устройство будет установлено без функционального драйвера", servicesName));
                continue;
            }
            bool assoc = false;
            for (const InfLine *l : doc.entries(svc->name, L"AddService")) {
                uint32_t flags = 0;
                if (parseNumber(model.resolve(l->value(1)), flags) && (flags & 0x2))
                    assoc = true;
            }
            if (!assoc && !extension)
                addSection(Severity::Warning, L"I002", *svc,
                           std::format(L"Ни одна AddService в [{}] не содержит флаг 0x2 (SPSVCINST_ASSOCSERVICE)", svc->name));
        }
    }

    // ---------------------------------------------------------------- службы
    void checkServices()
    {
        std::set<wstring> copiedNames;
        for (const FileRef &f : model.copiedFiles())
            copiedNames.insert(lower(f.destName));

        for (const InfSection &s : doc.sections()) {
            if (model.role(s.name) != SectionRole::ServiceInstall)
                continue;
            for (const wchar_t *key : {L"ServiceType", L"StartType", L"ErrorControl", L"ServiceBinary"})
                if (!doc.entry(s.name, key))
                    addSection(Severity::Error, L"SV01", s,
                               std::format(L"В секции установки службы [{}] нет обязательной директивы {}", s.name, key));

            if (const InfLine *bin = doc.entry(s.name, L"ServiceBinary")) {
                const wstring path = trim(bin->value(0));
                const wstring file = lower(fileName(path));
                // системные файлы (%12%\WinUSB.sys и т. п.) пакет не копирует
                const bool fromPackage = istartsWith(path, L"%13%") || model.sourceFiles().count(file);
                if (!file.empty() && fromPackage && !copiedNames.count(file))
                    addAt(Severity::Warning, L"SV02", bin->valueAt(0),
                          std::format(L"Файл {} не копируется ни одной директивой CopyFiles", fileName(path)));
                if (pnpLockdown() && !istartsWith(path, L"%13%"))
                    addAt(Severity::Info, L"SV03", bin->valueAt(0),
                          L"Для пакетов с PnpLockdown=1 рекомендуется запуск из Driver Store: %13%\\файл");
            }
            uint32_t n = 0;
            if (const InfLine *st = doc.entry(s.name, L"ServiceType"))
                if (!parseNumber(model.resolve(st->value(0)), n))
                    addAt(Severity::Error, L"SV04", st->valueAt(0), L"ServiceType должен быть числом");
            if (const InfLine *st = doc.entry(s.name, L"StartType"))
                if (!parseNumber(model.resolve(st->value(0)), n) || n > 4)
                    addAt(Severity::Error, L"SV05", st->valueAt(0), L"StartType должен быть числом от 0 до 4");
            if (const InfLine *st = doc.entry(s.name, L"ErrorControl"))
                if (!parseNumber(model.resolve(st->value(0)), n) || n > 3)
                    addAt(Severity::Error, L"SV06", st->valueAt(0), L"ErrorControl должен быть числом от 0 до 3");
        }
    }

    // ---------------------------------------------------------------- файлы
    void checkFiles()
    {
        const bool hasLayout = !versionValue(L"LayoutFile").empty();
        const bool hasSourceFiles = !doc.sectionsWithPrefix(L"SourceDisksFiles").empty();

        std::set<wstring> copiedSources;
        for (const FileRef &f : model.copiedFiles()) {
            copiedSources.insert(lower(f.sourceName));
            if (!(hasLayout && !hasSourceFiles) && !model.sourceFiles().count(lower(f.sourceName)))
                add(Severity::Error, L"F001", f.line, f.column, f.length,
                    std::format(L"Файл {} не указан в [SourceDisksFiles]", f.sourceName));
        }

        for (const auto &it : model.sourceFiles()) {
            const SourceFile &f = it.second;
            if (!f.diskId.empty() && !model.sourceDisks().count(lower(f.diskId)))
                add(Severity::Error, L"F002", f.line, 0, lineLength(f.line),
                    std::format(L"Диск {} не описан в [SourceDisksNames]", f.diskId));
            if (!copiedSources.count(lower(f.name)))
                add(Severity::Info, L"F003", f.line, 0, lineLength(f.line),
                    std::format(L"Файл {} из [{}] нигде не копируется", f.name, f.section));
        }

        const bool hasDefaultDest = model.destinationDirs().count(L"defaultdestdir") > 0;
        for (const InfSection &s : doc.sections()) {
            const SectionRole r = model.role(s.name);
            if (r != SectionRole::CopyFilesList && r != SectionRole::DelFilesList && r != SectionRole::RenFilesList)
                continue;
            if (!hasDefaultDest && !model.destinationDirs().count(lower(s.name)))
                addSection(Severity::Warning, L"F004", s,
                           std::format(L"Для [{}] нет записи в [DestinationDirs] и не задан DefaultDestDir", s.name));
        }

        for (const wstring &dd : doc.sectionsWithPrefix(L"DestinationDirs")) {
            for (const InfLine *l : doc.entries(*doc.section(dd))) {
                if (!l->hasKey)
                    continue;
                const wstring key = trim(l->key.text);
                if (!iequals(key, L"DefaultDestDir") && !doc.hasSection(key))
                    addAt(Severity::Info, L"F005", l->key, std::format(L"Запись для несуществующей секции [{}]", key));
                uint32_t dirid = 0;
                const wstring v = trim(model.resolve(l->value(0)));
                if (!parseNumber(v, dirid) && !(v.size() > 1 && v[0] == L'-' && parseNumber(v.substr(1), dirid))) {
                    addAt(Severity::Error, L"F006", l->valueAt(0), L"Директория назначения должна задаваться числовым DirID");
                } else if (pnpLockdown() && (dirid == 11 || dirid == 12)) {
                    addAt(Severity::Info, L"F007", l->valueAt(0), L"Для пакетов с PnpLockdown=1 рекомендуется DirID 13 (Driver Store)");
                }
            }
        }
    }

    // ---------------------------------------------------------------- реестр
    void checkRegistry()
    {
        static const StringList roots = {L"HKR", L"HKLM", L"HKCU", L"HKCR", L"HKU", L"HKEY_LOCAL_MACHINE",
                                         L"HKEY_CURRENT_USER", L"HKEY_CLASSES_ROOT", L"HKEY_USERS"};
        for (const InfSection &s : doc.sections()) {
            const SectionRole r = model.role(s.name);
            if (r != SectionRole::AddReg && r != SectionRole::DelReg)
                continue;
            for (const InfLine *l : doc.entries(s)) {
                if (l->hasKey || l->values.empty())
                    continue;
                const wstring root = trim(model.resolve(l->value(0)));
                if (!icontains(roots, root)) {
                    addAt(Severity::Error, L"A001", l->values[0],
                          std::format(L"Неизвестный корень реестра: {} (ожидается HKR, HKLM, HKCU, HKCR или HKU)", root));
                    continue;
                }
                if (iequals(root, L"HKCU"))
                    addAt(Severity::Warning, L"A002", l->values[0],
                          L"HKCU в драйверах устройств не работает так, как ожидается, и запрещён для универсальных драйверов");
                if (r == SectionRole::AddReg && l->values.size() > 3 && !trim(l->value(3)).empty()) {
                    uint32_t flags = 0;
                    if (!parseNumber(model.resolve(l->value(3)), flags))
                        addAt(Severity::Warning, L"A003", l->values[3], L"Флаги AddReg должны быть числом");
                }
            }
        }
    }
};

} // namespace

std::vector<Diagnostic> validate(const InfDocument &doc, const InfModel &model)
{
    return Checker(doc, model).run();
}

} // namespace inf
