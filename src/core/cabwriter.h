#pragma once

// Создание CAB-архива средствами Cabinet API (FCI) из cabinet.dll.

#include "infstr.h"

#include <vector>

namespace inf {

struct CabEntry {
    wstring source;     // путь к файлу на диске
    wstring name;       // имя внутри архива (может содержать поддиректории через '\')
};

// Сжатие LZX; при ошибке возвращает false и текст ошибки
bool writeCab(const wstring &cabPath, const std::vector<CabEntry> &entries, wstring *error = nullptr);

} // namespace inf
