#include "cabwriter.h"

#include "inffile.h"

#include <windows.h>
#include <fci.h>
#include <fcntl.h>

#include <cstdlib>
#include <cstring>

// сигнатуры обратных вызовов FCI задаются макросами, часть параметров не нужна
#pragma warning(disable : 4100)

namespace inf {

namespace {

// Имена файлов передаются в FCI в UTF-8: обратные вызовы открывают их через Unicode-API
struct CabContext {
    bool utfName = false;   // имя текущего файла в архиве содержит символы вне ASCII
};

bool isAscii(const std::string &s)
{
    for (unsigned char c : s)
        if (c >= 0x80)
            return false;
    return true;
}

FNFCIALLOC(cabAlloc)
{
    return malloc(cb);
}

FNFCIFREE(cabFree)
{
    free(memory);
}

FNFCIOPEN(cabOpen)
{
    DWORD access = 0;
    if ((oflag & (_O_RDWR | _O_WRONLY)) == 0)
        access = GENERIC_READ;
    else if (oflag & _O_RDWR)
        access = GENERIC_READ | GENERIC_WRITE;
    else
        access = GENERIC_WRITE;
    DWORD creation = OPEN_EXISTING;
    if ((oflag & _O_CREAT) && (oflag & _O_EXCL))
        creation = CREATE_NEW;
    else if ((oflag & _O_CREAT) && (oflag & _O_TRUNC))
        creation = CREATE_ALWAYS;
    else if (oflag & _O_CREAT)
        creation = OPEN_ALWAYS;
    else if (oflag & _O_TRUNC)
        creation = TRUNCATE_EXISTING;
    HANDLE h = CreateFileW(fromUtf8(pszFile).c_str(), access, FILE_SHARE_READ, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        *err = int(GetLastError());
        return -1;
    }
    return reinterpret_cast<INT_PTR>(h);
}

FNFCIREAD(cabRead)
{
    DWORD read = 0;
    if (!ReadFile(reinterpret_cast<HANDLE>(hf), memory, cb, &read, nullptr)) {
        *err = int(GetLastError());
        return UINT(-1);
    }
    return read;
}

FNFCIWRITE(cabWrite)
{
    DWORD written = 0;
    if (!WriteFile(reinterpret_cast<HANDLE>(hf), memory, cb, &written, nullptr)) {
        *err = int(GetLastError());
        return UINT(-1);
    }
    return written;
}

FNFCICLOSE(cabClose)
{
    if (!CloseHandle(reinterpret_cast<HANDLE>(hf))) {
        *err = int(GetLastError());
        return -1;
    }
    return 0;
}

FNFCISEEK(cabSeek)
{
    const DWORD pos = SetFilePointer(reinterpret_cast<HANDLE>(hf), dist, nullptr, DWORD(seektype));
    if (pos == INVALID_SET_FILE_POINTER) {
        *err = int(GetLastError());
        return -1;
    }
    return long(pos);
}

FNFCIDELETE(cabDelete)
{
    if (!DeleteFileW(fromUtf8(pszFile).c_str())) {
        *err = int(GetLastError());
        return -1;
    }
    return 0;
}

FNFCIGETTEMPFILE(cabTempFile)
{
    wchar_t dir[MAX_PATH], name[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, dir) || !GetTempFileNameW(dir, L"cab", 0, name))
        return FALSE;
    // FCI создаёт файл сам
    DeleteFileW(name);
    const std::string u = toUtf8(name);
    if (int(u.size()) >= cbTempName)
        return FALSE;
    memcpy(pszTempName, u.c_str(), u.size() + 1);
    return TRUE;
}

FNFCIFILEPLACED(cabFilePlaced)
{
    return 0;
}

FNFCIGETNEXTCABINET(cabNextCabinet)
{
    // архив всегда один
    return FALSE;
}

FNFCISTATUS(cabStatus)
{
    return 0;
}

FNFCIGETOPENINFO(cabOpenInfo)
{
    HANDLE h = CreateFileW(fromUtf8(pszName).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        *err = int(GetLastError());
        return -1;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    FILETIME local{};
    if (GetFileInformationByHandle(h, &info)) {
        FileTimeToLocalFileTime(&info.ftLastWriteTime, &local);
        FileTimeToDosDateTime(&local, pdate, ptime);
        *pattribs = USHORT(info.dwFileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE));
    } else {
        *pdate = 0;
        *ptime = 0;
        *pattribs = 0;
    }
    if (static_cast<CabContext *>(pv)->utfName)
        *pattribs |= _A_NAME_IS_UTF;
    return reinterpret_cast<INT_PTR>(h);
}

wstring fciError(const ERF &erf)
{
    switch (erf.erfOper) {
    case FCIERR_OPEN_SRC: return L"не удалось открыть исходный файл";
    case FCIERR_READ_SRC: return L"не удалось прочитать исходный файл";
    case FCIERR_ALLOC_FAIL: return L"недостаточно памяти";
    case FCIERR_TEMP_FILE: return L"не удалось создать временный файл";
    case FCIERR_BAD_COMPR_TYPE: return L"неизвестный тип сжатия";
    case FCIERR_CAB_FILE: return L"не удалось записать архив";
    case FCIERR_USER_ABORT: return L"операция прервана";
    case FCIERR_MCI_FAIL: return L"ошибка сжатия";
    case FCIERR_CAB_FORMAT_LIMIT: return L"превышен предельный размер архива";
    default: return L"ошибка FCI " + std::to_wstring(erf.erfOper);
    }
}

} // namespace

