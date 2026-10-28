#include "stdafx.h"

#include "nodes.h"

#include "guids.h"
#include "images.h"
#include "infhelp.h"

#include <algorithm>
#include <format>

using inf::Analysis;
using inf::Severity;

namespace {

const wchar_t kKeySep = L'\x1F';

std::wstring num(int n)
{
    return std::to_wstring(n);
}

const inf::Manufacturer *findManufacturer(const Analysis &a, const std::wstring &key)
{
    for (const inf::Manufacturer &m : a.model.manufacturers())
        if (m.name + kKeySep + m.modelsBase == key)
            return &m;
    return nullptr;
}

std::wstring valuesText(const inf::InfLine &l)
{
    inf::StringList parts;
    for (const inf::InfValue &v : l.values)
        parts.push_back(v.raw);
    return inf::join(parts, L",");
}

std::shared_ptr<ResultRow> entryRow(const Node &node, const Analysis &a, int idx)
{
    const inf::InfLine &l = a.doc.lines()[size_t(idx)];
    auto r = std::make_shared<ResultRow>();
    r->type = RowType::Entry;
    r->file = node.file;
    r->fileId = node.file->id();
    r->logicalLine = idx;
    r->lineText = a.doc.lineText(idx);
    r->section = inf::sectionOfLine(a, idx);
    r->key = l.keyText();
    r->image = ImgEntry;
    return r;
}

void addDeviceRows(const Node &node, const Analysis &a, const inf::StringList &modelSections,
                   std::vector<std::shared_ptr<ResultRow>> &rows)
{
    for (const inf::DeviceModel &m : a.model.models()) {
        if (!modelSections.empty() && !inf::icontains(modelSections, m.modelsSection))
            continue;
        auto r = std::make_shared<ResultRow>();
        r->type = RowType::Device;
        r->file = node.file;
        r->fileId = node.file->id();
        r->logicalLine = m.logicalLine;
        r->lineText = a.doc.lineText(m.logicalLine);
        r->section = m.modelsSection;
        r->key = m.description;
        r->image = ImgDevice;
        r->columns = {m.displayName.empty() ? L"(без описания)" : m.displayName,
                      inf::join(m.hardwareIds, L", "),
                      inf::join(m.compatibleIds, L", "),
                      m.installSection.empty() ? m.installBase + L" (не найдена)" : m.installSection,
                      m.architecture.empty() ? L"любая" : m.architecture,
                      m.modelsSection,
                      num(m.line + 1)};
        rows.push_back(r);
    }
}

void addStringRows(const Node &node, const Analysis &a, const std::wstring &section, bool withSection,
                   std::vector<std::shared_ptr<ResultRow>> &rows)
{
    for (const inf::StringDef &d : a.model.allStrings()) {
        if (!section.empty() && !inf::iequals(d.section, section))
            continue;
        auto r = std::make_shared<ResultRow>();
        r->type = RowType::String;
        r->file = node.file;
        r->fileId = node.file->id();
        r->logicalLine = d.logicalLine;
        r->lineText = a.doc.lineText(d.logicalLine);
        r->section = d.section;
        r->key = d.key;
        r->image = ImgString;
        r->columns = {d.key, d.value, num(a.model.stringUses(d.key))};
        if (withSection)
            r->columns.push_back(d.section);
        r->columns.push_back(num(d.line + 1));
        rows.push_back(r);
    }
}

} // namespace

// ------------------------------------------------------------------ устройства компьютера

namespace {

std::unique_ptr<SystemSnapshot> &snapshotSlot()
{
    static std::unique_ptr<SystemSnapshot> s;
    return s;
}

std::wstring classKey(const inf::DeviceInfo &d)
{
    return d.classGuid.empty() ? L"class:" + inf::lower(d.className) : d.classGuid;
}

std::wstring deviceState(const inf::DeviceInfo &d)
{
    if (d.hasProblem)
        return L"ошибка, код " + std::to_wstring(d.problem);
    if (d.infName.empty())
        return L"драйвер не установлен";
    return L"работает";
}

} // namespace

const SystemSnapshot &systemSnapshot()
{
    auto &s = snapshotSlot();
    if (!s) {
        s = std::make_unique<SystemSnapshot>();
        CWaitCursor wait;
        s->devices = inf::enumerateDevices(&s->error);
        s->packages = inf::enumerateDriverPackages(s->devices, true);
    }
    return *s;
}

void reloadSystemSnapshot()
{
    snapshotSlot().reset();
}

