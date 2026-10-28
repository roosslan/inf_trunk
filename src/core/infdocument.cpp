#include "infdocument.h"

#include "inflexer.h"

namespace inf {

wstring sectionKey(const wstring &name)
{
    return lower(trim(name));
}

namespace {

// Накопитель значения, которое может состоять из нескольких фрагментов
struct ValueBuilder {
    InfValue value;
    bool started = false;

    void add(const wstring &line, int lineNo, const Token &t)
    {
        const wstring seg = line.substr(size_t(t.start), size_t(t.length));
        const bool isQuoted = t.kind == TokenKind::QuotedString;
        if (!started) {
            value.line = lineNo;
            value.column = t.start;
            value.length = t.length;
            value.raw = seg;
        } else if (value.line == lineNo) {
            value.length = t.start + t.length - value.column;
            value.raw = line.substr(size_t(value.column), size_t(value.length));
        } else {
            value.raw += L' ' + seg;
        }
        value.text += isQuoted ? unquote(seg) : seg;
        value.quoted = value.quoted || isQuoted;
        started = true;
    }
};

} // namespace

InfDocument InfDocument::parse(const wstring &text)
{
    InfDocument doc;
    doc.m_physical = split(text, L'\n');
    const int count = int(doc.m_physical.size());
    doc.m_physToLogical.assign(size_t(count), 0);

    int currentSection = -1;
    InfLine entry;
    bool inEntry = false;
    ValueBuilder key;
    ValueBuilder value;
    bool valuePending = false;  // после запятой значение есть, даже если пустое

    auto chunkContent = [&](int lastLine) {
        if (currentSection >= 0)
            doc.m_sections[size_t(currentSection)].chunks.back().lastContentLine = lastLine;
    };

    auto finishEntry = [&](int lastLine) {
        if (key.started)
            entry.key = key.value;
        if (value.started || valuePending)
            entry.values.push_back(value.value);
        entry.lastLine = lastLine;
        entry.section = currentSection;
        const int idx = int(doc.m_lines.size());
        doc.m_lines.push_back(entry);
        for (int l = entry.firstLine; l <= lastLine; ++l)
            doc.m_physToLogical[size_t(l)] = idx;
        if (currentSection >= 0)
            doc.m_sections[size_t(currentSection)].entries.push_back(idx);
        else
            doc.m_outside.push_back(idx);
        chunkContent(lastLine);
        inEntry = false;
    };

    auto setError = [](InfLine &l, const wstring &msg, int line, int col, int len) {
        if (!l.error.empty())
            return;
        l.error = msg;
        l.errorLine = line;
        l.errorColumn = col;
        l.errorLength = len;
    };

    for (int ln = 0; ln < count; ++ln) {
        const wstring &line = doc.m_physical[size_t(ln)];
        const LexResult lex = lexLine(line, inEntry);

        if (!inEntry) {
            const bool blank = lex.tokens.empty();
            const bool commentOnly = lex.tokens.size() == 1 && lex.tokens.front().kind == TokenKind::Comment;
            if (blank || commentOnly) {
                InfLine l;
                l.kind = blank ? InfLine::Kind::Blank : InfLine::Kind::Comment;
                l.firstLine = l.lastLine = ln;
                l.section = currentSection;
                if (commentOnly) {
                    const Token &t = lex.tokens.front();
                    l.comment = line.substr(size_t(t.start), size_t(t.length));
                    chunkContent(ln);
                }
                doc.m_physToLogical[size_t(ln)] = int(doc.m_lines.size());
                doc.m_lines.push_back(l);
                continue;
            }

            const Token &first = lex.tokens.front();
            if (first.kind == TokenKind::SectionHeader
                || (first.kind == TokenKind::Error && trim(line).front() == L'[')) {
                InfLine l;
                l.kind = InfLine::Kind::Section;
                l.firstLine = l.lastLine = ln;
                wstring name;
                if (first.kind == TokenKind::SectionHeader) {
                    const wstring inner = line.substr(size_t(first.start) + 1, size_t(first.length) - 2);
                    name = trim(inner);
                    int lead = 0;
                    while (lead < int(inner.size()) && isSpace(inner[size_t(lead)]))
                        ++lead;
                    l.sectionName.text = name;
                    l.sectionName.raw = inner;
                    l.sectionName.line = ln;
                    l.sectionName.column = first.start + 1 + lead;
                    l.sectionName.length = int(name.size());
                } else {
                    name = trim(line.substr(size_t(first.start) + 1));
                    l.sectionName.text = name;
                    l.sectionName.line = ln;
                    l.sectionName.column = first.start + 1;
                    l.sectionName.length = int(name.size());
                }
                for (const Token &t : lex.tokens) {
                    if (t.kind == TokenKind::Comment && l.comment.empty())
                        l.comment = line.substr(size_t(t.start), size_t(t.length));
                    if (t.kind == TokenKind::Error)
                        setError(l, lex.error, ln, t.start, t.length);
                }
                if (name.empty())
                    setError(l, L"Пустое имя секции", ln, first.start, first.length);

                // закрыть предыдущий фрагмент
                if (currentSection >= 0)
                    doc.m_sections[size_t(currentSection)].chunks.back().lastLine = ln - 1;

                const wstring k = sectionKey(name);
                int idx;
                auto it = doc.m_index.find(k);
                if (it == doc.m_index.end()) {
                    idx = int(doc.m_sections.size());
                    InfSection s;
                    s.name = name;
                    doc.m_sections.push_back(s);
                    doc.m_index.emplace(k, idx);
                } else {
                    idx = it->second;
                }
                InfSectionChunk chunk;
                chunk.headerLine = ln;
                chunk.lastLine = ln;
                chunk.lastContentLine = ln;
                chunk.logicalHeader = int(doc.m_lines.size());
                doc.m_sections[size_t(idx)].chunks.push_back(chunk);
                currentSection = idx;

                l.section = idx;
                doc.m_physToLogical[size_t(ln)] = int(doc.m_lines.size());
                doc.m_lines.push_back(l);
                continue;
            }

            // начало записи
            entry = InfLine();
            entry.kind = InfLine::Kind::Entry;
            entry.firstLine = ln;
            key = ValueBuilder();
            value = ValueBuilder();
            valuePending = false;
            inEntry = true;
        }

        for (const Token &t : lex.tokens) {
            switch (t.kind) {
            case TokenKind::Key:
                key.add(line, ln, t);
                break;
            case TokenKind::Equals:
                entry.hasKey = true;
                break;
            case TokenKind::Value:
            case TokenKind::QuotedString:
                value.add(line, ln, t);
                break;
            case TokenKind::Comma:
                if (!value.started) {
                    value.value.line = ln;
                    value.value.column = t.start;
                    value.value.length = 0;
                }
                entry.values.push_back(value.value);
                value = ValueBuilder();
                value.value.line = ln;
                value.value.column = t.start + 1;
                valuePending = true;
                break;
            case TokenKind::Comment:
                if (entry.comment.empty())
                    entry.comment = line.substr(size_t(t.start), size_t(t.length));
                break;
            case TokenKind::Error:
                setError(entry, lex.error, ln, t.start, t.length);
                break;
            case TokenKind::Continuation:
            case TokenKind::SectionHeader:
                break;
            }
        }

        if (!lex.continues) {
            finishEntry(ln);
        } else if (ln == count - 1) {
            setError(entry, L"Символ продолжения строки '\\' в конце файла", ln, 0, int(line.size()));
            finishEntry(ln);
        }
    }

    if (currentSection >= 0)
        doc.m_sections[size_t(currentSection)].chunks.back().lastLine = count - 1;

    return doc;
}

int InfDocument::sectionIndex(const wstring &name) const
{
    auto it = m_index.find(sectionKey(name));
    return it == m_index.end() ? -1 : it->second;
}

const InfSection *InfDocument::section(const wstring &name) const
{
    const int idx = sectionIndex(name);
    return idx >= 0 ? &m_sections[size_t(idx)] : nullptr;
}

std::vector<const InfLine *> InfDocument::entries(const wstring &sectionName, const wstring &key) const
{
    std::vector<const InfLine *> out;
    const InfSection *s = section(sectionName);
    if (!s)
        return out;
    for (int idx : s->entries) {
        const InfLine &l = m_lines[size_t(idx)];
        if (l.hasKey && iequals(l.key.text, key))
            out.push_back(&l);
    }
    return out;
}

const InfLine *InfDocument::entry(const wstring &sectionName, const wstring &key) const
{
    const auto list = entries(sectionName, key);
    return list.empty() ? nullptr : list.front();
}

std::vector<const InfLine *> InfDocument::entries(const InfSection &s) const
{
    std::vector<const InfLine *> out;
    out.reserve(s.entries.size());
    for (int idx : s.entries)
        out.push_back(&m_lines[size_t(idx)]);
    return out;
}

wstring InfDocument::value(const wstring &sectionName, const wstring &key, int index) const
{
    const InfLine *l = entry(sectionName, key);
    return l ? l->value(index) : wstring();
}

StringList InfDocument::sectionsWithPrefix(const wstring &prefix) const
{
    StringList out;
    const wstring p = lower(prefix);
    for (const InfSection &s : m_sections) {
        const wstring k = lower(s.name);
        if (k == p || (k.size() > p.size() && k.compare(0, p.size(), p) == 0 && k[p.size()] == L'.'))
            out.push_back(s.name);
    }
    return out;
}

int InfDocument::logicalLineAt(int physicalLine) const
{
    if (physicalLine < 0 || physicalLine >= int(m_physToLogical.size()))
        return -1;
    return m_physToLogical[size_t(physicalLine)];
}

wstring InfDocument::lineText(int logicalLine) const
{
    if (logicalLine < 0 || logicalLine >= int(m_lines.size()))
        return {};
    const InfLine &l = m_lines[size_t(logicalLine)];
    StringList parts;
    for (int i = l.firstLine; i <= l.lastLine; ++i)
        parts.push_back(physicalLine(i));
    return join(parts, L"\n");
}

} // namespace inf
