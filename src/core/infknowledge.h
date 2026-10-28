#pragma once

// Справочные данные о синтаксисе INF: стандартные классы устройств,
// идентификаторы директорий (DirID), директивы и их константы.

#include "infstr.h"

#include <vector>

namespace inf {

struct DeviceClassInfo {
    wstring name;
    wstring guid;           // в фигурных скобках, нижний регистр
    wstring description;
};

struct DirIdInfo {
    int id;
    wstring description;
};

struct DirectiveInfo {
    wstring name;
    wstring where;          // в каких секциях применяется
    wstring syntax;
    wstring help;
};

const std::vector<DeviceClassInfo> &deviceClasses();
const DeviceClassInfo *findClassByName(const wstring &name);
const DeviceClassInfo *findClassByGuid(const wstring &guid);

const std::vector<DirIdInfo> &dirIds();
wstring dirIdDescription(int id);

const std::vector<DirectiveInfo> &directives();
const DirectiveInfo *findDirective(const wstring &name);

// Стандартные секции, которые не обязаны иметь ссылки на себя
StringList standardSectionNames();
bool isStandardSection(const wstring &name);
wstring standardSectionHelp(const wstring &name);

// Пояснение к значению известной директивы (ServiceType, StartType, AddReg-флаги и т. п.)
wstring explainValue(const wstring &directive, const wstring &value);
wstring explainAddRegFlags(uint32_t flags);

// Проверка формата GUID {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
bool isGuid(const wstring &text);

// Известные архитектуры для декораций NT<arch>
StringList knownArchitectures();

} // namespace inf