bool isSystemNode(NodeType t)
{
    return t == NodeType::Hardware || t == NodeType::DeviceClass || t == NodeType::DriverPackages;
}

// ------------------------------------------------------------------ Node

const inf::Manufacturer *manufacturerForKey(const Analysis &a, const std::wstring &key)
{
    return findManufacturer(a, key);
}


std::wstring stringSectionTitle(const std::wstring &section)
{
    const size_t dot = section.find(L'.');
    if (dot == std::wstring::npos)
        return L"[" + section + L"]";
    const std::wstring code = section.substr(dot + 1);
    wchar_t *end = nullptr;
    const unsigned long lcid = wcstoul(code.c_str(), &end, 16);
    wchar_t name[128] = {};
    if (end && *end == 0 && lcid != 0
        && GetLocaleInfoW(MAKELCID(LANGID(lcid), SORT_DEFAULT), LOCALE_SLOCALIZEDDISPLAYNAME, name, 128) > 0)
        return L"[" + section + L"] — " + name;
    return L"[" + section + L"]";
}

std::wstring Node::displayName() const
{
    const auto a = file ? file->analysis() : nullptr;
    switch (type) {
    case NodeType::Root:
        return kSnapinName;
    case NodeType::Hardware:
        return std::format(L"Устройства компьютера ({})", systemSnapshot().devices.size());
    case NodeType::DeviceClass: {
        int n = 0;
        std::wstring name = key;
        for (const inf::DeviceInfo &d : systemSnapshot().devices)
            if (classKey(d) == key) {
                ++n;
                name = d.classDescription;
            }
        return std::format(L"{} ({})", name, n);
    }
    case NodeType::DriverPackages:
        return std::format(L"Пакеты драйверов ({} сторонних)", systemSnapshot().packages.size());
    case NodeType::File:
        return file->title();
    case NodeType::Version:
        return L"Версия [Version]";
    case NodeType::Devices:
        return std::format(L"Производители и устройства ({})", a->model.models().size());
    case NodeType::Manufacturer: {
        if (const inf::Manufacturer *m = findManufacturer(*a, key))
            return m->displayName.empty() ? m->modelsBase : m->displayName;
        return key.substr(0, key.find(kKeySep));
    }
    case NodeType::Sections:
        return std::format(L"Секции ({})", a->doc.sections().size());
    case NodeType::Section:
        return L"[" + key + L"]";
    case NodeType::Strings:
        return std::format(L"Строки ({})", a->model.allStrings().size());
    case NodeType::StringSection:
        return stringSectionTitle(key);
    case NodeType::Files:
        return std::format(L"Файлы ({})", a->model.copiedFiles().size());
    case NodeType::Problems: {
        const int e = file->count(Severity::Error), w = file->count(Severity::Warning);
        if (e == 0 && w == 0)
            return L"Проблемы (нет)";
        return std::format(L"Проблемы (ошибок: {}, предупреждений: {})", e, w);
    }
    }
    return {};
}

int Node::image() const
{
    switch (type) {
    case NodeType::Root: return ImgApp;
    case NodeType::Hardware: return ImgManufacturer;
    case NodeType::DeviceClass: return ImgDevice;
    case NodeType::DriverPackages: return ImgCopyFile;
    case NodeType::File: return ImgFile;
    case NodeType::Version: return ImgSettings;
    case NodeType::Manufacturer: return ImgManufacturer;
    case NodeType::Section:
    case NodeType::StringSection: return ImgSection;
    case NodeType::Problems: {
        if (file->count(Severity::Error))
            return ImgError;
        if (file->count(Severity::Warning))
            return ImgWarning;
        return ImgInfo;
    }
    default: return ImgFolder;
    }
}

int Node::openImage() const
{
    const int i = image();
    return i == ImgFolder ? ImgFolderOpen : i;
}

bool Node::mayHaveChildren() const
{
    switch (type) {
    case NodeType::Root:
    case NodeType::File:
    case NodeType::Sections:
    case NodeType::Hardware:
        return true;
    case NodeType::DeviceClass:
    case NodeType::DriverPackages:
        return false;
    case NodeType::Devices:
        return !file->analysis()->model.manufacturers().empty();
    case NodeType::Strings:
        return file->analysis()->model.stringSections().size() > 1;
    default:
        return false;
    }
}

const GUID &Node::nodeType() const
{
    switch (type) {
    case NodeType::Root: return NODETYPE_Root;
    case NodeType::File: return NODETYPE_File;
    case NodeType::Section:
    case NodeType::StringSection: return NODETYPE_Section;
    default: return NODETYPE_Folder;
    }
}

