#pragma once

// Строковые утилиты ядра: регистронезависимое сравнение, обрезка, разбиение,
// разбор чисел. Строки — UTF-16 (std::wstring), как в Windows API.

#include <cstdint>
#include <string>
#include <vector>

namespace inf {

using std::wstring;
using StringList = std::vector<wstring>;

wstring lower(const wstring &s);
bool iequals(const wstring &a, const wstring &b);
bool istartsWith(const wstring &s, const wstring &prefix);
bool iendsWith(const wstring &s, const wstring &suffix);
bool icontains(const StringList &list, const wstring &value);
bool contains(const wstring &s, wchar_t c);
bool contains(const wstring &s, const wstring &part);

bool isSpace(wchar_t c);
wstring trim(const wstring &s);

StringList split(const wstring &s, wchar_t sep, bool skipEmpty = false);
wstring join(const StringList &list, const wstring &sep);
wstring replaceAll(wstring s, const wstring &from, const wstring &to);

// Число в записи INF: десятичное или шестнадцатеричное (0x...)
bool parseNumber(const wstring &text, uint32_t &out);
// Только десятичное целое без знака
bool parseUInt(const wstring &text, uint32_t &out);
int toInt(const wstring &text, int fallback = 0);

wstring htmlEscape(const wstring &s);

// Дата в формате INF (месяц/день/год)
struct Date {
    int year = 0;
    int month = 0;
    int day = 0;

    static Date today();
    bool isValid() const;
    Date addDays(int days) const;
    bool operator<(const Date &o) const;
    bool operator>(const Date &o) const { return o < *this; }
    wstring toInf() const;          // MM/dd/yyyy
    static Date fromInf(const wstring &text);  // M/d/yyyy, при ошибке — недействительная дата
};

// Новый GUID в виде {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} (нижний регистр)
wstring newGuid();

// Имя файла без пути и без последнего расширения
wstring fileName(const wstring &path);
wstring baseName(const wstring &path);

// Преобразования UTF-8 <-> UTF-16 (для отладочного вывода и тестов)
std::string toUtf8(const wstring &s);
wstring fromUtf8(const std::string &s);

} // namespace inf
