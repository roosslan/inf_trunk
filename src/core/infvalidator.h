#pragma once

// Проверка INF-файла: синтаксис, обязательные секции и директивы,
// ссылки между секциями, строковые константы, файлы и службы.

#include "infdocument.h"
#include "infmodel.h"

#include <vector>

namespace inf {

enum class Severity { Error, Warning, Info };

struct Diagnostic {
    Severity severity = Severity::Error;
    int line = 0;           // физическая строка (с нуля)
    int column = 0;
    int length = 0;
    wstring message;
    wstring code;           // короткий код проверки, например V002
    wstring source = L"INF Editor";
};

wstring severityName(Severity s);

std::vector<Diagnostic> validate(const InfDocument &doc, const InfModel &model);

} // namespace inf
