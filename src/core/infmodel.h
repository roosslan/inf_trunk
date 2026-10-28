#pragma once

// Семантическая модель INF: роли секций, ссылки между ними, производители,
// модели устройств, строковые константы и копируемые файлы.

#include "infdocument.h"

#include <set>
#include <unordered_map>
#include <vector>

namespace inf {

enum class SectionRole {
    Unknown,
    Version,
    Manufacturer,
    Models,
    Install,            // DDInstall
    InstallServices,    // DDInstall.Services
    InstallHw,          // DDInstall.HW
    InstallCoInstallers,
    InstallInterfaces,
    InstallWdf,
    InstallComponents,
    InstallSoftware,
    InstallEvents,
    InstallFilters,
    InstallOther,       // LogConfigOverride, FactDef
    DefaultInstall,
    ClassInstall,
    ServiceInstall,
    EventLogInstall,
    CopyFilesList,
    DelFilesList,
    RenFilesList,
    AddReg,
    DelReg,
    BitReg,
    AddProperty,
    DelProperty,
    InterfaceInstall,
    WdfInstall,
    UmdfInstall,
    ComponentInstall,
    SoftwareInstall,
    EventProvider,
    FilterInstall,
    LogConfig,
    PowerSetting,
    RegisterDlls,
    ProfileItems,
    IniUpdate,
    Trigger,
    WmiSecurity,
    SourceDisksNames,
    SourceDisksFiles,
    DestinationDirs,
    Strings,
    ControlFlags,
    InterfaceInstall32,
    Standard
};

wstring roleName(SectionRole role);

struct SectionRef {
    wstring target;     // имя секции, на которую указывает ссылка
    wstring from;       // имя секции, в которой находится ссылка
    wstring directive;  // директива (CopyFiles, AddReg...) или вид неявной связи
    SectionRole role = SectionRole::Unknown;
    int line = -1;      // -1 для неявных ссылок (DDInstall -> DDInstall.Services)
    int column = 0;
    int length = 0;
    bool exists = false;
    bool implicit = false;
};

struct Manufacturer {
    wstring name;           // ключ записи как в файле (например, %MfgName%)
    wstring displayName;    // после подстановки строк
    wstring modelsBase;
    StringList decorations;
    StringList modelSections;
    int line = -1;
    int logicalLine = -1;
};

struct DeviceModel {
    wstring modelsSection;
    wstring description;    // как в файле
    wstring displayName;    // после подстановки строк
    wstring installBase;    // имя секции установки из записи модели
    wstring installSection; // фактически выбранная секция (с учётом .NTamd64/.NT)
    StringList hardwareIds;
    StringList compatibleIds;
    wstring architecture;   // из декорации секции моделей
    int line = -1;
    int logicalLine = -1;
    int hardwareIdColumn = 0;
    int hardwareIdLength = 0;
};

struct StringDef {
    wstring key;
    wstring value;
    wstring section;
    int line = -1;
    int logicalLine = -1;
};

struct StringToken {
    int start = 0;          // позиция '%'
    int length = 0;         // вместе с обоими '%'
    wstring name;
    bool numeric = false;   // %10% — DirID, а не строка
};

struct FileRef {
    wstring destName;
    wstring sourceName;
    wstring listSection;    // пусто для CopyFiles=@файл
    int line = -1;
    int logicalLine = -1;
    int column = 0;
    int length = 0;
};

struct SourceFile {
    wstring name;
    wstring diskId;
    wstring subdir;
    wstring section;
    int line = -1;
    int logicalLine = -1;
};

class InfModel {
public:
    static InfModel analyze(const InfDocument &doc);

    // Строки
    static std::vector<StringToken> stringTokens(const wstring &text);
    wstring resolve(const wstring &text, StringList *missing = nullptr) const;
    bool hasString(const wstring &key) const;
    wstring stringValue(const wstring &key) const;
    const StringList &stringSections() const { return m_stringSections; }
    wstring baseStringSection() const { return m_baseStrings; }
    std::vector<StringDef> strings(const wstring &section) const;
    const std::vector<StringDef> &allStrings() const { return m_allStrings; }
    // Сколько раз строка %ключ% используется вне секций строк
    int stringUses(const wstring &key) const;

    // Секции и ссылки
    SectionRole role(const wstring &section) const;
    const std::vector<SectionRef> &references() const { return m_refs; }
    std::vector<SectionRef> refsFrom(const wstring &section) const;
    std::vector<SectionRef> refsTo(const wstring &section) const;
    int incomingCount(const wstring &section) const;

    const std::vector<Manufacturer> &manufacturers() const { return m_manufacturers; }
    const std::vector<DeviceModel> &models() const { return m_models; }
    StringList modelSections() const;
    const StringList &installSections() const { return m_installSections; }

    // Файлы
    const std::vector<FileRef> &copiedFiles() const { return m_files; }
    const std::unordered_map<wstring, SourceFile> &sourceFiles() const { return m_sourceFiles; }
    const std::set<wstring> &sourceDisks() const { return m_disks; }
    const std::unordered_map<wstring, int> &destinationDirs() const { return m_destDirs; }

    // Архитектура из декорации: "NTamd64.10.0...16299" -> "amd64"; "NT" -> ""
    static wstring architectureOf(const wstring &decoration);

private:
    void setRole(const wstring &section, SectionRole role);
    void addRef(const InfDocument &doc, SectionRef ref);

    StringList m_stringSections;
    wstring m_baseStrings;
    std::unordered_map<wstring, std::unordered_map<wstring, StringDef>> m_strings;  // секция -> ключ -> строка
    std::vector<StringDef> m_allStrings;
    std::unordered_map<wstring, int> m_stringUses;

    std::unordered_map<wstring, SectionRole> m_roles;
    std::vector<SectionRef> m_refs;
    std::unordered_map<wstring, int> m_incoming;
    std::vector<Manufacturer> m_manufacturers;
    std::vector<DeviceModel> m_models;
    StringList m_installSections;

    std::vector<FileRef> m_files;
    std::unordered_map<wstring, SourceFile> m_sourceFiles;
    std::set<wstring> m_disks;
    std::unordered_map<wstring, int> m_destDirs;
};

} // namespace inf
