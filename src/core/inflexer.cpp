#include "inflexer.h"

namespace inf {

static void addToken(LexResult &r, TokenKind kind, int start, int length)
{
    r.tokens.push_back(Token{kind, start, length});
}

LexResult lexLine(const wstring &line, bool continued)
{
    LexResult r;
    const int n = int(line.size());
    int i = 0;
    auto skipSpaces = [&] {
        while (i < n && isSpace(line[i]))
            ++i;
    };

    skipSpaces();
    if (i >= n)
        return r;

    // Заголовок секции
    if (!continued && line[i] == L'[') {
        const size_t closePos = line.find(L']', size_t(i) + 1);
        if (closePos == wstring::npos) {
            addToken(r, TokenKind::Error, i, n - i);
            r.error = L"Не закрыта квадратная скобка в заголовке секции";
            return r;
        }
        const int close = int(closePos);
        addToken(r, TokenKind::SectionHeader, i, close - i + 1);
        i = close + 1;
        skipSpaces();
        if (i < n) {
            if (line[i] == L';') {
                addToken(r, TokenKind::Comment, i, n - i);
            } else {
                const size_t semi = line.find(L';', size_t(i));
                const int end = semi == wstring::npos ? n : int(semi);
                addToken(r, TokenKind::Error, i, end - i);
                r.error = L"Лишние символы после заголовка секции";
                if (semi != wstring::npos)
                    addToken(r, TokenKind::Comment, int(semi), n - int(semi));
            }
        }
        return r;
    }

    // Запись: [ключ =] значение[, значение...]
    bool keyAllowed = !continued;
    while (i < n) {
        const wchar_t c = line[i];
        if (isSpace(c)) {
            ++i;
            continue;
        }
        if (c == L';') {
            addToken(r, TokenKind::Comment, i, n - i);
            break;
        }
        if (c == L',') {
            addToken(r, TokenKind::Comma, i, 1);
            keyAllowed = false;
            ++i;
            continue;
        }
        if (c == L'=' && keyAllowed) {
            // всё, что было до '=', становится ключом
            for (Token &t : r.tokens)
                if (t.kind == TokenKind::Value || t.kind == TokenKind::QuotedString)
                    t.kind = TokenKind::Key;
            addToken(r, TokenKind::Equals, i, 1);
            keyAllowed = false;
            ++i;
            continue;
        }
        if (c == L'"') {
            int j = i + 1;
            while (j < n) {
                if (line[j] == L'"') {
                    if (j + 1 < n && line[j + 1] == L'"') {
                        j += 2;
                        continue;
                    }
                    break;
                }
                ++j;
            }
            if (j >= n) {
                addToken(r, TokenKind::Error, i, n - i);
                r.unterminatedQuote = true;
                r.error = L"Не закрыта кавычка";
                break;
            }
            addToken(r, TokenKind::QuotedString, i, j - i + 1);
            i = j + 1;
            continue;
        }

        // значение без кавычек: до кавычки, запятой, комментария или '='
        int j = i;
        while (j < n) {
            const wchar_t d = line[j];
            if (d == L'"' || d == L',' || d == L';')
                break;
            if (d == L'=' && keyAllowed)
                break;
            ++j;
        }
        int end = j;
        while (end > i && isSpace(line[end - 1]))
            --end;

        // '\' последним символом строки (возможно, перед комментарием) означает продолжение
        if ((j == n || line[j] == L';') && end > i && line[end - 1] == L'\\') {
            const int bs = end - 1;
            int valueEnd = bs;
            while (valueEnd > i && isSpace(line[valueEnd - 1]))
                --valueEnd;
            if (valueEnd > i)
                addToken(r, TokenKind::Value, i, valueEnd - i);
            addToken(r, TokenKind::Continuation, bs, 1);
            if (j < n)
                addToken(r, TokenKind::Comment, j, n - j);
            r.continues = true;
            break;
        }

        addToken(r, TokenKind::Value, i, end - i);
        i = j;
    }
    return r;
}

wstring unquote(const wstring &raw)
{
    if (raw.find(L'"') == wstring::npos)
        return trim(raw);

    wstring out;
    out.reserve(raw.size());
    bool inQuotes = false;
    const size_t n = raw.size();
    for (size_t i = 0; i < n; ++i) {
        const wchar_t c = raw[i];
        if (c == L'"') {
            if (inQuotes && i + 1 < n && raw[i + 1] == L'"') {
                out += L'"';
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
            continue;
        }
        out += c;
    }
    return out;
}

wstring quote(const wstring &value)
{
    return L"\"" + replaceAll(value, L"\"", L"\"\"") + L"\"";
}

wstring quoteIfNeeded(const wstring &value)
{
    if (value.empty())
        return value;
    bool need = isSpace(value.front()) || isSpace(value.back()) || value.back() == L'\\' || value.front() == L'[';
    for (wchar_t c : value) {
        if (c == L',' || c == L';' || c == L'=' || c == L'"') {
            need = true;
            break;
        }
    }
    return need ? quote(value) : value;
}

} // namespace inf
