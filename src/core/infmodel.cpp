#include "infmodel.h"

#include "infknowledge.h"

namespace inf {

namespace {

struct DirectiveRef {
    const wchar_t *name;
    SectionRole role;
    int index;          // -1 — все значения являются именами секций
};

const DirectiveRef kDirectiveRefs[] = {
    {L"CopyFiles", SectionRole::CopyFilesList, -1},
    {L"DelFiles", SectionRole::DelFilesList, -1},
    {L"RenFiles", SectionRole::RenFilesList, -1},
    {L"AddReg", SectionRole::AddReg, -1},
    {L"DelReg", SectionRole::DelReg, -1},
    {L"BitReg", SectionRole::BitReg, -1},
    {L"AddProperty", SectionRole::AddProperty, -1},
    {L"DelProperty", SectionRole::DelProperty, -1},
    {L"LogConfig", SectionRole::LogConfig, -1},
    {L"AddPowerSetting", SectionRole::PowerSetting, -1},
    {L"RegisterDlls", SectionRole::RegisterDlls, -1},
    {L"UnregisterDlls", SectionRole::RegisterDlls, -1},
    {L"ProfileItems", SectionRole::ProfileItems, -1},
    {L"Ini2Reg", SectionRole::IniUpdate, -1},
    {L"UpdateInis", SectionRole::IniUpdate, -1},
    {L"UpdateIniFields", SectionRole::IniUpdate, -1},
    {L"AddTrigger", SectionRole::Trigger, -1},
    {L"AddService", SectionRole::ServiceInstall, 2},
    {L"AddInterface", SectionRole::InterfaceInstall, 2},
    {L"AddComponent", SectionRole::ComponentInstall, 2},
    {L"AddSoftware", SectionRole::SoftwareInstall, 2},
    {L"AddFilter", SectionRole::FilterInstall, 2},
    {L"AddEventProvider", SectionRole::EventProvider, 1},
    {L"KmdfService", SectionRole::WdfInstall, 1},
    {L"UmdfService", SectionRole::UmdfInstall, 1},
    {L"WMIInterface", SectionRole::WmiSecurity, 2},
};

struct Suffix {
    const wchar_t *name;
    SectionRole role;
};

const Suffix kInstallSuffixes[] = {
    {L"Services", SectionRole::InstallServices},
    {L"HW", SectionRole::InstallHw},
    {L"CoInstallers", SectionRole::InstallCoInstallers},
    {L"Interfaces", SectionRole::InstallInterfaces},
    {L"Wdf", SectionRole::InstallWdf},
    {L"Components", SectionRole::InstallComponents},
    {L"Software", SectionRole::InstallSoftware},
    {L"Events", SectionRole::InstallEvents},
    {L"Filters", SectionRole::InstallFilters},
    {L"LogConfigOverride", SectionRole::InstallOther},
    {L"FactDef", SectionRole::InstallOther},
    {L"WMI", SectionRole::InstallOther},
};

bool hasInstallSuffix(const wstring &name)
{
    const size_t dot = name.rfind(L'.');
    if (dot == wstring::npos)
        return false;
    const wstring last = name.substr(dot + 1);
    for (const Suffix &s : kInstallSuffixes)
        if (iequals(last, s.name))
            return true;
    return false;
}

wstring entryValueText(const InfLine &l)
{
    StringList parts;
    for (const InfValue &v : l.values)
        parts.push_back(v.text);
    return join(parts, L",");
}

int logicalIndex(const InfDocument &doc, const InfLine *l)
{
    return int(l - doc.lines().data());
}

} // namespace

wstring roleName(SectionRole role)
{
    switch (role) {
    case SectionRole::Unknown: return L"не определена";
    case SectionRole::Version: return L"версия";
    case SectionRole::Manufacturer: return L"производители";
    case SectionRole::Models: return L"модели";
    case SectionRole::Install: return L"DDInstall";
    case SectionRole::InstallServices: return L"DDInstall.Services";
    case SectionRole::InstallHw: return L"DDInstall.HW";
    case SectionRole::InstallCoInstallers: return L"DDInstall.CoInstallers";
    case SectionRole::InstallInterfaces: return L"DDInstall.Interfaces";
    case SectionRole::InstallWdf: return L"DDInstall.Wdf";
    case SectionRole::InstallComponents: return L"DDInstall.Components";
    case SectionRole::InstallSoftware: return L"DDInstall.Software";
    case SectionRole::InstallEvents: return L"DDInstall.Events";
    case SectionRole::InstallFilters: return L"DDInstall.Filters";
    case SectionRole::InstallOther: return L"DDInstall (доп.)";
    case SectionRole::DefaultInstall: return L"DefaultInstall";
    case SectionRole::ClassInstall: return L"ClassInstall32";
    case SectionRole::ServiceInstall: return L"установка службы";
    case SectionRole::EventLogInstall: return L"журнал событий";
    case SectionRole::CopyFilesList: return L"копирование файлов";
    case SectionRole::DelFilesList: return L"удаление файлов";
    case SectionRole::RenFilesList: return L"переименование файлов";
    case SectionRole::AddReg: return L"AddReg";
    case SectionRole::DelReg: return L"DelReg";
    case SectionRole::BitReg: return L"BitReg";
    case SectionRole::AddProperty: return L"AddProperty";
    case SectionRole::DelProperty: return L"DelProperty";
    case SectionRole::InterfaceInstall: return L"установка интерфейса";
    case SectionRole::WdfInstall: return L"установка KMDF";
    case SectionRole::UmdfInstall: return L"установка UMDF";
    case SectionRole::ComponentInstall: return L"установка компонента";
    case SectionRole::SoftwareInstall: return L"установка ПО";
    case SectionRole::EventProvider: return L"поставщик событий";
    case SectionRole::FilterInstall: return L"установка фильтра";
    case SectionRole::LogConfig: return L"LogConfig";
    case SectionRole::PowerSetting: return L"параметры питания";
    case SectionRole::RegisterDlls: return L"регистрация DLL";
    case SectionRole::ProfileItems: return L"ярлыки";
    case SectionRole::IniUpdate: return L"INI-файлы";
    case SectionRole::Trigger: return L"триггер службы";
    case SectionRole::WmiSecurity: return L"безопасность WMI";
    case SectionRole::SourceDisksNames: return L"исходные диски";
    case SectionRole::SourceDisksFiles: return L"исходные файлы";
    case SectionRole::DestinationDirs: return L"директории назначения";
    case SectionRole::Strings: return L"строки";
    case SectionRole::ControlFlags: return L"флаги управления";
    case SectionRole::InterfaceInstall32: return L"классы интерфейсов";
    case SectionRole::Standard: return L"стандартная";
    }
    return {};
}

wstring InfModel::architectureOf(const wstring &decoration)
{
    const wstring d = lower(trim(decoration));
    if (d.compare(0, 2, L"nt") != 0)
        return {};
    const wstring rest = d.substr(2);
    return rest.substr(0, rest.find(L'.'));
}

std::vector<StringToken> InfModel::stringTokens(const wstring &text)
{
    std::vector<StringToken> out;
    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        const size_t open = text.find(L'%', i);
        if (open == wstring::npos)
            break;
        const size_t close = text.find(L'%', open + 1);
        if (close == wstring::npos)
            break;
        if (close == open + 1) {  // %% — литерал '%'
            i = close + 1;
            continue;
        }
        StringToken t;
        t.start = int(open);
        t.length = int(close - open + 1);
        t.name = text.substr(open + 1, close - open - 1);
        uint32_t num = 0;
        t.numeric = parseUInt(t.name, num) || (t.name.size() > 1 && t.name[0] == L'-' && parseUInt(t.name.substr(1), num));
        out.push_back(t);
        i = close + 1;
    }
    return out;
}

