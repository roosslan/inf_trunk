#include "infknowledge.h"

#include <cwctype>
#include <utility>

namespace inf {

const std::vector<DeviceClassInfo> &deviceClasses()
{
    static const std::vector<DeviceClassInfo> list = {
        {L"1394", L"{6bdd1fc1-810f-11d0-bec7-08002be2092f}", L"Хост-контроллеры IEEE 1394"},
        {L"61883", L"{7ebefbc0-3200-11d2-b4c2-00a0c9697d07}", L"Устройства IEC 61883"},
        {L"AudioEndpoint", L"{c166523c-fe0c-4a94-a586-f1a80cfbbf3e}", L"Аудиовходы и аудиовыходы"},
        {L"AudioProcessingObject", L"{5989fce8-9cd0-467d-8a6a-5419e31529d4}", L"Объекты обработки звука (APO)"},
        {L"AVC", L"{c06ff265-ae09-48f0-812c-16753d7cba83}", L"Устройства IEEE 1394 AV/C"},
        {L"Battery", L"{72631e54-78a4-11d0-bcf7-00aa00b7b32a}", L"Батареи и ИБП"},
        {L"Biometric", L"{53d29ef7-377c-4d14-864b-eb3a85769359}", L"Биометрические устройства"},
        {L"Bluetooth", L"{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}", L"Устройства Bluetooth"},
        {L"Camera", L"{ca3e7ab9-b4c3-4ae6-8251-579ef933890f}", L"Камеры"},
        {L"CDROM", L"{4d36e965-e325-11ce-bfc1-08002be10318}", L"Дисководы CD/DVD"},
        {L"Computer", L"{4d36e966-e325-11ce-bfc1-08002be10318}", L"Компьютер"},
        {L"DiskDrive", L"{4d36e967-e325-11ce-bfc1-08002be10318}", L"Дисковые устройства"},
        {L"Display", L"{4d36e968-e325-11ce-bfc1-08002be10318}", L"Видеоадаптеры"},
        {L"Dot4", L"{48721b56-6795-11d2-b1a8-0080c72e74a2}", L"Устройства IEEE 1284.4"},
        {L"Dot4Print", L"{49ce6ac8-6f86-11d2-b1e5-0080c72e74a2}", L"Функции печати IEEE 1284.4"},
        {L"Extension", L"{e2f84ce7-8efa-411c-aa69-97454ca4cb57}", L"Расширения (extension INF)"},
        {L"FDC", L"{4d36e969-e325-11ce-bfc1-08002be10318}", L"Контроллеры гибких дисков"},
        {L"Firmware", L"{f2e7dd72-6468-4e36-b6f1-6488f42c1b52}", L"Встроенное ПО (обновление прошивки)"},
        {L"FloppyDisk", L"{4d36e980-e325-11ce-bfc1-08002be10318}", L"Дисководы гибких дисков"},
        {L"HDC", L"{4d36e96a-e325-11ce-bfc1-08002be10318}", L"Контроллеры IDE ATA/ATAPI"},
        {L"HIDClass", L"{745a17a0-74d3-11d0-b6fe-00a0c90f57da}", L"Устройства HID"},
        {L"Image", L"{6bdd1fc6-810f-11d0-bec7-08002be2092f}", L"Устройства обработки изображений"},
        {L"Infrared", L"{6bdd1fc5-810f-11d0-bec7-08002be2092f}", L"Инфракрасные устройства"},
        {L"Keyboard", L"{4d36e96b-e325-11ce-bfc1-08002be10318}", L"Клавиатуры"},
        {L"LegacyDriver", L"{8ecc055d-047f-11d1-a537-0000f8753ed1}", L"Драйверы устройств не Plug and Play"},
        {L"Media", L"{4d36e96c-e325-11ce-bfc1-08002be10318}", L"Звуковые, видео и игровые устройства"},
        {L"MediumChanger", L"{ce5939ae-ebde-11d0-b181-0000f8753ec4}", L"Устройства смены носителей"},
        {L"Modem", L"{4d36e96d-e325-11ce-bfc1-08002be10318}", L"Модемы"},
        {L"Monitor", L"{4d36e96e-e325-11ce-bfc1-08002be10318}", L"Мониторы"},
        {L"Mouse", L"{4d36e96f-e325-11ce-bfc1-08002be10318}", L"Мыши и иные указывающие устройства"},
        {L"MTD", L"{4d36e970-e325-11ce-bfc1-08002be10318}", L"Устройства памяти (MTD)"},
        {L"Multifunction", L"{4d36e971-e325-11ce-bfc1-08002be10318}", L"Многофункциональные устройства"},
        {L"MultiportSerial", L"{50906cb8-ba12-11d1-bf5d-0000f805f530}", L"Многопортовые последовательные адаптеры"},
        {L"Net", L"{4d36e972-e325-11ce-bfc1-08002be10318}", L"Сетевые адаптеры"},
        {L"NetClient", L"{4d36e973-e325-11ce-bfc1-08002be10318}", L"Сетевые клиенты"},
        {L"NetService", L"{4d36e974-e325-11ce-bfc1-08002be10318}", L"Сетевые службы"},
        {L"NetTrans", L"{4d36e975-e325-11ce-bfc1-08002be10318}", L"Сетевые протоколы"},
        {L"PCMCIA", L"{4d36e977-e325-11ce-bfc1-08002be10318}", L"Адаптеры PCMCIA"},
        {L"PNPPrinters", L"{4658ee7e-f050-11d1-b6bd-00c04fa372a7}", L"Принтеры на шине IEEE 1394 / USB"},
        {L"Ports", L"{4d36e978-e325-11ce-bfc1-08002be10318}", L"Порты (COM и LPT)"},
        {L"Printer", L"{4d36e979-e325-11ce-bfc1-08002be10318}", L"Принтеры"},
        {L"PrintQueue", L"{1ed2bbf9-11f0-4084-b21f-ad83a8e6dcdc}", L"Очереди печати"},
        {L"Processor", L"{50127dc3-0f36-415e-a6cc-4cb3be910b65}", L"Процессоры"},
        {L"SBP2", L"{d48179be-ec20-11d1-b6b8-00c04fa372a7}", L"Устройства IEEE 1394 SBP-2"},
        {L"SCSIAdapter", L"{4d36e97b-e325-11ce-bfc1-08002be10318}", L"Контроллеры запоминающих устройств"},
        {L"SecurityAccelerator", L"{268c95a1-edfe-11d3-95c3-0010dc4050a5}", L"Ускорители шифрования"},
        {L"SecurityDevices", L"{d94ee5d8-d189-4994-83d2-f68d7d41b0e6}", L"Устройства безопасности (TPM)"},
        {L"Sensor", L"{5175d334-c371-4806-b3ba-71fd53c9258d}", L"Датчики"},
        {L"SmartCard", L"{990a2bd7-e738-46c7-b26f-1cf8fb9f1391}", L"Смарт-карты"},
        {L"SmartCardReader", L"{50dd5230-ba8a-11d1-bf5d-0000f805f530}", L"Устройства чтения смарт-карт"},
        {L"SoftwareComponent", L"{5c4c3332-344d-483c-8739-259e934c9cc8}", L"Программные компоненты"},
        {L"SoftwareDevice", L"{62f9c741-b25a-46ce-b54c-9bccce08b6f2}", L"Программные устройства"},
        {L"System", L"{4d36e97d-e325-11ce-bfc1-08002be10318}", L"Системные устройства"},
        {L"TapeDrive", L"{6d807884-7d21-11cf-801c-08002be10318}", L"Ленточные накопители"},
        {L"Unknown", L"{4d36e97e-e325-11ce-bfc1-08002be10318}", L"Прочие устройства"},
        {L"USB", L"{36fc9e60-c465-11cf-8056-444553540000}", L"Контроллеры и концентраторы USB"},
        {L"USBDevice", L"{88bae032-5a81-49f0-bc3d-a4ff138216d6}", L"Устройства USB (WinUSB и прочие)"},
        {L"Volume", L"{71a27cdd-812a-11d0-bec7-00a0c91efb8b}", L"Тома запоминающих устройств"},
        {L"WCEUSBS", L"{25dbce51-6c8f-4a72-8a6d-b54c2b4fc835}", L"Устройства Windows CE USB ActiveSync"},
        {L"WPD", L"{eec5ad98-8080-425f-922a-dabf3de3f69a}", L"Переносные устройства (WPD)"},
    };
    return list;
}

const DeviceClassInfo *findClassByName(const wstring &name)
{
    for (const DeviceClassInfo &c : deviceClasses())
        if (iequals(c.name, trim(name)))
            return &c;
    return nullptr;
}

const DeviceClassInfo *findClassByGuid(const wstring &guid)
{
    wstring g = lower(trim(guid));
    if (!g.empty() && g.front() != L'{')
        g = L"{" + g + L"}";
    for (const DeviceClassInfo &c : deviceClasses())
        if (c.guid == g)
            return &c;
    return nullptr;
}

const std::vector<DirIdInfo> &dirIds()
{
    static const std::vector<DirIdInfo> list = {
        {-1, L"Абсолютный путь"},
        {1, L"Директория, из которой устанавливается INF"},
        {10, L"Директория Windows (%SystemRoot%)"},
        {11, L"System32"},
        {12, L"System32\\drivers"},
        {13, L"Хранилище драйверов (запуск из Driver Store)"},
        {17, L"Директория INF"},
        {18, L"Директория справки"},
        {20, L"Директория шрифтов"},
        {21, L"Директория средств просмотра"},
        {23, L"Директория цветовых профилей"},
        {24, L"Корень системного диска"},
        {25, L"Общая директория"},
        {30, L"Корень загрузочного диска"},
        {50, L"System (16-разрядная)"},
        {51, L"Директория очереди печати (spool)"},
        {52, L"Директория драйверов печати"},
        {53, L"Профиль пользователя"},
        {54, L"Директория загрузчика"},
        {55, L"Директория обработчиков печати"},
        {16406, L"Главное меню (все пользователи)"},
        {16407, L"Программы главного меню (все пользователи)"},
        {16408, L"Автозагрузка (все пользователи)"},
        {16409, L"Рабочий стол (все пользователи)"},
        {16415, L"Избранное (все пользователи)"},
        {16419, L"Данные приложений (все пользователи, ProgramData)"},
        {16422, L"Program Files"},
        {16425, L"SysWOW64 (System для x86)"},
        {16426, L"Program Files (x86)"},
        {16427, L"Program Files\\Common Files"},
        {16428, L"Program Files (x86)\\Common Files"},
        {16429, L"Шаблоны (все пользователи)"},
        {16430, L"Документы (все пользователи)"},
    };
    return list;
}

wstring dirIdDescription(int id)
{
    for (const DirIdInfo &d : dirIds())
        if (d.id == id)
            return d.description;
    return {};
}

const std::vector<DirectiveInfo> &directives()
{
    static const std::vector<DirectiveInfo> list = {
        // [Version]
        {L"Signature", L"Version", L"Signature=\"$Windows NT$\"",
         L"Подпись INF. Для современных Windows — \"$Windows NT$\"; \"$Chicago$\" допустима, но устарела."},
        {L"Class", L"Version", L"Class=имя-класса", L"Имя класса установки устройства. Должно соответствовать ClassGuid."},
        {L"ClassGuid", L"Version", L"ClassGuid={GUID}", L"GUID класса установки устройства. Для стандартных классов задан системой."},
        {L"ClassVer", L"Version", L"ClassVer=2.0", L"Версия класса. Используется для классов Extension и SoftwareComponent."},
        {L"Provider", L"Version", L"Provider=%ProviderName%", L"Поставщик INF-файла (обычно строка из [Strings])."},
        {L"ExtensionId", L"Version", L"ExtensionId={GUID}", L"Идентификатор extension INF. Обязателен для Class=Extension."},
        {L"CatalogFile", L"Version", L"CatalogFile[.NTarch]=имя.cat", L"Файл каталога с цифровой подписью пакета драйвера."},
        {L"DriverVer", L"Version, DDInstall", L"DriverVer=мм/дд/гггг[,w.x.y.z]",
         L"Дата и версия драйвера. Дата — в формате месяц/день/год."},
        {L"PnpLockdown", L"Version", L"PnpLockdown=1",
         L"Запрещает изменение файлов драйвера кем-либо, кроме установщика. Рекомендуется для всех пакетов."},
        {L"LayoutFile", L"Version", L"LayoutFile=layout.inf", L"Файл раскладки. Применяется только в INF, поставляемых с Windows."},
        {L"DriverPackageDisplayName", L"Version", L"DriverPackageDisplayName=%Name%", L"Отображаемое имя пакета (используется DIFx)."},
        {L"DriverPackageType", L"Version", L"DriverPackageType=PlugAndPlay",
         L"Тип пакета для DIFx: PlugAndPlay, ClassFilter, FileSystem, FileSystemFilter и др."},

        // DDInstall и общие
        {L"CopyFiles", L"DDInstall, Services, ClassInstall32", L"CopyFiles=секция-списка-файлов[,...] | @имя-файла",
         L"Копирует файлы. Каждая секция списка файлов должна иметь запись в [DestinationDirs] либо должен быть задан DefaultDestDir."},
        {L"DelFiles", L"DDInstall", L"DelFiles=секция-списка-файлов[,...]", L"Удаляет файлы. Для пакетов в Driver Store не рекомендуется."},
        {L"RenFiles", L"DDInstall", L"RenFiles=секция-переименования[,...]", L"Переименовывает файлы."},
        {L"AddReg", L"DDInstall, DDInstall.HW, Services, ClassInstall32", L"AddReg=секция-добавления-в-реестр[,...]",
         L"Добавляет ключи и значения реестра. Строки секции: корень,[подключ],[имя],[флаги],[значение]."},
        {L"DelReg", L"DDInstall, DDInstall.HW, Services", L"DelReg=секция-удаления-из-реестра[,...]", L"Удаляет ключи и значения реестра."},
        {L"BitReg", L"DDInstall, DDInstall.HW", L"BitReg=секция-битов-реестра[,...]", L"Устанавливает или сбрасывает биты в значениях REG_BINARY."},
        {L"AddProperty", L"DDInstall, DDInstall.HW", L"AddProperty=секция-свойств[,...]", L"Задаёт свойства устройства (DEVPKEY)."},
        {L"DelProperty", L"DDInstall, DDInstall.HW", L"DelProperty=секция-свойств[,...]", L"Удаляет свойства устройства."},
        {L"Include", L"DDInstall", L"Include=файл.inf[,...]", L"Системные INF-файлы, секции которых используются через Needs."},
        {L"Needs", L"DDInstall", L"Needs=секция[,...]", L"Секции из INF, указанных в Include, которые нужно обработать."},
        {L"FeatureScore", L"DDInstall", L"FeatureScore=0x00..0xFF", L"Оценка функциональности драйвера при выборе между подходящими драйверами."},
        {L"Reboot", L"DDInstall", L"Reboot", L"После установки требуется перезагрузка."},
        {L"RegisterDlls", L"DDInstall", L"RegisterDlls=секция[,...]", L"Регистрирует OLE-библиотеки."},
        {L"UnregisterDlls", L"DDInstall", L"UnregisterDlls=секция[,...]", L"Отменяет регистрацию OLE-библиотек."},
        {L"ProfileItems", L"DDInstall", L"ProfileItems=секция[,...]", L"Ярлыки в главном меню. В универсальных драйверах недопустимо."},
        {L"CopyINF", L"DDInstall", L"CopyINF=файл.inf[,...]", L"Копирует дополнительные INF-файлы в хранилище драйверов."},
        {L"AddPowerSetting", L"DDInstall", L"AddPowerSetting=секция[,...]", L"Добавляет параметры электропитания."},
        {L"Ini2Reg", L"DDInstall", L"Ini2Reg=секция[,...]", L"Перенос данных из INI-файлов в реестр (устарело)."},
        {L"UpdateInis", L"DDInstall", L"UpdateInis=секция[,...]", L"Изменение INI-файлов (устарело)."},
        {L"UpdateIniFields", L"DDInstall", L"UpdateIniFields=секция[,...]", L"Изменение полей INI-файлов (устарело)."},
        {L"LogConfig", L"DDInstall", L"LogConfig=секция[,...]", L"Логическая конфигурация ресурсов для устройств не Plug and Play."},
        {L"AddComponent", L"DDInstall.Components", L"AddComponent=имя,[флаги],секция-установки-компонента",
         L"Создаёт виртуальное программное устройство (компонент)."},
        {L"AddSoftware", L"DDInstall.Software", L"AddSoftware=имя,[флаги],секция-установки-ПО", L"Устанавливает сопутствующее программное обеспечение."},
        {L"AddEventProvider", L"DDInstall.Events", L"AddEventProvider={GUID},секция-поставщика", L"Регистрирует поставщика событий ETW."},
        {L"AddService", L"DDInstall.Services, DefaultInstall.Services",
         L"AddService=имя-службы,[флаги],секция-установки-службы[,секция-журнала-событий[,тип[,имя]]]",
         L"Устанавливает службу (драйвер). Флаг 0x2 (SPSVCINST_ASSOCSERVICE) назначает службу функциональным драйвером устройства."},
        {L"DelService", L"DDInstall.Services", L"DelService=имя-службы[,флаги[,тип-журнала[,имя]]]", L"Удаляет службу."},
        {L"AddInterface", L"DDInstall.Interfaces", L"AddInterface={GUID-интерфейса},[ссылка],[секция-интерфейса][,флаги]",
         L"Регистрирует интерфейс устройства."},
        {L"AddFilter", L"DDInstall.Filters", L"AddFilter=имя-фильтра,[флаги],секция-установки-фильтра",
         L"Добавляет драйвер-фильтр (Windows 10 1903 и новее)."},
        {L"KmdfService", L"DDInstall.Wdf", L"KmdfService=имя-службы,секция-установки-KMDF", L"Связывает службу драйвера с библиотекой KMDF."},
        {L"KmdfLibraryVersion", L"Секция установки KMDF", L"KmdfLibraryVersion=1.15", L"Версия библиотеки KMDF, с которой собран драйвер."},
        {L"UmdfService", L"DDInstall.Wdf", L"UmdfService=имя-службы,секция-установки-UMDF", L"Связывает драйвер UMDF со службой."},
        {L"UmdfServiceOrder", L"DDInstall.Wdf", L"UmdfServiceOrder=служба[,...]", L"Порядок загрузки драйверов UMDF в стеке."},
        {L"UmdfLibraryVersion", L"Секция установки UMDF", L"UmdfLibraryVersion=2.15.0", L"Версия библиотеки UMDF."},
        {L"UmdfKernelModeClientPolicy", L"DDInstall.Wdf", L"UmdfKernelModeClientPolicy=AllowKernelModeClients",
         L"Разрешает запросы ввода-вывода от драйверов режима ядра."},
        {L"UmdfDispatcher", L"DDInstall.Wdf", L"UmdfDispatcher=FileHandle|WinUsb|NativeUSB", L"Диспетчер ввода-вывода UMDF."},

        // Секция установки службы
        {L"DisplayName", L"Секция установки службы", L"DisplayName=%ServiceDesc%", L"Отображаемое имя службы."},
        {L"Description", L"Секция установки службы", L"Description=%ServiceDesc%", L"Описание службы."},
        {L"ServiceType", L"Секция установки службы", L"ServiceType=1",
         L"Тип службы: 1 — драйвер режима ядра, 2 — драйвер файловой системы, 0x10 — собственный процесс, 0x20 — общий процесс."},
        {L"StartType", L"Секция установки службы", L"StartType=3",
         L"Тип запуска: 0 — при загрузке, 1 — системный, 2 — автоматический, 3 — по требованию, 4 — отключено."},
        {L"ErrorControl", L"Секция установки службы", L"ErrorControl=1",
         L"Реакция на ошибку запуска: 0 — игнорировать, 1 — обычная, 2 — серьёзная, 3 — критическая."},
        {L"ServiceBinary", L"Секция установки службы", L"ServiceBinary=%13%\\driver.sys",
         L"Путь к исполняемому файлу службы. Для современных пакетов рекомендуется %13% (Driver Store)."},
        {L"StartName", L"Секция установки службы", L"StartName=учётная-запись", L"Учётная запись (для драйверов — имя объекта драйвера)."},
        {L"LoadOrderGroup", L"Секция установки службы", L"LoadOrderGroup=группа", L"Группа порядка загрузки."},
        {L"Dependencies", L"Секция установки службы", L"Dependencies=служба[,...]",
         L"Службы или группы (с префиксом +), от которых зависит служба."},
        {L"Security", L"Секция установки службы, ClassInstall32", L"Security=\"SDDL\"", L"Дескриптор безопасности в формате SDDL."},
        {L"ServiceSidType", L"Секция установки службы", L"ServiceSidType=1", L"Тип SID службы: 0 — нет, 1 — неограниченный, 3 — ограниченный."},
        {L"DelayedAutoStart", L"Секция установки службы", L"DelayedAutoStart=1", L"Отложенный автоматический запуск (только для StartType=2)."},
        {L"AddTrigger", L"Секция установки службы", L"AddTrigger=секция-триггера", L"Триггер запуска службы."},

        // Прочее
        {L"DefaultDestDir", L"DestinationDirs", L"DefaultDestDir=DirID[,поддиректория]",
         L"Директория назначения для секций списков файлов, не перечисленных в [DestinationDirs]."},
        {L"ExcludeFromSelect", L"ControlFlags", L"ExcludeFromSelect=ID-устройства|*", L"Скрывает устройства из списка ручного выбора драйвера."},
        {L"CopyFilesOnly", L"ControlFlags", L"CopyFilesOnly=ID-устройства", L"Только копировать файлы, без установки."},
        {L"HKR", L"Секция AddReg", L"HKR,[подключ],[имя],[флаги],[значение]",
         L"Относительный корень реестра: ключ устройства, драйвера, службы или класса — в зависимости от секции, из которой вызвана AddReg."},
    };
    return list;
}

const DirectiveInfo *findDirective(const wstring &name)
{
    wstring n = trim(name);
    // CatalogFile.NTamd64 и т. п.
    const size_t dot = n.find(L'.');
    if (dot != wstring::npos && dot > 0 && iequals(n.substr(0, dot), L"CatalogFile"))
        n = n.substr(0, dot);
    for (const DirectiveInfo &d : directives())
        if (iequals(d.name, n))
            return &d;
    return nullptr;
}

StringList standardSectionNames()
{
    return {L"Version", L"Manufacturer", L"SourceDisksNames", L"SourceDisksFiles", L"DestinationDirs",
            L"Strings", L"DefaultInstall", L"DefaultUninstall", L"ClassInstall32", L"ControlFlags",
            L"InterfaceInstall32", L"SignatureAttributes", L"PackageInfo"};
}

static wstring baseOf(const wstring &name)
{
    return lower(name.substr(0, name.find(L'.')));
}

bool isStandardSection(const wstring &name)
{
    static const StringList roots = {L"version", L"manufacturer", L"sourcedisksnames", L"sourcedisksfiles",
                                     L"destinationdirs", L"strings", L"defaultinstall", L"defaultuninstall",
                                     L"classinstall32", L"controlflags", L"interfaceinstall32",
                                     L"signatureattributes", L"packageinfo"};
    const wstring b = baseOf(name);
    for (const wstring &r : roots)
        if (r == b)
            return true;
    return false;
}

wstring standardSectionHelp(const wstring &name)
{
    const wstring base = baseOf(name);
    if (base == L"version")
        return L"Обязательная секция: подпись INF, класс устройства, поставщик, версия драйвера и каталог.";
    if (base == L"manufacturer")
        return L"Список производителей. Каждая запись ссылается на секцию моделей и задаёт декорации платформ (NTamd64, NTarm64...).";
    if (base == L"sourcedisksnames")
        return L"Описание установочных дисков: id-диска = описание,[файл-тега],[не-используется],[путь].";
    if (base == L"sourcedisksfiles")
        return L"Расположение исходных файлов: имя-файла = id-диска[,поддиректория[,размер]].";
    if (base == L"destinationdirs")
        return L"Директории назначения: секция-списка-файлов = DirID[,поддиректория]; DefaultDestDir — значение по умолчанию.";
    if (base == L"strings")
        return L"Строковые константы для подстановок %имя%. Секции Strings.LLLL задают локализованные варианты.";
    if (base == L"defaultinstall")
        return L"Установка без Plug and Play (правый щелчок → «Установить»). Для драйверов устройств не используется.";
    if (base == L"defaultuninstall")
        return L"Удаление для INF без Plug and Play. В пакетах из Driver Store недопустимо.";
    if (base == L"classinstall32")
        return L"Установка нового класса устройств.";
    if (base == L"controlflags")
        return L"Флаги управления установкой (ExcludeFromSelect, CopyFilesOnly).";
    if (base == L"interfaceinstall32")
        return L"Установка новых классов интерфейсов устройств.";
    if (base == L"signatureattributes")
        return L"Атрибуты подписи файлов (подпись для PETrust, DRM и т. п.).";
    return {};
}

bool isGuid(const wstring &text)
{
    const wstring t = trim(text);
    if (t.size() != 38 || t.front() != L'{' || t.back() != L'}')
        return false;
    for (size_t i = 1; i < 37; ++i) {
        const bool dash = i == 9 || i == 14 || i == 19 || i == 24;
        if (dash ? t[i] != L'-' : !std::iswxdigit(t[i]))
            return false;
    }
    return true;
}

StringList knownArchitectures()
{
    return {L"x86", L"amd64", L"arm64", L"arm", L"ia64"};
}

static wstring hex8(uint32_t v)
{
    wchar_t buf[16];
    swprintf_s(buf, L"0x%08x", v);
    return buf;
}

static wstring flagsList(uint32_t value, const std::vector<std::pair<uint32_t, wstring>> &table)
{
    StringList parts;
    uint32_t rest = value;
    for (const auto &p : table) {
        if (p.first && (value & p.first) == p.first) {
            parts.push_back(p.second);
            rest &= ~p.first;
        }
    }
    if (rest)
        parts.push_back(hex8(rest));
    return parts.empty() ? L"нет флагов" : join(parts, L" | ");
}

wstring explainAddRegFlags(uint32_t flags)
{
    wstring type;
    const uint32_t typeBits = flags & 0xFFFF0001u;
    switch (typeBits) {
    case 0x00000000: type = L"REG_SZ"; break;
    case 0x00000001: type = L"REG_BINARY"; break;
    case 0x00010000: type = L"REG_MULTI_SZ"; break;
    case 0x00020000: type = L"REG_EXPAND_SZ"; break;
    case 0x00010001: type = L"REG_DWORD"; break;
    case 0x00020001: type = L"REG_NONE"; break;
    default: type = L"тип " + hex8(typeBits);
    }
    static const std::vector<std::pair<uint32_t, wstring>> table = {
        {0x00000002, L"FLG_ADDREG_NOCLOBBER"},     {0x00000004, L"FLG_ADDREG_DELVAL"},
        {0x00000008, L"FLG_ADDREG_APPEND"},        {0x00000010, L"FLG_ADDREG_KEYONLY"},
        {0x00000020, L"FLG_ADDREG_OVERWRITEONLY"}, {0x00001000, L"FLG_ADDREG_KEYONLY_COMMON"},
        {0x00002000, L"FLG_DELREG_KEYONLY_COMMON"}, {0x00004000, L"FLG_ADDREG_64BITKEY"},
        {0x00008000, L"FLG_ADDREG_32BITKEY"},
    };
    const uint32_t other = flags & 0x0000FFFEu;
    wstring s = type;
    if (other)
        s += L", " + flagsList(other, table);
    return s;
}

wstring explainValue(const wstring &directive, const wstring &value)
{
    const wstring d = lower(directive);
    uint32_t n = 0;
    const bool isNum = parseNumber(value, n);

    if (d == L"servicetype" && isNum) {
        switch (n) {
        case 0x1: return L"SERVICE_KERNEL_DRIVER — драйвер режима ядра";
        case 0x2: return L"SERVICE_FILE_SYSTEM_DRIVER — драйвер файловой системы";
        case 0x4: return L"SERVICE_ADAPTER";
        case 0x8: return L"SERVICE_RECOGNIZER_DRIVER";
        case 0x10: return L"SERVICE_WIN32_OWN_PROCESS — служба в собственном процессе";
        case 0x20: return L"SERVICE_WIN32_SHARE_PROCESS — служба в общем процессе";
        }
    }
    if (d == L"starttype" && isNum) {
        switch (n) {
        case 0: return L"SERVICE_BOOT_START — запуск загрузчиком";
        case 1: return L"SERVICE_SYSTEM_START — запуск при инициализации ядра";
        case 2: return L"SERVICE_AUTO_START — автоматический запуск";
        case 3: return L"SERVICE_DEMAND_START — запуск по требованию (типично для PnP)";
        case 4: return L"SERVICE_DISABLED — отключено";
        }
    }
    if (d == L"errorcontrol" && isNum) {
        switch (n) {
        case 0: return L"SERVICE_ERROR_IGNORE";
        case 1: return L"SERVICE_ERROR_NORMAL";
        case 2: return L"SERVICE_ERROR_SEVERE";
        case 3: return L"SERVICE_ERROR_CRITICAL";
        }
    }
    if (d == L"servicesidtype" && isNum) {
        switch (n) {
        case 0: return L"SERVICE_SID_TYPE_NONE";
        case 1: return L"SERVICE_SID_TYPE_UNRESTRICTED";
        case 3: return L"SERVICE_SID_TYPE_RESTRICTED";
        }
    }
    if (d == L"addservice.flags" && isNum) {
        static const std::vector<std::pair<uint32_t, wstring>> table = {
            {0x00000002, L"SPSVCINST_ASSOCSERVICE"},
            {0x00000008, L"SPSVCINST_NOCLOBBER_DISPLAYNAME"},
            {0x00000010, L"SPSVCINST_NOCLOBBER_STARTTYPE"},
            {0x00000020, L"SPSVCINST_NOCLOBBER_ERRORCONTROL"},
            {0x00000040, L"SPSVCINST_NOCLOBBER_LOADORDERGROUP"},
            {0x00000080, L"SPSVCINST_NOCLOBBER_DEPENDENCIES"},
            {0x00000100, L"SPSVCINST_NOCLOBBER_DESCRIPTION"},
            {0x00000400, L"SPSVCINST_STARTSERVICE"},
            {0x00000800, L"SPSVCINST_NOCLOBBER_REQUIREDPRIVILEGES"},
        };
        return flagsList(n, table);
    }
    if (d == L"copyfiles.flags" && isNum) {
        static const std::vector<std::pair<uint32_t, wstring>> table = {
            {0x00000001, L"COPYFLG_WARN_IF_SKIP"},        {0x00000002, L"COPYFLG_NOSKIP"},
            {0x00000004, L"COPYFLG_NOVERSIONCHECK"},      {0x00000008, L"COPYFLG_FORCE_FILE_IN_USE"},
            {0x00000010, L"COPYFLG_NO_OVERWRITE"},        {0x00000020, L"COPYFLG_NO_VERSION_DIALOG"},
            {0x00000040, L"COPYFLG_OVERWRITE_OLDER_ONLY"}, {0x00000400, L"COPYFLG_REPLACEONLY"},
            {0x00000800, L"COPYFLG_NODECOMP"},            {0x00001000, L"COPYFLG_REPLACE_BOOT_FILE"},
            {0x00002000, L"COPYFLG_NOPRUNE"},             {0x00004000, L"COPYFLG_IN_USE_TRY_RENAME"},
        };
        return flagsList(n, table);
    }
    if (d == L"addreg.flags" && isNum)
        return explainAddRegFlags(n);
    if (d == L"addreg.root") {
        const wstring v = lower(trim(value));
        if (v == L"hkr") return L"Относительный корень (ключ устройства, драйвера или службы)";
        if (v == L"hklm") return L"HKEY_LOCAL_MACHINE";
        if (v == L"hkcu") return L"HKEY_CURRENT_USER (недопустимо в универсальных драйверах)";
        if (v == L"hkcr") return L"HKEY_CLASSES_ROOT";
        if (v == L"hku") return L"HKEY_USERS";
    }
    if (d == L"dirid" && isNum) {
        const wstring desc = dirIdDescription(int(n));
        if (!desc.empty())
            return L"DirID " + std::to_wstring(n) + L": " + desc;
    }
    if (d == L"class" || d == L"classguid") {
        const DeviceClassInfo *c = d == L"class" ? findClassByName(value) : findClassByGuid(value);
        if (c)
            return c->name + L" " + c->guid + L" — " + c->description;
    }
    return {};
}

} // namespace inf
