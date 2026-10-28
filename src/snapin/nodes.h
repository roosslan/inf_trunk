// Узлы дерева консоли и строки области результатов
#pragma once

#include "drvstore.h"
#include "inffiledoc.h"

#include <memory>
#include <string>
#include <vector>

enum class NodeType {
    Root,           // «Редактор INF»
    File,           // открытый INF-файл
    Version,        // [Version]
    Devices,        // производители и устройства
    Manufacturer,   // один производитель
    Sections,       // все секции
    Section,        // одна секция
    Strings,        // строковые константы
    StringSection,  // [Strings] или [Strings.LLLL]
    Files,          // копируемые файлы
    Problems,       // результаты проверки
    Hardware,       // устройства компьютера
    DeviceClass,    // один класс устройств компьютера
    DriverPackages  // сторонние пакеты в хранилище драйверов
};

struct Node {
    NodeType type = NodeType::Root;
    InfFileDoc *file = nullptr;
    std::wstring key;               // имя секции, ключ производителя и т. п.
    Node *parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;
    HSCOPEITEM hItem = 0;
    bool expanded = false;

    std::wstring displayName() const;
    int image() const;
    int openImage() const;
    bool mayHaveChildren() const;
    const GUID &nodeType() const;
    bool isInside(const Node *ancestor) const;

    Node *addChild(NodeType t, const std::wstring &k);
    Node *findChild(NodeType t, const std::wstring &k) const;
};

// Желаемые дочерние узлы для узлов внутри файла (вычисляются по анализу)
std::vector<std::pair<NodeType, std::wstring>> desiredChildren(const Node &node);

enum class RowType { Entry, Device, String, CopiedFile, Problem, SysDevice, Package };

struct ResultRow {
    RowType type = RowType::Entry;
    InfFileDoc *file = nullptr;
    int fileId = 0;
    int logicalLine = -1;       // логическая строка записи (для проблем — строка, к которой она относится)
    std::wstring lineText;      // текст записи на момент построения строки (для проверки актуальности)
    std::wstring section;       // секция записи
    std::wstring key;           // ключ строки и т. п.
    inf::Diagnostic diagnostic; // для проблем
    std::vector<std::wstring> columns;
    int image = 0;
    inf::DeviceInfo device;     // для устройств компьютера
    inf::DriverPackage package; // для пакетов драйверов

    bool canDelete() const { return type != RowType::Problem && logicalLine >= 0; }
};

struct ColumnInfo {
    const wchar_t *title;
    int width;
};

// Колонки области результатов для узла
std::vector<ColumnInfo> columnsFor(const Node &node);

// Строки области результатов для узла
std::vector<std::shared_ptr<ResultRow>> buildRows(const Node &node);

// Текст колонки для дочернего узла дерева, показанного в области результатов
std::wstring scopeItemColumn(const Node &child, int column);

// Поиск логической строки: сначала по номеру, затем по тексту
int locateLine(const inf::Analysis &a, int logicalLine, const std::wstring &lineText);

// Снимок устройств и пакетов драйверов компьютера (загружается при первом обращении)
struct SystemSnapshot {
    std::vector<inf::DeviceInfo> devices;
    std::vector<inf::DriverPackage> packages;
    std::wstring error;
};
const SystemSnapshot &systemSnapshot();
void reloadSystemSnapshot();
// Показывать отключённые и скрытые устройства (как «Показать скрытые устройства» в devmgmt.msc)
void setShowHiddenDevices(bool show);
bool showHiddenDevices();
bool isSystemNode(NodeType t);

// Производитель по ключу узла (имя записи и секция моделей)
const inf::Manufacturer *manufacturerForKey(const inf::Analysis &a, const std::wstring &key);

// Отображаемое имя языка для секции Strings.LLLL
std::wstring stringSectionTitle(const std::wstring &section);