wstring InfModel::stringValue(const wstring &key) const
{
    const wstring k = lower(key);
    auto it = m_strings.find(lower(m_baseStrings));
    if (it != m_strings.end()) {
        auto s = it->second.find(k);
        if (s != it->second.end())
            return s->second.value;
    }
    for (const auto &sec : m_strings) {
        auto s = sec.second.find(k);
        if (s != sec.second.end())
            return s->second.value;
    }
    return {};
}

bool InfModel::hasString(const wstring &key) const
{
    const wstring k = lower(key);
    for (const auto &sec : m_strings)
        if (sec.second.count(k))
            return true;
    return false;
}

int InfModel::stringUses(const wstring &key) const
{
    auto it = m_stringUses.find(lower(key));
    return it == m_stringUses.end() ? 0 : it->second;
}

wstring InfModel::resolve(const wstring &text, StringList *missing) const
{
    const std::vector<StringToken> tokens = stringTokens(text);
    wstring out;
    size_t pos = 0;
    for (const StringToken &t : tokens) {
        out += text.substr(pos, size_t(t.start) - pos);
        if (!t.numeric && hasString(t.name)) {
            out += stringValue(t.name);
        } else {
            out += text.substr(size_t(t.start), size_t(t.length));
            if (!t.numeric && missing)
                missing->push_back(t.name);
        }
        pos = size_t(t.start + t.length);
    }
    out += text.substr(pos);
    return replaceAll(out, L"%%", L"%");
}