bool Node::isInside(const Node *ancestor) const
{
    for (const Node *n = this; n; n = n->parent)
        if (n == ancestor)
            return true;
    return false;
}

Node *Node::addChild(NodeType t, const std::wstring &k)
{
    auto n = std::make_unique<Node>();
    n->type = t;
    n->file = file;
    n->key = k;
    n->parent = this;
    children.push_back(std::move(n));
    return children.back().get();
}

Node *Node::findChild(NodeType t, const std::wstring &k) const
{
    for (const auto &c : children)
        if (c->type == t && c->key == k)
            return c.get();
    return nullptr;
}

std::vector<std::pair<NodeType, std::wstring>> desiredChildren(const Node &node)
{
    std::vector<std::pair<NodeType, std::wstring>> out;
    if (node.type == NodeType::Hardware) {
        // классы в порядке названий, как в Диспетчере устройств
        std::vector<std::pair<std::wstring, std::wstring>> classes;
        for (const inf::DeviceInfo &d : systemSnapshot().devices) {
            const std::wstring k = classKey(d);
            if (std::none_of(classes.begin(), classes.end(), [&](const auto &c) { return c.second == k; }))
                classes.push_back({d.classDescription, k});
        }
        std::sort(classes.begin(), classes.end(), [](const auto &x, const auto &y) {
            return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, x.first.c_str(), -1, y.first.c_str(), -1, nullptr,
                                   nullptr, 0) == CSTR_LESS_THAN;
        });
        for (const auto &c : classes)
            out.push_back({NodeType::DeviceClass, c.second});
        return out;
    }
    if (!node.file)
        return out;
    const auto a = node.file->analysis();
    switch (node.type) {
    case NodeType::File:
        out = {{NodeType::Version, L""}, {NodeType::Devices, L""}, {NodeType::Sections, L""},
               {NodeType::Strings, L""}, {NodeType::Files, L""}, {NodeType::Problems, L""}};
        break;
    case NodeType::Devices:
        for (const inf::Manufacturer &m : a->model.manufacturers())
            out.push_back({NodeType::Manufacturer, m.name + kKeySep + m.modelsBase});
        break;
    case NodeType::Sections:
        for (const inf::InfSection &s : a->doc.sections())
            out.push_back({NodeType::Section, s.name});
        break;
    case NodeType::Strings:
        if (a->model.stringSections().size() > 1)
            for (const std::wstring &s : a->model.stringSections())
                out.push_back({NodeType::StringSection, s});
        break;
    default:
        break;
    }
    return out;
}

// ------------------------------------------------------------------ колонки и строки

std::vector<ColumnInfo> columnsFor(const Node &node)
{
    switch (node.type) {
    case NodeType::Root:
        return {{L"Имя", 220}, {L"Путь", 320}, {L"Кодировка", 90}, {L"Ошибки", 70}, {L"Предупреждения", 100}};
    case NodeType::File:
        return {{L"Имя", 260}, {L"Описание", 420}};
    case NodeType::Version:
        return {{L"Ключ", 140}, {L"Значение", 260}, {L"После подстановки", 220}, {L"Строка", 60}};
    case NodeType::Devices:
    case NodeType::Manufacturer:
        return {{L"Описание", 220}, {L"ID оборудования", 220}, {L"Совместимые ID", 140}, {L"Секция установки", 160},
                {L"Архитектура", 90}, {L"Секция моделей", 180}, {L"Строка", 60}};
    case NodeType::Sections:
        return {{L"Секция", 240}, {L"Роль", 160}, {L"Строка", 60}, {L"Записей", 70}, {L"Ссылок", 70}};
    case NodeType::Section:
        return {{L"Ключ", 180}, {L"Значение", 320}, {L"Комментарий", 200}, {L"Строка", 60}};
    case NodeType::Strings:
        return {{L"Ключ", 200}, {L"Значение", 320}, {L"Использований", 100}, {L"Секция", 140}, {L"Строка", 60}};
    case NodeType::StringSection:
        return {{L"Ключ", 200}, {L"Значение", 360}, {L"Использований", 100}, {L"Строка", 60}};
    case NodeType::Files:
        return {{L"Файл", 180}, {L"Источник", 140}, {L"Секция копирования", 180}, {L"Диск", 50},
                {L"Поддиректория", 110}, {L"Строка", 60}, {L"Состояние", 200}};
    case NodeType::Problems:
        return {{L"Важность", 120}, {L"Строка", 60}, {L"Код", 60}, {L"Сообщение", 460}, {L"Секция", 160}, {L"Источник", 90}};
    case NodeType::Hardware:
        return {{L"Класс", 300}, {L"Устройств", 90}};
    case NodeType::DeviceClass:
        return {{L"Устройство", 280}, {L"Поставщик драйвера", 160}, {L"Версия", 110}, {L"Дата", 85}, {L"INF", 110},
                {L"Пакет", 90}, {L"ID оборудования", 260}, {L"Состояние", 150}};
    case NodeType::DriverPackages:
        return {{L"Опубликован", 100}, {L"Исходный INF", 170}, {L"Поставщик", 180}, {L"Класс", 120}, {L"Версия", 170},
                {L"Устройств", 80}, {L"Каталог", 150}};
    }
    return {{L"Имя", 200}};
}

