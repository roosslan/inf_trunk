#pragma once

// Поиск InfVerif.exe из WDK, запуск и разбор его вывода в диагностические сообщения.

#include "infvalidator.h"

#include <vector>

namespace inf {

// Путь к InfVerif.exe из последней установленной версии Windows Kits; пусто, если не найден
wstring findInfVerif();

// Режимы проверки InfVerif
struct InfVerifMode {
    wstring key;        // /w, /u, /h, /k или пусто
    wstring title;
};
const std::vector<InfVerifMode> &infVerifModes();

StringList infVerifArguments(const wstring &modeKey, const wstring &file);

struct InfVerifResult {
    bool started = false;
    bool valid = false;
    unsigned long exitCode = 0;
    wstring commandLine;
    wstring output;
    wstring error;               // причина, по которой процесс не запустился
    std::vector<Diagnostic> diagnostics;
};

// Запустить InfVerif для файла и дождаться завершения
InfVerifResult runInfVerif(const wstring &tool, const wstring &modeKey, const wstring &file, unsigned long timeoutMs = 60000);

// Разбор вывода: строки вида "ERROR(1233) in путь, line 1: текст"
std::vector<Diagnostic> parseInfVerifOutput(const wstring &output, bool *valid = nullptr);

} // namespace inf
