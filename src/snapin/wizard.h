// Мастер создания нового INF-файла (по мотивам GenInf из Windows DDK)
#pragma once

#include "infgenerator.h"

#include <string>

// Возвращает true и созданный текст, если мастер завершён кнопкой «Готово».
// initial — начальные значения (например, данные выбранного устройства) или nullptr
bool runNewInfWizard(CWnd *parent, std::wstring &text, std::wstring &fileName, const inf::GeneratorParams *initial);
