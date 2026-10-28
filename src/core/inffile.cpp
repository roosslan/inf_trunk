#include "inffile.h"

#include <windows.h>

namespace inf {

namespace {

bool looksLikeUtf16(const Bytes &b, bool littleEndian)
{
    if (b.size() < 4 || b.size() % 2 != 0)
        return false;
    const size_t pairs = std::min<size_t>(b.size() / 2, 4096);
    size_t zeros = 0;
    for (size_t i = 0; i < pairs; ++i) {
        const char hi = littleEndian ? b[2 * i + 1] : b[2 * i];
        const char lo = littleEndian ? b[2 * i] : b[2 * i + 1];
        if (hi == 0 && lo != 0)
            ++zeros;
    }
    return zeros * 10 >= pairs * 4;
}

bool isAscii(const Bytes &b)
{
    for (char c : b)
        if (static_cast<unsigned char>(c) >= 0x80)
            return false;
    return true;
}

// Декодирование однобайтовой или UTF-8 кодировки; strict — проверять недопустимые байты
bool decodeCodePage(const Bytes &b, UINT cp, bool strict, wstring &out)
{
    out.clear();
    if (b.empty())
        return true;
    const DWORD flags = strict ? MB_ERR_INVALID_CHARS : 0;
    const int n = MultiByteToWideChar(cp, flags, b.data(), int(b.size()), nullptr, 0);
    if (n <= 0)
        return false;
    out.resize(size_t(n));
    MultiByteToWideChar(cp, flags, b.data(), int(b.size()), out.data(), n);
    return true;
}

wstring decodeUtf16(const Bytes &b, bool littleEndian, bool &lossy)
{
    wstring out;
    out.reserve(b.size() / 2);
    for (size_t i = 0; i + 1 < b.size(); i += 2) {
        const unsigned char a = static_cast<unsigned char>(b[i]);
        const unsigned char c = static_cast<unsigned char>(b[i + 1]);
        out += littleEndian ? wchar_t(a | (c << 8)) : wchar_t((a << 8) | c);
    }
    if (b.size() % 2)
        lossy = true;
    return out;
}

Bytes encodeCodePage(const wstring &t, UINT cp)
{
    if (t.empty())
        return {};
    const int n = WideCharToMultiByte(cp, 0, t.data(), int(t.size()), nullptr, 0, nullptr, nullptr);
    Bytes out(size_t(n), '\0');
    WideCharToMultiByte(cp, 0, t.data(), int(t.size()), out.data(), n, nullptr, nullptr);
    return out;
}

} // namespace

DecodedFile decode(const Bytes &bytes)
{
    DecodedFile out;
    FileFormat &f = out.format;
    Bytes body = bytes;

    if (bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        f.encoding = Encoding::Utf8Bom;
        f.hasBom = true;
        body = bytes.substr(3);
    } else if (bytes.compare(0, 2, "\xFF\xFE") == 0) {
        f.encoding = Encoding::Utf16LE;
        f.hasBom = true;
        body = bytes.substr(2);
    } else if (bytes.compare(0, 2, "\xFE\xFF") == 0) {
        f.encoding = Encoding::Utf16BE;
        f.hasBom = true;
        body = bytes.substr(2);
    } else if (looksLikeUtf16(bytes, true)) {
        f.encoding = Encoding::Utf16LE;
    } else if (looksLikeUtf16(bytes, false)) {
        f.encoding = Encoding::Utf16BE;
    } else if (isAscii(bytes)) {
        f.encoding = Encoding::Ansi;
    } else {
        wstring probe;
        f.encoding = decodeCodePage(bytes, CP_UTF8, true, probe) ? Encoding::Utf8 : Encoding::Ansi;
    }

    wstring text;
    switch (f.encoding) {
    case Encoding::Utf16LE:
    case Encoding::Utf16BE:
        text = decodeUtf16(body, f.encoding == Encoding::Utf16LE, out.lossy);
        break;
    default: {
        const UINT cp = f.encoding == Encoding::Ansi ? CP_ACP : CP_UTF8;
        if (!decodeCodePage(body, cp, true, text)) {
            out.lossy = true;
            decodeCodePage(body, cp, false, text);
        }
    }
    }

    if (!text.empty() && text.back() == wchar_t(0x1A)) {
        f.trailingCtrlZ = true;
        text.pop_back();
    }

    // концы строк: считаем и приводим к '\n'
    int crlf = 0, lf = 0, cr = 0;
    wstring norm;
    norm.reserve(text.size());
    const size_t n = text.size();
    for (size_t i = 0; i < n; ++i) {
        const wchar_t c = text[i];
        if (c == L'\r') {
            if (i + 1 < n && text[i + 1] == L'\n') {
                ++crlf;
                ++i;
            } else {
                ++cr;
            }
            norm += L'\n';
        } else {
            if (c == L'\n')
                ++lf;
            norm += c;
        }
    }
    if (lf > crlf && lf >= cr)
        f.lineEnding = LineEnding::LF;
    else if (cr > crlf && cr > lf)
        f.lineEnding = LineEnding::CR;
    else
        f.lineEnding = LineEnding::CRLF;
    f.mixedLineEndings = (crlf > 0) + (lf > 0) + (cr > 0) > 1;

    out.text = std::move(norm);
    return out;
}

Bytes encode(const wstring &text, const FileFormat &format)
{
    wstring t;
    switch (format.lineEnding) {
    case LineEnding::CRLF: t = replaceAll(text, L"\n", L"\r\n"); break;
    case LineEnding::CR: t = replaceAll(text, L"\n", L"\r"); break;
    case LineEnding::LF: t = text; break;
    }
    if (format.trailingCtrlZ)
        t += wchar_t(0x1A);

    Bytes out;
    switch (format.encoding) {
    case Encoding::Ansi:
        out = encodeCodePage(t, CP_ACP);
        break;
    case Encoding::Utf8:
        out = encodeCodePage(t, CP_UTF8);
        break;
    case Encoding::Utf8Bom:
        out = "\xEF\xBB\xBF" + encodeCodePage(t, CP_UTF8);
        break;
    case Encoding::Utf16LE:
    case Encoding::Utf16BE: {
        const bool le = format.encoding == Encoding::Utf16LE;
        if (format.hasBom)
            out = le ? Bytes("\xFF\xFE") : Bytes("\xFE\xFF");
        out.reserve(out.size() + t.size() * 2);
        for (wchar_t c : t) {
            const char lo = char(c & 0xFF), hi = char((c >> 8) & 0xFF);
            out += le ? lo : hi;
            out += le ? hi : lo;
        }
        break;
    }
    }
    return out;
}

wstring encodingName(Encoding e)
{
    switch (e) {
    case Encoding::Ansi: return L"ANSI";
    case Encoding::Utf8: return L"UTF-8";
    case Encoding::Utf8Bom: return L"UTF-8 BOM";
    case Encoding::Utf16LE: return L"UTF-16 LE";
    case Encoding::Utf16BE: return L"UTF-16 BE";
    }
    return {};
}

wstring lineEndingName(LineEnding e)
{
    switch (e) {
    case LineEnding::CRLF: return L"CRLF (Windows)";
    case LineEnding::LF: return L"LF (Unix)";
    case LineEnding::CR: return L"CR";
    }
    return {};
}

wstring lastErrorText(unsigned long code)
{
    wchar_t *buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   code, 0, reinterpret_cast<wchar_t *>(&buf), 0, nullptr);
    wstring s = buf ? trim(buf) : L"Ошибка " + std::to_wstring(code);
    if (buf)
        LocalFree(buf);
    return s;
}