std::vector<std::shared_ptr<ResultRow>> buildRows(const Node &node)
{
    std::vector<std::shared_ptr<ResultRow>> rows;
    if (node.type == NodeType::DeviceClass) {
        for (const inf::DeviceInfo &d : systemSnapshot().devices) {
            if (classKey(d) != node.key)
                continue;
            auto r = std::make_shared<ResultRow>();
            r->type = RowType::SysDevice;
            r->key = d.instanceId;
            r->device = d;
            r->image = d.hasProblem ? ImgError : ImgDevice;
            r->columns = {d.description, d.driverProvider, d.driverVersion, d.driverDate, d.infName,
                          d.infName.empty() ? L"" : (d.thirdParty() ? L"сторонний" : L"Windows"),
                          d.hardwareIds.empty() ? L"" : d.hardwareIds.front(), deviceState(d)};
            rows.push_back(r);
        }
        return rows;
    }
    if (node.type == NodeType::DriverPackages) {
        for (const inf::DriverPackage &p : systemSnapshot().packages) {
            auto r = std::make_shared<ResultRow>();
            r->type = RowType::Package;
            r->key = p.publishedName;
            r->package = p;
            r->image = ImgCopyFile;
            r->columns = {p.publishedName, p.originalName, p.provider, p.className, p.driverVer, num(p.devices), p.catalog};
            rows.push_back(r);
        }
        return rows;
    }
    if (!node.file)
        return rows;
    const auto a = node.file->analysis();

    switch (node.type) {
    case NodeType::Version:
        if (const inf::InfSection *s = a->doc.section(L"Version")) {
            for (int idx : s->entries) {
                const inf::InfLine &l = a->doc.lines()[size_t(idx)];
                auto r = entryRow(node, *a, idx);
                const std::wstring v = valuesText(l);
                const std::wstring resolved = a->model.resolve(v);
                r->columns = {l.keyText(), v, resolved != v ? resolved : L"", num(l.firstLine + 1)};
                rows.push_back(r);
            }
        }
        break;
    case NodeType::Devices:
        addDeviceRows(node, *a, {}, rows);
        break;
    case NodeType::Manufacturer:
        if (const inf::Manufacturer *m = findManufacturer(*a, node.key))
            addDeviceRows(node, *a, m->modelSections, rows);
        break;
    case NodeType::Section:
        if (const inf::InfSection *s = a->doc.section(node.key)) {
            for (int idx : s->entries) {
                const inf::InfLine &l = a->doc.lines()[size_t(idx)];
                auto r = entryRow(node, *a, idx);
                r->columns = {l.keyText(), valuesText(l), l.comment, num(l.firstLine + 1)};
                if (!l.error.empty())
                    r->image = ImgError;
                rows.push_back(r);
            }
        }
        break;
    case NodeType::Strings:
        addStringRows(node, *a, L"", true, rows);
        break;
    case NodeType::StringSection:
        addStringRows(node, *a, node.key, false, rows);
        break;
    case NodeType::Files:
        for (const inf::FileRef &f : a->model.copiedFiles()) {
            auto r = std::make_shared<ResultRow>();
            r->type = RowType::CopiedFile;
            r->file = node.file;
            r->fileId = node.file->id();
            r->logicalLine = f.listSection.empty() ? -1 : f.logicalLine;
            r->lineText = a->doc.lineText(f.logicalLine);
            r->section = f.listSection;
            r->key = f.destName;
            r->image = ImgCopyFile;
            const auto src = a->model.sourceFiles().find(inf::lower(f.sourceName));
            std::wstring state = L"в порядке";
            if (src == a->model.sourceFiles().end()) {
                state = L"нет в [SourceDisksFiles]";
                r->image = ImgWarning;
            }
            r->columns = {f.destName,
                          inf::iequals(f.sourceName, f.destName) ? L"" : f.sourceName,
                          f.listSection.empty() ? L"(CopyFiles=@)" : f.listSection,
                          src != a->model.sourceFiles().end() ? src->second.diskId : L"",
                          src != a->model.sourceFiles().end() ? src->second.subdir : L"",
                          num(f.line + 1),
                          state};
            rows.push_back(r);
        }
        break;
    case NodeType::Problems:
        for (const inf::Diagnostic &d : node.file->diagnostics()) {
            auto r = std::make_shared<ResultRow>();
            r->type = RowType::Problem;
            r->file = node.file;
            r->fileId = node.file->id();
            r->diagnostic = d;
            r->logicalLine = a->doc.logicalLineAt(d.line);
            r->section = inf::sectionOfLine(*a, r->logicalLine);
            r->image = d.severity == Severity::Error ? ImgError : d.severity == Severity::Warning ? ImgWarning : ImgInfo;
            r->columns = {inf::severityName(d.severity), num(d.line + 1), d.code, d.message,
                          r->section.empty() ? L"" : L"[" + r->section + L"]", d.source};
            rows.push_back(r);
        }
        break;
    default:
        break;
    }
    return rows;
}

