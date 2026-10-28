#include "infstr.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <cwctype>

namespace inf {

wstring lower(const wstring &s)
{
    if (s.empty())
        return s;
    wstring out = s;
    CharLowerBuffW(out.data(), static_cast<DWORD>(out.size()));
    return out;
}

bool iequals(const wstring &a, const wstring &b)
{
    if (a.size() != b.size())
        return false;
    return CompareStringOrdinal(a.data(), int(a.size()), b.data(), int(b.size()), TRUE) == CSTR_EQUAL;
}

bool istartsWith(const wstring &s, const wstring &prefix)
{
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

bool iendsWith(const wstring &s, const wstring &suffix)
{
    return s.size() >= suffix.size() && iequals(s.substr(s.size() - suffix.size()), suffix);
}

bool icontains(const StringList &list, const wstring &value)
{
    return std::any_of(list.begin(), list.end(), [&](const wstring &x) { return iequals(x, value); });
}

bool contains(const wstring &s, wchar_t c)
{
    return s.find(c) != wstring::npos;
}

bool contains(const wstring &s, const wstring &part)
{
    return s.find(part) != wstring::npos;
}

bool isSpace(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\v' || c == L'\f' || c == 0xA0
           || c == 0xFEFF || std::iswspace(c);
}

wstring trim(const wstring &s)
{
    size_t b = 0, e = s.size();
    while (b < e && isSpace(s[b]))
        ++b;
    while (e > b && isSpace(s[e - 1]))
        --e;
    return s.substr(b, e - b);
}

StringList split(const wstring &s, wchar_t sep, bool skipEmpty)
{
    StringList out;
    size_t start = 0;
    while (true) {
        const size_t pos = s.find(sep, start);
        const wstring part = s.substr(start, pos == wstring::npos ? wstring::npos : pos - start);
        if (!skipEmpty || !part.empty())
            out.push_back(part);
        if (pos == wstring::npos)
            break;
        start = pos + 1;
    }
    return out;
}

wstring join(const StringList &list, const wstring &sep)
{
    wstring out;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i)
            out += sep;
        out += list[i];
    }
    return out;
}

wstring replaceAll(wstring s, const wstring &from, const wstring &to)
{
    if (from.empty())
        return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != wstring::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

static bool parseDigits(const wstring &t, int base, uint32_t &out)
{
    if (t.empty() || t.size() > 10)
        return false;
    uint64_t v = 0;
    for (wchar_t c : t) {
        int d;
        if (c >= L'0' && c <= L'9')
            d = c - L'0';
        else if (base == 16 && c >= L'a' && c <= L'f')
            d = c - L'a' + 10;
        else if (base == 16 && c >= L'A' && c <= L'F')
            d = c - L'A' + 10;
        else
            return false;
        v = v * base + d;
        if (v > 0xFFFFFFFFull)
            return false;
    }
    out = uint32_t(v);
    return true;
}

bool parseNumber(const wstring &text, uint32_t &out)
{
    const wstring t = trim(text);
    if (istartsWith(t, L"0x"))
        return parseDigits(t.substr(2), 16, out);
    return parseDigits(t, 10, out);
}

bool parseUInt(const wstring &text, uint32_t &out)
{
    return parseDigits(trim(text), 10, out);
}

int toInt(const wstring &text, int fallback)
{
    const wstring t = trim(text);
    if (t.empty())
        return fallback;
    bool neg = t[0] == L'-';
    uint32_t v = 0;
    if (!parseDigits(neg ? t.substr(1) : t, 10, v))
        return fallback;
    return neg ? -int(v) : int(v);
}

wstring htmlEscape(const wstring &s)
{
    wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        switch (c) {
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'&': out += L"&amp;"; break;
        case L'"': out += L"&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

// ------------------------------------------------------------------ Date

static bool isLeap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int daysInMonth(int y, int m)
{
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && isLeap(y) ? 29 : d[m - 1];
}

Date Date::today()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    return Date{st.wYear, st.wMonth, st.wDay};
}

bool Date::isValid() const
{
    return year >= 1 && month >= 1 && month <= 12 && day >= 1 && day <= daysInMonth(year, month);
}

Date Date::addDays(int days) const
{
    Date d = *this;
    while (days > 0) {
        if (++d.day > daysInMonth(d.year, d.month)) {
            d.day = 1;
            if (++d.month > 12) {
                d.month = 1;
                ++d.year;
            }
        }
        --days;
    }
    while (days < 0) {
        if (--d.day < 1) {
            if (--d.month < 1) {
                d.month = 12;
                --d.year;
            }
            d.day = daysInMonth(d.year, d.month);
        }
        ++days;
    }
    return d;
}

bool Date::operator<(const Date &o) const
{
    if (year != o.year)
        return year < o.year;
    if (month != o.month)
        return month < o.month;
    return day < o.day;
}

wstring Date::toInf() const
{
    wchar_t buf[16];
    swprintf_s(buf, L"%02d/%02d/%04d", month, day, year);
    return buf;
}

Date Date::fromInf(const wstring &text)
{
    const StringList p = split(trim(text), L'/');
    Date d;
    if (p.size() != 3)
        return d;
    uint32_t m = 0, dd = 0, y = 0;
    if (p[0].empty() || p[0].size() > 2 || p[1].empty() || p[1].size() > 2 || p[2].size() != 4)
        return d;
    if (!parseUInt(p[0], m) || !parseUInt(p[1], dd) || !parseUInt(p[2], y))
        return d;
    d.year = int(y);
    d.month = int(m);
    d.day = int(dd);
    return d;
}

wstring newGuid()
{
    GUID g{};
    CoCreateGuid(&g);
    wchar_t buf[64];
    StringFromGUID2(g, buf, 64);
    return lower(buf);
}

wstring fileName(const wstring &path)
{
    const size_t pos = path.find_last_of(L"\\/");
    return pos == wstring::npos ? path : path.substr(pos + 1);
}

wstring baseName(const wstring &path)
{
    const wstring f = fileName(path);
    const size_t dot = f.rfind(L'.');
    return dot == wstring::npos ? f : f.substr(0, dot);
}

std::string toUtf8(const wstring &s)
{
    if (s.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

wstring fromUtf8(const std::string &s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}

} // namespace inf
