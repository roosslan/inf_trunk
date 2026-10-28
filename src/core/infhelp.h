#pragma once

// Текстовые описания элементов INF для окон свойств и панели описания:
// директива записи, пояснения значений, подстановки строк, связи секции.

#include "infanalysis.h"

namespace inf {

// Описание логической строки-записи: директива, значения, подстановки
wstring describeEntry(const Analysis &a, int logicalLine);

// Описание секции: роль, где используется, на что ссылается
wstring describeSection(const Analysis &a, const wstring &section);

// Имя секции, к которой относится логическая строка (пусто вне секций)
wstring sectionOfLine(const Analysis &a, int logicalLine);

} // namespace inf
