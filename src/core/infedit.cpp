#include "infedit.h"

#include <algorithm>

namespace inf {

namespace {

wstring indentOf(const wstring &line)
{
    size_t i = 0;
    while (i < line.size() && (line[i] == L' ' || line[i] == L'\t'))
        ++i;
    return line.substr(0, i);
}

int logicalIndex(const InfDocument &doc, const InfLine *l)
{
    return int(l - doc.lines().data());
}

} // namespace

wstring formatEntry(const wstring &key, const StringList &values)
{
    return key + L" = " + join(values, L",");
}

TextEdit replaceLineEdit(const InfDocument &doc, int logicalLine, const wstring &newText, bool keepComment)
{
    const InfLine &l = doc.lines()[size_t(logicalLine)];
    const wstring first = doc.physicalLine(l.firstLine);
    wstring result = indentOf(first) + newText;

    if (keepComment && !l.comment.empty()) {
        wstring gap = L" ";
        if (l.firstLine == l.lastLine && first.size() >= l.comment.size()
            && first.compare(first.size() - l.comment.size(), l.comment.size(), l.comment) == 0) {
            const wstring before = first.substr(0, first.size() - l.comment.size());
            size_t ws = 0;
            while (ws < before.size() && isSpace(before[before.size() - 1 - ws]))
                ++ws;
            if (ws > 0 && ws < before.size())
                gap = before.substr(before.size() - ws);
        }
        result += gap + l.comment;
    }

    TextEdit e;
    e.firstLine = l.firstLine;
    e.removeCount = l.lastLine - l.firstLine + 1;
    e.lines.push_back(result);
    return e;
}

TextEdit removeLineEdit(const InfDocument &doc, int logicalLine)
{
    const InfLine &l = doc.lines()[size_t(logicalLine)];
    TextEdit e;
    e.firstLine = l.firstLine;
    e.removeCount = l.lastLine - l.firstLine + 1;
    return e;
}

TextEdit removeKeyEdit(const InfDocument &doc, const wstring &section, const wstring &key)
{
    const InfLine *l = doc.entry(section, key);
    if (!l)
        return TextEdit();
    return removeLineEdit(doc, logicalIndex(doc, l));
}

TextEdit setValueEdit(const InfDocument &doc, const wstring &section, const wstring &key, const wstring &value)
{
    if (const InfLine *l = doc.entry(section, key)) {
        const wstring first = doc.physicalLine(l->firstLine);
        const wstring indent = indentOf(first);
        wstring prefix;
        if (!l->values.empty() && l->values.front().line == l->firstLine) {
            prefix = first.substr(0, size_t(l->values.front().column)).substr(indent.size());
        } else {
            const size_t eq = first.find(L'=', size_t(l->key.column + l->key.length));
            prefix = eq != wstring::npos ? first.substr(0, eq + 1).substr(indent.size()) + L" " : l->key.raw + L" = ";
        }
        return replaceLineEdit(doc, logicalIndex(doc, l), prefix + value);
    }
    return appendToSectionEdit(doc, section, {key + L" = " + value});
}

TextEdit appendToSectionEdit(const InfDocument &doc, const wstring &section, const StringList &lines)
{
    if (const InfSection *s = doc.section(section)) {
        wstring indent;
        if (!s->entries.empty())
            indent = indentOf(doc.physicalLine(doc.lines()[size_t(s->entries.back())].firstLine));
        TextEdit e;
        e.firstLine = s->chunks.back().lastContentLine + 1;
        for (const wstring &l : lines)
            e.lines.push_back(l.empty() ? l : indent + l);
        return e;
    }

    StringList block;
    block.push_back(L"[" + section + L"]");
    block.insert(block.end(), lines.begin(), lines.end());

    // [Version] принято размещать первой секцией
    if (iequals(section, L"Version")) {
        TextEdit e;
        e.firstLine = 0;
        if (!doc.sections().empty()) {
            e.firstLine = doc.sections().front().headerLine();
            for (const InfSection &s : doc.sections())
                e.firstLine = std::min(e.firstLine, s.headerLine());
        }
        e.lines = block;
        if (!doc.sections().empty())
            e.lines.push_back(wstring());
        return e;
    }
    return appendSectionEdit(doc, block);
}

TextEdit appendSectionEdit(const InfDocument &doc, const StringList &lines)
{
    const StringList &phys = doc.physicalLines();
    int last = int(phys.size()) - 1;
    while (last >= 0 && trim(phys[size_t(last)]).empty())
        --last;
    TextEdit e;
    e.firstLine = last + 1;
    if (last >= 0)
        e.lines.push_back(wstring());
    e.lines.insert(e.lines.end(), lines.begin(), lines.end());
    return e;
}

std::vector<TextEdit> removeSectionEdits(const InfDocument &doc, const wstring &section)
{
    std::vector<TextEdit> out;
    const InfSection *s = doc.section(section);
    if (!s)
        return out;
    for (const InfSectionChunk &c : s->chunks) {
        TextEdit e;
        e.firstLine = c.headerLine;
        e.removeCount = c.lastLine - c.headerLine + 1;
        out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const TextEdit &a, const TextEdit &b) { return a.firstLine > b.firstLine; });
    return out;
}