std::wstring scopeItemColumn(const Node &child, int column)
{
    if (column == 0)
        return child.displayName();
    if (child.type == NodeType::File) {
        const InfFileDoc &f = *child.file;
        switch (column) {
        case 1: return f.isUntitled() ? L"(не сохранён)" : f.path();
        case 2: return inf::encodingName(f.format().encoding);
        case 3: return num(f.count(Severity::Error));
        case 4: return num(f.count(Severity::Warning));
        }
        return {};
    }
    if (child.type == NodeType::DeviceClass) {
        if (column != 1)
            return {};
        int n = 0;
        for (const inf::DeviceInfo &d : systemSnapshot().devices)
            if (classKey(d) == child.key)
                ++n;
        return num(n);
    }
    if (child.type == NodeType::Hardware)
        return column == 1 ? L"Устройства по классам, их драйверы и пакеты" : L"";
    if (child.type == NodeType::DriverPackages)
        return column == 1 ? L"Сторонние пакеты (oemNN.inf) в хранилище драйверов" : L"";
    if (!child.file)
        return {};
    const auto a = child.file->analysis();
    switch (child.type) {
    case NodeType::Version:
        return column == 1 ? L"Подпись, класс устройства, поставщик, версия драйвера, каталог" : L"";
    case NodeType::Devices:
        return column == 1 ? L"Секции [Manufacturer] и моделей: устройства и их идентификаторы оборудования" : L"";
    case NodeType::Sections:
        return column == 1 ? L"Все секции файла с ролями и связями" : L"";
    case NodeType::Strings:
        return column == 1 ? L"Строковые константы [Strings] и их локализации" : L"";
    case NodeType::Files:
        return column == 1 ? L"Копируемые файлы и их расположение на исходных дисках" : L"";
    case NodeType::Problems:
        return column == 1 ? L"Результаты проверки и InfVerif" : L"";
    case NodeType::Manufacturer:
        if (const inf::Manufacturer *m = findManufacturer(*a, child.key))
            return column == 1 ? inf::join(m->modelSections, L", ") : L"";
        return {};
    case NodeType::Section: {
        const inf::InfSection *s = a->doc.section(child.key);
        if (!s)
            return {};
        switch (column) {
        case 1: return inf::roleName(a->model.role(s->name));
        case 2: return num(s->headerLine() + 1);
        case 3: return num(int(s->entries.size()));
        case 4: return num(a->model.incomingCount(s->name));
        }
        return {};
    }
    case NodeType::StringSection:
        return column == 1 ? num(int(a->model.strings(child.key).size())) + L" строк" : L"";
    default:
        return {};
    }
}

int locateLine(const Analysis &a, int logicalLine, const std::wstring &lineText)
{
    if (logicalLine >= 0 && logicalLine < int(a.doc.lines().size()) && a.doc.lineText(logicalLine) == lineText)
        return logicalLine;
    for (int i = 0; i < int(a.doc.lines().size()); ++i)
        if (a.doc.lines()[size_t(i)].kind == inf::InfLine::Kind::Entry && a.doc.lineText(i) == lineText)
            return i;
    return -1;
}
