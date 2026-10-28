#pragma once

// Устройства компьютера и пакеты драйверов в хранилище драйверов (Driver Store):
// перечисление через SetupAPI, определение сторонних пакетов, экспорт пакета в CAB.

#include "inffile.h"
#include "infstr.h"

#include <vector>

namespace inf {

struct DeviceInfo {
    wstring instanceId;
    wstring description;    // понятное имя или описание устройства
    wstring className;
    wstring classGuid;      // {xxxxxxxx-...} в нижнем регистре
    wstring classDescription;
    StringList hardwareIds;
    StringList compatibleIds;
    wstring manufacturer;
    wstring driverProvider;
    wstring driverVersion;
    wstring driverDate;     // мм/дд/гггг
    wstring infName;        // опубликованное имя INF: oem12.inf или usbport.inf
    wstring infSection;
    wstring matchingId;     // идентификатор, по которому выбран драйвер
    unsigned long problem = 0;
    bool hasProblem = false;
    bool present = true;    // устройство подключено
    bool hidden = false;    // скрыто в Диспетчере устройств (флаг DN_NO_SHOW_IN_DM)

    // Сторонний пакет: INF опубликован под именем oemNN.inf
    bool thirdParty() const;
};

struct DriverPackage {
    wstring publishedName;  // oem12.inf
    wstring publishedPath;  // C:\Windows\INF\oem12.inf
    wstring storeInf;       // путь к INF в Driver Store
    wstring storeDir;       // директория пакета в Driver Store
    wstring originalName;   // исходное имя INF
    wstring provider;
    wstring className;
    wstring driverVer;      // как в DriverVer после подстановки строк
    wstring catalog;
    int devices = 0;        // сколько присутствующих устройств используют пакет
};

bool isThirdPartyInf(const wstring &infName);

// Устройства, как в Диспетчере устройств. includeHidden = false — только подключённые и не скрытые;
// true — также отключённые и скрытые («Вид → Показать скрытые устройства»)
std::vector<DeviceInfo> enumerateDevices(bool includeHidden = false, wstring *error = nullptr);

// Пакеты драйверов; thirdPartyOnly — только oemNN.inf
std::vector<DriverPackage> enumerateDriverPackages(const std::vector<DeviceInfo> &devices, bool thirdPartyOnly);

// Сведения о пакете по опубликованному имени INF (oem12.inf или usbport.inf)
bool driverPackage(const wstring &publishedName, DriverPackage &out, wstring *error = nullptr);

// Путь к директории INF Windows (%SystemRoot%\INF) с завершающей косой чертой
wstring windowsInfDir();

// ------------------------------------------------------------------ экспорт

struct ExportFile {
    wstring source;     // файл на диске
    wstring nameInCab;  // имя в архиве
};

struct ExportPlan {
    wstring infName;            // имя INF в архиве
    wstring infText;            // текст INF для архива
    FileFormat infFormat;
    bool infChanged = false;    // пути в INF исправлены
    std::vector<ExportFile> files;  // все файлы, кроме INF
    StringList warnings;
    StringList errors;
};

// Исправить пути в INF так, чтобы все исходные файлы брались из корня пакета:
// очищаются пути в [SourceDisksNames*] и поддиректории в [SourceDisksFiles*]
wstring flattenInfPaths(const wstring &text, int *changedLines = nullptr);

// План экспорта пакета из директории с INF. flatten = true — все файлы в корень архива
// и исправленные пути в INF; false — пакет целиком с исходной структурой директорий
ExportPlan planExport(const wstring &infPath, bool flatten);

// Записать план в CAB-архив
bool exportToCab(const ExportPlan &plan, const wstring &cabPath, wstring *error = nullptr);

// Имя CAB для пакета: исходное имя INF, версия драйвера
wstring suggestedCabName(const DriverPackage &p);

} // namespace inf
