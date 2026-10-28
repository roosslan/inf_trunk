#include "infhelp.h"

#include "infknowledge.h"

#include <format>

namespace inf {

namespace {

// Контекст значения для explainValue: какая директива и какая позиция
wstring contextKey(const InfLine &l, SectionRole role, int valueIndex)
{
    if (l.hasKey) {
        const wstring k = lower(trim(l.key.text));
        if (k == L"servicetype" || k == L"starttype" || k == L"errorcontrol" || k == L"servicesidtype")
            return k;
        if (k == L"addservice" && valueIndex == 1)
            return L"addservice.flags";
        if (k == L"defaultdestdir" && valueIndex == 0)
            return L"dirid";
        if (role == SectionRole::Version && (k == L"class" || k == L"classguid"))
            return k;
        if (role == SectionRole::DestinationDirs && valueIndex == 0)
            return L"dirid";
    } else {
        if ((role == SectionRole::AddReg || role == SectionRole::DelReg) && valueIndex == 0)
            return L"addreg.root";
        if (role == SectionRole::AddReg && valueIndex == 3)
            return L"addreg.flags";
        if (role == SectionRole::CopyFilesList && valueIndex == 3)
            return L"copyfiles.flags";
    }
    return {};
}

} // namespace

wstring sectionOfLine(const Analysis &a, int logicalLine)
{
    if (logicalLine < 0 || logicalLine >= int(a.doc.lines().size()))
        return {};
    const int s = a.doc.lines()[size_t(logicalLine)].section;
    return s >= 0 ? a.doc.sections()[size_t(s)].name : wstring();
}

wstring describeEntry(const Analysis &a, int logicalLine)
{
    if (logicalLine < 0 || logicalLine >= int(a.doc.lines().size()))
        return {};
    const InfLine &l = a.doc.lines()[size_t(logicalLine)];
    const wstring section = sectionOfLine(a, logicalLine);
    const SectionRole role = a.model.role(section);
    StringList out;

    if (l.hasKey) {
        const wstring key = trim(l.key.text);
        if (role == SectionRole::Strings) {
            out.push_back(std::format(L"Строковая константа %{}%, используется {} раз(а).", key, a.model.stringUses(key)));
        } else if (role == SectionRole::Models) {
            out.push_back(L"Запись модели устройства: описание = секция-установки, ID-оборудования[, совместимые ID...].");
        } else if (role == SectionRole::Manufacturer) {
            out.push_back(L"Запись производителя: имя = секция-моделей[, декорации платформ...].");
        } else if (role == SectionRole::SourceDisksFiles) {
            out.push_back(L"Исходный файл: имя-файла = id-диска[, поддиректория[, размер]].");
        } else if (role == SectionRole::SourceDisksNames) {
            out.push_back(L"Исходный диск: id = описание, [файл-тега], [не используется], [путь].");
        } else if (role == SectionRole::DestinationDirs && !iequals(key, L"DefaultDestDir")) {
            out.push_back(std::format(L"Директория назначения для секции [{}].", key));
        } else if (const DirectiveInfo *d = findDirective(key)) {
            out.push_back(d->name + L" (" + d->where + L")");
            out.push_back(L"Синтаксис: " + d->syntax);
            out.push_back(d->help);
        }
    } else if (role == SectionRole::AddReg || role == SectionRole::DelReg) {
        out.push_back(L"Запись реестра: корень, [подключ], [имя значения], [флаги], [значение].");
    } else if (role == SectionRole::CopyFilesList) {
        out.push_back(L"Файл для копирования: имя-назначения[, имя-источника[, временное-имя[, флаги]]].");
    }

    for (int i = 0; i < int(l.values.size()); ++i) {
        const InfValue &v = l.values[size_t(i)];
        const wstring t = trim(v.text);
        if (t.empty())
            continue;
        StringList notes;
        if (a.doc.hasSection(t))
            notes.push_back(L"секция [" + a.doc.section(t)->name + L"], роль: " + roleName(a.model.role(t)));
        const wstring ctx = contextKey(l, role, i);
        if (!ctx.empty()) {
            const wstring e = explainValue(ctx, a.model.resolve(t));
            if (!e.empty())
                notes.push_back(e);
        }
        if (role == SectionRole::Models) {
            if (i == 1)
                notes.push_back(L"идентификатор оборудования");
            else if (i >= 2)
                notes.push_back(L"совместимый идентификатор");
            else if (!a.doc.hasSection(t))
                notes.push_back(L"секция установки (с учётом декораций .NT/.NTamd64)");
        }
        if (role == SectionRole::Manufacturer && i > 0) {
            const wstring arch = InfModel::architectureOf(t);
            notes.push_back(L"декорация платформы, архитектура: " + (arch.empty() ? wstring(L"любая") : arch));
        }
        for (const StringToken &tok : InfModel::stringTokens(v.raw)) {
            if (tok.numeric) {
                const wstring d = dirIdDescription(toInt(tok.name));
                notes.push_back(L"%" + tok.name + L"% — DirID" + (d.empty() ? wstring() : L": " + d));
            } else if (a.model.hasString(tok.name)) {
                notes.push_back(L"%" + tok.name + L"% = \"" + a.model.stringValue(tok.name) + L"\"");
            } else {
                notes.push_back(L"%" + tok.name + L"% — строка не определена");
            }
        }
        if (!notes.empty())
            out.push_back(std::format(L"Значение {} «{}»: {}", i + 1, t, join(notes, L"; ")));
    }
    if (!l.comment.empty())
        out.push_back(L"Комментарий: " + l.comment);
    return join(out, L"\r\n");
}

wstring describeSection(const Analysis &a, const wstring &section)
{
    const InfSection *s = a.doc.section(section);
    if (!s)
        return {};
    const SectionRole role = a.model.role(s->name);
    StringList out;
    out.push_back(L"Роль: " + roleName(role));
    const wstring help = standardSectionHelp(s->name);
    if (!help.empty())
        out.push_back(help);
    if (s->chunks.size() > 1)
        out.push_back(std::format(L"Секция встречается в файле {} раз(а); Windows объединяет фрагменты.", s->chunks.size()));

    const auto to = a.model.refsTo(s->name);
    if (!to.empty()) {
        out.push_back(L"");
        out.push_back(L"Используется:");
        for (const SectionRef &r : to)
            out.push_back(L"    [" + r.from + L"] — " + r.directive
                          + (r.line >= 0 ? std::format(L", строка {}", r.line + 1) : wstring()));
    } else if (role == SectionRole::Unknown) {
        out.push_back(L"На секцию нет ссылок.");
    }

    const auto from = a.model.refsFrom(s->name);
    if (!from.empty()) {
        out.push_back(L"");
        out.push_back(L"Ссылается на:");
        for (const SectionRef &r : from)
            out.push_back(L"    " + r.directive + L" → [" + r.target + L"]" + (r.exists ? wstring() : wstring(L" — не найдена")));
    }
    return join(out, L"\r\n");
}

} // namespace inf