StringList sectionText(const InfDocument &doc, const wstring &section)
{
    StringList out;
    const InfSection *s = doc.section(section);
    if (!s)
        return out;
    const InfSectionChunk &c = s->chunks.front();
    for (int i = c.headerLine; i <= c.lastContentLine; ++i)
        out.push_back(doc.physicalLine(i));
    return out;
}

TextEdit replaceSectionEdit(const InfDocument &doc, const wstring &section, const StringList &lines)
{
    const InfSection *s = doc.section(section);
    if (!s)
        return appendSectionEdit(doc, lines);
    const InfSectionChunk &c = s->chunks.front();
    TextEdit e;
    e.firstLine = c.headerLine;
    e.removeCount = c.lastContentLine - c.headerLine + 1;
    e.lines = lines;
    return e;
}

std::vector<SpanEdit> renameSectionEdits(const InfDocument &doc, const InfModel &model, const wstring &section,
                                         const wstring &newName)
{
    std::vector<SpanEdit> out;
    const InfSection *s = doc.section(section);
    if (!s)
        return out;
    for (const InfSectionChunk &c : s->chunks) {
        const InfLine &h = doc.lines()[size_t(c.logicalHeader)];
        out.push_back(SpanEdit{h.sectionName.line, h.sectionName.column, h.sectionName.length, newName});
    }
    for (const SectionRef &r : model.refsTo(s->name)) {
        if (r.implicit || r.line < 0)
            continue;
        // ссылка из [Manufacturer] на секцию моделей с декорацией указывает только на базовое имя
        const wstring text = doc.physicalLine(r.line).substr(size_t(r.column), size_t(r.length));
        if (!iequals(trim(text), s->name))
            continue;
        out.push_back(SpanEdit{r.line, r.column, r.length, newName});
    }
    return out;
}

wstring applyEdit(const wstring &text, const TextEdit &edit)
{
    StringList l = split(text, L'\n');
    const int first = std::clamp(edit.firstLine, 0, int(l.size()));
    const int count = std::min(edit.removeCount, int(l.size()) - first);
    l.erase(l.begin() + first, l.begin() + first + count);
    l.insert(l.begin() + first, edit.lines.begin(), edit.lines.end());
    return join(l, L"\n");
}

wstring applyEdits(const wstring &text, std::vector<TextEdit> edits)
{
    std::stable_sort(edits.begin(), edits.end(), [](const TextEdit &a, const TextEdit &b) { return a.firstLine > b.firstLine; });
    wstring t = text;
    for (const TextEdit &e : edits)
        t = applyEdit(t, e);
    return t;
}

wstring applySpans(const wstring &text, std::vector<SpanEdit> edits)
{
    std::sort(edits.begin(), edits.end(), [](const SpanEdit &a, const SpanEdit &b) {
        return a.line != b.line ? a.line > b.line : a.column > b.column;
    });
    StringList l = split(text, L'\n');
    for (const SpanEdit &e : edits) {
        if (e.line < 0 || e.line >= int(l.size()))
            continue;
        wstring &line = l[size_t(e.line)];
        if (size_t(e.column) > line.size())
            continue;
        line.replace(size_t(e.column), size_t(e.length), e.text);
    }
    return join(l, L"\n");
}

} // namespace inf