bool writeCab(const wstring &cabPath, const std::vector<CabEntry> &entries, wstring *error)
{
    auto fail = [&](const wstring &msg) {
        if (error)
            *error = msg;
        return false;
    };
    const size_t slash = cabPath.find_last_of(L"\\/");
    const std::string dir = toUtf8(slash == wstring::npos ? L".\\" : cabPath.substr(0, slash + 1));
    const std::string name = toUtf8(slash == wstring::npos ? cabPath : cabPath.substr(slash + 1));

    CCAB ccab{};
    ccab.cb = 0x7FFFFFFF;
    ccab.cbFolderThresh = 0x7FFFFFFF;
    ccab.iCab = 1;
    ccab.iDisk = 1;
    ccab.setID = 0x1234;
    if (dir.size() >= sizeof(ccab.szCabPath) || name.size() >= sizeof(ccab.szCab))
        return fail(L"Слишком длинный путь к архиву");
    memcpy(ccab.szCabPath, dir.c_str(), dir.size() + 1);
    memcpy(ccab.szCab, name.c_str(), name.size() + 1);

    DeleteFileW(cabPath.c_str());
    ERF erf{};
    CabContext ctx;
    HFCI fci = FCICreate(&erf, cabFilePlaced, cabAlloc, cabFree, cabOpen, cabRead, cabWrite, cabClose, cabSeek, cabDelete,
                         cabTempFile, &ccab, &ctx);
    if (!fci)
        return fail(L"Не удалось начать создание архива: " + fciError(erf));

    for (const CabEntry &e : entries) {
        std::string src = toUtf8(e.source);
        std::string inCab = toUtf8(replaceAll(e.name, L"/", L"\\"));
        // имена с символами вне ASCII хранятся в UTF-8 с признаком _A_NAME_IS_UTF
        ctx.utfName = !isAscii(inCab);
        if (!FCIAddFile(fci, src.data(), inCab.data(), FALSE, cabNextCabinet, cabStatus, cabOpenInfo,
                        TCOMPfromLZXWindow(21))) {
            const wstring msg = fciError(erf) + L": " + e.source;
            FCIDestroy(fci);
            DeleteFileW(cabPath.c_str());
            return fail(msg);
        }
    }
    if (!FCIFlushCabinet(fci, FALSE, cabNextCabinet, cabStatus)) {
        const wstring msg = fciError(erf);
        FCIDestroy(fci);
        DeleteFileW(cabPath.c_str());
        return fail(L"Не удалось записать архив: " + msg);
    }
    FCIDestroy(fci);
    return true;
}

} // namespace inf
