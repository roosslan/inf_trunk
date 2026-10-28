// Модальные диалоги оснастки и общие вспомогательные функции для окон MFC
#pragma once

#include "infanalysis.h"

#include <string>
#include <vector>

// Текст элемента окна как std::wstring
std::wstring dlgText(CWnd *wnd, int id);
void setDlgText(CWnd *wnd, int id, const std::wstring &text);
std::wstring comboText(CComboBox &combo);

bool runAddEntryDialog(CWnd *parent, const std::wstring &section, std::wstring &key, std::wstring &value);
bool runAddSectionDialog(CWnd *parent, const inf::StringList &suggestions, std::wstring &name, std::wstring &content);

struct NewDevice {
    std::wstring description;
    std::wstring stringKey;     // пусто — описание без строки
    std::wstring hardwareId;
    std::wstring compatible;    // через запятую
    std::wstring install;
    inf::StringList modelSections;
};
bool runAddDeviceDialog(CWnd *parent, const inf::Analysis &a, NewDevice &out);

bool runAddStringDialog(CWnd *parent, const inf::StringList &sections, std::wstring &section, std::wstring &key,
                        std::wstring &value);

struct NewFile {
    std::wstring name;
    std::wstring list;      // секция копирования
    std::wstring disk = L"1";
    std::wstring subdir;
};
bool runAddFileDialog(CWnd *parent, const inf::Analysis &a, NewFile &out);

bool runInputDialog(CWnd *parent, const std::wstring &title, const std::wstring &prompt, std::wstring &value);
void runOutputDialog(CWnd *parent, const std::wstring &title, const std::wstring &text);

// Параметры экспорта пакета драйвера в CAB
struct ExportOptions {
    std::wstring title;
    std::wstring info;          // сведения о пакете
    std::wstring path;          // файл .cab или директория (directory = true)
    bool directory = false;
    bool flatten = true;        // все файлы в корень архива, пути в INF исправить
};
bool runExportDialog(CWnd *parent, ExportOptions &options);
