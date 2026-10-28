#pragma once

// Генерация нового INF-файла по параметрам мастера (аналог GenInf из DDK).

#include "infstr.h"

#include <vector>

namespace inf {

enum class DriverKind {
    Kmdf,       // драйвер KMDF
    Umdf,       // драйвер UMDF 2
    Wdm,        // драйвер WDM без WDF
    WinUsb,     // устройство USB с системным драйвером WinUSB
    NullDriver, // устройство без драйвера («пустой» драйвер)
    Extension,  // extension INF
    NonPnp      // установка службы без Plug and Play через DefaultInstall
};

constexpr int kDriverKindCount = 7;

wstring driverKindName(DriverKind kind);
wstring driverKindHelp(DriverKind kind);

struct GeneratorDevice {
    wstring description;
    wstring hardwareId;
    StringList compatibleIds;
};

struct GeneratorParams {
    DriverKind kind = DriverKind::Kmdf;

    wstring infName = L"mydriver.inf";
    wstring provider;
    wstring manufacturer;
    wstring className;
    wstring classGuid;
    bool customClass = false;          // добавить [ClassInstall32] для нового класса
    wstring customClassDescription;
    wstring extensionId;               // для Extension

    Date date = Date::today();
    wstring version = L"1.0.0.0";
    wstring catalogFile;               // пусто — имя INF с расширением .cat
    bool pnpLockdown = true;

    StringList architectures = {L"amd64"};
    wstring targetOsVersion;           // например 10.0...16299, дописывается к декорации

    std::vector<GeneratorDevice> devices;

    wstring serviceName = L"MyDriver";
    wstring serviceDescription;
    wstring binaryName = L"mydriver.sys";
    StringList extraFiles;
    int startType = 3;
    int errorControl = 1;
    int destinationDirId = 13;

    wstring kmdfVersion = L"1.15";
    wstring umdfVersion = L"2.15.0";
    wstring deviceInterfaceGuid;       // для WinUSB
    bool comments = true;              // поясняющие комментарии в тексте
};

wstring generateInf(const GeneratorParams &p);

// Идентификатор для имён секций и ключей строк: только латинские буквы, цифры и '_'
wstring toIdentifier(const wstring &text, const wstring &fallback = L"Item");

} // namespace inf
