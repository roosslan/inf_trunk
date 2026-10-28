#pragma once

// Чтение и запись INF-файлов с сохранением кодировки, BOM, концов строк
// и завершающего символа Ctrl+Z, который встречается в старых INF.

#include "infstr.h"

#include <string>

namespace inf {

using Bytes = std::string;

enum class Encoding {
    Ansi,       // системная кодовая страница ANSI
    Utf8,       // UTF-8 без BOM
    Utf8Bom,    // UTF-8 с BOM
    Utf16LE,    // UTF-16 LE (рекомендуемая Microsoft кодировка Unicode-INF)
    Utf16BE     // UTF-16 BE
};

enum class LineEnding { CRLF, LF, CR };

struct FileFormat {
    Encoding encoding = Encoding::Ansi;
    LineEnding lineEnding = LineEnding::CRLF;
    bool hasBom = false;        // для UTF-16: был ли BOM
    bool trailingCtrlZ = false; // в конце файла стоял символ 0x1A
    bool mixedLineEndings = false;
};

struct DecodedFile {
    wstring text;       // текст с концами строк '\n'
    FileFormat format;
    bool lossy = false; // при декодировании встретились недопустимые байты
};

DecodedFile decode(const Bytes &bytes);
Bytes encode(const wstring &text, const FileFormat &format);

wstring encodingName(Encoding e);
wstring lineEndingName(LineEnding e);

bool readBytes(const wstring &path, Bytes &out, wstring *error = nullptr);
bool writeBytes(const wstring &path, const Bytes &bytes, wstring *error = nullptr);
bool readFile(const wstring &path, DecodedFile &out, wstring *error = nullptr);
bool writeFile(const wstring &path, const wstring &text, const FileFormat &format, wstring *error = nullptr);

// Текст последней ошибки Windows
wstring lastErrorText(unsigned long code);

} // namespace inf