std::vector<StringDef> InfModel::strings(const wstring &section) const
{
    std::vector<StringDef> out;
    for (const StringDef &d : m_allStrings)
        if (iequals(d.section, section))
            out.push_back(d);
    return out;
}

SectionRole InfModel::role(const wstring &section) const
{
    auto it = m_roles.find(sectionKey(section));
    return it == m_roles.end() ? SectionRole::Unknown : it->second;
}

std::vector<SectionRef> InfModel::refsFrom(const wstring &section) const
{
    std::vector<SectionRef> out;
    for (const SectionRef &r : m_refs)
        if (iequals(r.from, section))
            out.push_back(r);
    return out;
}

std::vector<SectionRef> InfModel::refsTo(const wstring &section) const
{
    std::vector<SectionRef> out;
    for (const SectionRef &r : m_refs)
        if (iequals(r.target, section))
            out.push_back(r);
    return out;
}

int InfModel::incomingCount(const wstring &section) const
{
    auto it = m_incoming.find(sectionKey(section));
    return it == m_incoming.end() ? 0 : it->second;
}

StringList InfModel::modelSections() const
{
    StringList out;
    for (const Manufacturer &m : m_manufacturers)
        for (const wstring &s : m.modelSections)
            if (!icontains(out, s))
                out.push_back(s);
    return out;
}

void InfModel::setRole(const wstring &section, SectionRole role)
{
    const wstring k = sectionKey(section);
    auto it = m_roles.find(k);
    if (it == m_roles.end() || it->second == SectionRole::Unknown)
        m_roles[k] = role;
}

void InfModel::addRef(const InfDocument &doc, SectionRef ref)
{
    ref.exists = doc.hasSection(ref.target);
    if (ref.exists) {
        // ссылаемся на написание из заголовка
        ref.target = doc.section(ref.target)->name;
        m_incoming[sectionKey(ref.target)] += 1;
        setRole(ref.target, ref.role);
    }
    m_refs.push_back(ref);
}