bool readBytes(const wstring &path, Bytes &out, wstring *error)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (error)
            *error = lastErrorText(GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    if (size.QuadPart > 256LL * 1024 * 1024) {
        CloseHandle(h);
        if (error)
            *error = L"Файл слишком большой";
        return false;
    }
    out.assign(size_t(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = out.empty() || ReadFile(h, out.data(), DWORD(out.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok) {
        if (error)
            *error = lastErrorText(GetLastError());
        return false;
    }
    out.resize(read);
    return true;
}

bool writeBytes(const wstring &path, const Bytes &bytes, wstring *error)
{
    // запись во временный файл рядом и атомарная замена
    const wstring tmp = path + L".~tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (error)
            *error = lastErrorText(GetLastError());
        return false;
    }
    DWORD written = 0;
    const BOOL ok = bytes.empty() || WriteFile(h, bytes.data(), DWORD(bytes.size()), &written, nullptr);
    const DWORD err = GetLastError();
    CloseHandle(h);
    if (!ok || written != bytes.size()) {
        DeleteFileW(tmp.c_str());
        if (error)
            *error = lastErrorText(err);
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD e = GetLastError();
        DeleteFileW(tmp.c_str());
        if (error)
            *error = lastErrorText(e);
        return false;
    }
    return true;
}

bool readFile(const wstring &path, DecodedFile &out, wstring *error)
{
    Bytes b;
    if (!readBytes(path, b, error))
        return false;
    out = decode(b);
    return true;
}

bool writeFile(const wstring &path, const wstring &text, const FileFormat &format, wstring *error)
{
    return writeBytes(path, encode(text, format), error);
}

} // namespace inf