InfModel InfModel::analyze(const InfDocument &doc)
{
    InfModel m;

    // Стандартные секции
    for (const InfSection &s : doc.sections()) {
        const wstring base = lower(s.name.substr(0, s.name.find(L'.')));
        if (base == L"version")
            m.setRole(s.name, SectionRole::Version);
        else if (base == L"manufacturer")
            m.setRole(s.name, SectionRole::Manufacturer);
        else if (base == L"strings")
            m.setRole(s.name, SectionRole::Strings);
        else if (base == L"sourcedisksnames")
            m.setRole(s.name, SectionRole::SourceDisksNames);
        else if (base == L"sourcedisksfiles")
            m.setRole(s.name, SectionRole::SourceDisksFiles);
        else if (base == L"destinationdirs")
            m.setRole(s.name, SectionRole::DestinationDirs);
        else if (base == L"controlflags")
            m.setRole(s.name, SectionRole::ControlFlags);
        else if (base == L"interfaceinstall32")
            m.setRole(s.name, SectionRole::InterfaceInstall32);
        else if ((base == L"defaultinstall" || base == L"defaultuninstall") && !hasInstallSuffix(s.name))
            m.setRole(s.name, SectionRole::DefaultInstall);
        else if (base == L"classinstall32" && !hasInstallSuffix(s.name))
            m.setRole(s.name, SectionRole::ClassInstall);
        else if (isStandardSection(s.name) && !hasInstallSuffix(s.name))
            m.setRole(s.name, SectionRole::Standard);
    }

    // Строки
    m.m_stringSections = doc.sectionsWithPrefix(L"Strings");
    if (doc.hasSection(L"Strings"))
        m.m_baseStrings = doc.section(L"Strings")->name;
    else if (!m.m_stringSections.empty())
        m.m_baseStrings = m.m_stringSections.front();
    for (const wstring &sec : m.m_stringSections) {
        auto &table = m.m_strings[lower(sec)];
        for (const InfLine *l : doc.entries(*doc.section(sec))) {
            if (!l->hasKey)
                continue;
            StringDef d;
            d.key = l->key.text;
            d.value = entryValueText(*l);
            d.section = sec;
            d.line = l->firstLine;
            d.logicalLine = logicalIndex(doc, l);
            const wstring k = lower(d.key);
            if (!table.count(k))
                table.emplace(k, d);
            m.m_allStrings.push_back(d);
        }
    }

    // Использование строк вне секций строк
    for (const InfLine &l : doc.lines()) {
        if (l.kind != InfLine::Kind::Entry)
            continue;
        if (l.section >= 0 && m.role(doc.sections()[size_t(l.section)].name) == SectionRole::Strings)
            continue;
        auto count = [&](const InfValue &v) {
            for (const StringToken &t : stringTokens(v.raw))
                if (!t.numeric)
                    m.m_stringUses[lower(t.name)] += 1;
        };
        if (l.hasKey)
            count(l.key);
        for (const InfValue &v : l.values)
            count(v);
    }

    // Производители и секции моделей
    if (const InfSection *mfgSec = doc.section(L"Manufacturer")) {
        for (const InfLine *l : doc.entries(*mfgSec)) {
            if (l->values.empty())
                continue;
            Manufacturer mf;
            mf.line = l->firstLine;
            mf.logicalLine = logicalIndex(doc, l);
            mf.modelsBase = trim(l->value(0));
            mf.name = l->hasKey ? l->key.text : mf.modelsBase;
            mf.displayName = m.resolve(mf.name);
            if (mf.modelsBase.empty())
                continue;
            for (size_t i = 1; i < l->values.size(); ++i) {
                const wstring d = trim(l->values[i].text);
                if (!d.empty())
                    mf.decorations.push_back(d);
            }

            auto refAt = [&](const wstring &target, const InfValue &v) {
                SectionRef r;
                r.target = target;
                r.from = mfgSec->name;
                r.directive = L"Manufacturer";
                r.role = SectionRole::Models;
                r.line = v.line;
                r.column = v.column;
                r.length = v.length;
                m.addRef(doc, r);
            };

            if (mf.decorations.empty()) {
                mf.modelSections.push_back(mf.modelsBase);
                refAt(mf.modelsBase, l->values[0]);
            } else {
                if (doc.hasSection(mf.modelsBase)) {
                    mf.modelSections.push_back(mf.modelsBase);
                    refAt(mf.modelsBase, l->values[0]);
                }
                for (size_t i = 1; i < l->values.size(); ++i) {
                    const wstring d = trim(l->values[i].text);
                    if (d.empty())
                        continue;
                    const wstring target = mf.modelsBase + L"." + d;
                    mf.modelSections.push_back(target);
                    refAt(target, l->values[i]);
                }
            }
            m.m_manufacturers.push_back(mf);
        }
    }

    // Модели устройств и секции установки
    auto addInstall = [&](const wstring &name) {
        if (!icontains(m.m_installSections, name))
            m.m_installSections.push_back(name);
    };
    const std::vector<Manufacturer> manufacturers = m.m_manufacturers;
    for (const Manufacturer &mf : manufacturers) {
        for (const wstring &msName : mf.modelSections) {
            const InfSection *ms = doc.section(msName);
            if (!ms)
                continue;
            wstring decoration = msName.substr(mf.modelsBase.size());
            if (!decoration.empty() && decoration[0] == L'.')
                decoration = decoration.substr(1);
            const wstring arch = architectureOf(decoration);

            for (const InfLine *l : doc.entries(*ms)) {
                if (l->values.empty())
                    continue;
                DeviceModel dm;
                dm.modelsSection = ms->name;
                dm.description = l->keyText();
                dm.displayName = m.resolve(dm.description);
                dm.installBase = trim(l->value(0));
                dm.architecture = arch;
                dm.line = l->firstLine;
                dm.logicalLine = logicalIndex(doc, l);
                if (l->values.size() > 1) {
                    const wstring hw = trim(l->value(1));
                    if (!hw.empty())
                        dm.hardwareIds.push_back(hw);
                    dm.hardwareIdColumn = l->values[1].column;
                    dm.hardwareIdLength = l->values[1].length;
                }
                for (size_t i = 2; i < l->values.size(); ++i) {
                    const wstring c = trim(l->values[i].text);
                    if (!c.empty())
                        dm.compatibleIds.push_back(c);
                }

                if (!dm.installBase.empty()) {
                    StringList candidates;
                    if (!arch.empty())
                        candidates.push_back(dm.installBase + L".NT" + arch);
                    candidates.push_back(dm.installBase + L".NT");
                    candidates.push_back(dm.installBase);
                    for (const wstring &c : candidates) {
                        if (doc.hasSection(c)) {
                            dm.installSection = doc.section(c)->name;
                            break;
                        }
                    }
                    SectionRef r;
                    r.target = dm.installSection.empty() ? dm.installBase : dm.installSection;
                    r.from = ms->name;
                    r.directive = L"Модель";
                    r.role = SectionRole::Install;
                    r.line = l->values[0].line;
                    r.column = l->values[0].column;
                    r.length = l->values[0].length;
                    m.addRef(doc, r);
                    if (!dm.installSection.empty())
                        addInstall(dm.installSection);

                    // остальные платформенные варианты той же секции тоже используются
                    StringList variants = {dm.installBase, dm.installBase + L".NT"};
                    for (const wstring &a : knownArchitectures())
                        variants.push_back(dm.installBase + L".NT" + a);
                    for (const wstring &v : variants) {
                        if (!doc.hasSection(v) || iequals(v, dm.installSection))
                            continue;
                        const wstring real = doc.section(v)->name;
                        if (icontains(m.m_installSections, real))
                            continue;
                        SectionRef iv;
                        iv.target = real;
                        iv.from = ms->name;
                        iv.directive = L"Вариант платформы";
                        iv.role = SectionRole::Install;
                        iv.implicit = true;
                        m.addRef(doc, iv);
                        addInstall(real);
                    }
                }
                m.m_models.push_back(dm);
            }
        }
    }

    // Неявные секции DDInstall.Services, .HW и т. д.
    StringList installLike = m.m_installSections;
    for (const InfSection &s : doc.sections()) {
        const SectionRole r = m.role(s.name);
        if (r == SectionRole::DefaultInstall || r == SectionRole::ClassInstall)
            installLike.push_back(s.name);
    }
    for (const wstring &inst : installLike) {
        for (const Suffix &suf : kInstallSuffixes) {
            const wstring child = inst + L"." + suf.name;
            if (!doc.hasSection(child))
                continue;
            SectionRef r;
            r.target = child;
            r.from = inst;
            r.directive = suf.name;
            r.role = suf.role;
            r.implicit = true;
            m.addRef(doc, r);
        }
    }

    // Директивы, ссылающиеся на секции
    for (const InfSection &s : doc.sections()) {
        const SectionRole sr = m.role(s.name);
        if (sr == SectionRole::Strings || sr == SectionRole::SourceDisksNames || sr == SectionRole::SourceDisksFiles
            || sr == SectionRole::DestinationDirs || sr == SectionRole::Manufacturer || sr == SectionRole::Models
            || sr == SectionRole::Version)
            continue;

        for (const InfLine *l : doc.entries(s)) {
            if (!l->hasKey)
                continue;
            const wstring key = trim(l->key.text);

            if (sr == SectionRole::InterfaceInstall32) {
                if (!trim(l->value(0)).empty()) {
                    SectionRef r;
                    r.target = trim(l->value(0));
                    r.from = s.name;
                    r.directive = L"InterfaceInstall32";
                    r.role = SectionRole::InterfaceInstall;
                    r.line = l->values[0].line;
                    r.column = l->values[0].column;
                    r.length = l->values[0].length;
                    m.addRef(doc, r);
                }
                continue;
            }

            for (const DirectiveRef &d : kDirectiveRefs) {
                if (!iequals(key, d.name))
                    continue;
                auto refValue = [&](size_t i, SectionRole role) {
                    if (i >= l->values.size())
                        return;
                    const InfValue &v = l->values[i];
                    const wstring t = trim(v.text);
                    if (t.empty())
                        return;
                    if (role == SectionRole::CopyFilesList && t[0] == L'@') {
                        FileRef f;
                        f.destName = t.substr(1);
                        f.sourceName = f.destName;
                        f.line = v.line;
                        f.logicalLine = logicalIndex(doc, l);
                        f.column = v.column;
                        f.length = v.length;
                        m.m_files.push_back(f);
                        return;
                    }
                    SectionRef r;
                    r.target = t;
                    r.from = s.name;
                    r.directive = d.name;
                    r.role = role;
                    r.line = v.line;
                    r.column = v.column;
                    r.length = v.length;
                    m.addRef(doc, r);
                };
                if (d.index < 0) {
                    for (size_t i = 0; i < l->values.size(); ++i)
                        refValue(i, d.role);
                } else {
                    refValue(size_t(d.index), d.role);
                    if (d.role == SectionRole::ServiceInstall)
                        refValue(3, SectionRole::EventLogInstall);
                }
                break;
            }
        }
    }

    // Директории назначения
    for (const wstring &dd : doc.sectionsWithPrefix(L"DestinationDirs"))
        for (const InfLine *l : doc.entries(*doc.section(dd)))
            if (l->hasKey)
                m.m_destDirs[lower(trim(l->key.text))] = l->firstLine;

    // Исходные диски и файлы
    for (const wstring &sn : doc.sectionsWithPrefix(L"SourceDisksNames"))
        for (const InfLine *l : doc.entries(*doc.section(sn)))
            if (l->hasKey)
                m.m_disks.insert(lower(trim(l->key.text)));
    for (const wstring &sf : doc.sectionsWithPrefix(L"SourceDisksFiles")) {
        for (const InfLine *l : doc.entries(*doc.section(sf))) {
            if (!l->hasKey)
                continue;
            SourceFile f;
            f.name = trim(l->key.text);
            f.diskId = trim(l->value(0));
            f.subdir = trim(l->value(1));
            f.section = sf;
            f.line = l->firstLine;
            f.logicalLine = logicalIndex(doc, l);
            const wstring k = lower(f.name);
            if (!m.m_sourceFiles.count(k))
                m.m_sourceFiles.emplace(k, f);
        }
    }

    // Файлы из секций списков копирования
    for (const InfSection &s : doc.sections()) {
        if (m.role(s.name) != SectionRole::CopyFilesList)
            continue;
        for (const InfLine *l : doc.entries(s)) {
            FileRef f;
            f.listSection = s.name;
            f.line = l->firstLine;
            f.logicalLine = logicalIndex(doc, l);
            if (l->hasKey) {
                f.destName = trim(l->key.text);
                f.sourceName = trim(l->value(0));
                f.column = l->key.column;
                f.length = l->key.length;
            } else {
                f.destName = trim(l->value(0));
                f.sourceName = trim(l->value(1));
                if (!l->values.empty()) {
                    f.column = l->values[0].column;
                    f.length = l->values[0].length;
                }
            }
            if (f.sourceName.empty())
                f.sourceName = f.destName;
            if (!f.destName.empty())
                m.m_files.push_back(f);
        }
    }

    return m;
}

} // namespace inf
