#include "infverif.h"

#include "inffile.h"

#include <windows.h>

#include <algorithm>
#include <iterator>
#include <regex>

namespace inf {

namespace {

bool fileExists(const wstring &path)
{
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

wstring env(const wchar_t *name)
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetEnvironmentVariableW(name, buf, DWORD(std::size(buf)));
    return n > 0 && n < std::size(buf) ? wstring(buf, n) : wstring();
}

std::vector<int> versionParts(const wstring &v)
{
    std::vector<int> out;
    for (const wstring &p : split(v, L'.'))
        out.push_back(toInt(p, -1));
    return out;
}

StringList subdirectories(const wstring &dir)
{
    StringList out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return out;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0)
            out.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

wstring quoteArg(const wstring &a)
{
    if (!a.empty() && a.find_first_of(L" \t\"") == wstring::npos)
        return a;
    return L"\"" + replaceAll(a, L"\"", L"\\\"") + L"\"";
}

} // namespace

wstring findInfVerif()
{
    StringList roots;
    const wstring wdk = env(L"WDKContentRoot");
    if (!wdk.empty())
        roots.push_back(wdk + L"\\Tools");
    for (const wchar_t *var : {L"ProgramFiles(x86)", L"ProgramFiles"}) {
        const wstring pf = env(var);
        if (!pf.empty())
            roots.push_back(pf + L"\\Windows Kits\\10\\Tools");
    }
#if defined(_M_ARM64)
    const wstring arch = L"arm64";
#else
    const wstring arch = L"x64";
#endif
    for (const wstring &root : roots) {
        StringList versions = subdirectories(root);
        std::sort(versions.begin(), versions.end(),
                  [](const wstring &a, const wstring &b) { return versionParts(a) > versionParts(b); });
        for (const wstring &v : versions) {
            const wstring candidate = root + L"\\" + v + L"\\" + arch + L"\\infverif.exe";
            if (fileExists(candidate))
                return candidate;
        }
        const wstring flat = root + L"\\" + arch + L"\\infverif.exe";
        if (fileExists(flat))
            return flat;
    }
    return {};
}

const std::vector<InfVerifMode> &infVerifModes()
{
    static const std::vector<InfVerifMode> modes = {
        {L"/w", L"Windows Driver (/w)"},
        {L"/u", L"Universal Driver (/u)"},
        {L"/h", L"Требования WHQL (/h)"},
        {L"/k", L"Declarative Driver (/k)"},
        {L"", L"Только базовая проверка"},
    };
    return modes;
}

StringList infVerifArguments(const wstring &modeKey, const wstring &file)
{
    StringList args = {L"/v"};
    if (!modeKey.empty())
        args.push_back(modeKey);
    args.push_back(file);
    return args;
}

InfVerifResult runInfVerif(const wstring &tool, const wstring &modeKey, const wstring &file, unsigned long timeoutMs)
{
    InfVerifResult r;
    wstring cmd = quoteArg(tool);
    for (const wstring &a : infVerifArguments(modeKey, file))
        cmd += L" " + quoteArg(a);
    r.commandLine = cmd;

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        r.error = lastErrorText(GetLastError());
        return r;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    wstring mutableCmd = cmd;
    const BOOL ok = CreateProcessW(tool.c_str(), mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   nullptr, &si, &pi);
    const DWORD startError = GetLastError();
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        r.error = lastErrorText(startError);
        return r;
    }
    r.started = true;

    // чтение до закрытия канала: процесс завершается, когда весь вывод прочитан
    std::string bytes;
    char buf[4096];
    DWORD read = 0;
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (true) {
        DWORD avail = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr))
            break;
        if (avail > 0) {
            if (!ReadFile(readPipe, buf, sizeof(buf), &read, nullptr) || read == 0)
                break;
            bytes.append(buf, read);
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) {
            // дочитать остаток
            while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0
                   && ReadFile(readPipe, buf, sizeof(buf), &read, nullptr) && read > 0)
                bytes.append(buf, read);
            break;
        }
        if (GetTickCount64() > deadline) {
            TerminateProcess(pi.hProcess, 1);
            r.error = L"InfVerif не завершился за отведённое время";
            break;
        }
    }
    WaitForSingleObject(pi.hProcess, 5000);
    GetExitCodeProcess(pi.hProcess, &r.exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(readPipe);

    // консольные программы пишут в кодовой странице OEM
    if (!bytes.empty()) {
        const int n = MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), int(bytes.size()), nullptr, 0);
        r.output.resize(size_t(n));
        MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), int(bytes.size()), r.output.data(), n);
    }
    r.diagnostics = parseInfVerifOutput(r.output, &r.valid);
    return r;
}

std::vector<Diagnostic> parseInfVerifOutput(const wstring &output, bool *valid)
{
    static const std::wregex re(LR"(^\s*(ERROR|WARNING|INFO)\((\d+)\)\s+in\s+.*?,\s*line\s+(\d+)\s*:\s*(.*)$)",
                                std::regex::icase);
    static const std::wregex plain(LR"(^\s*(ERROR|WARNING)\((\d+)\)\s*:?\s*(.*)$)", std::regex::icase);

    std::vector<Diagnostic> out;
    if (valid) {
        const wstring u = lower(output);
        *valid = u.find(L"not valid") == wstring::npos;
    }
    for (wstring line : split(output, L'\n')) {
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        std::wsmatch m;
        Diagnostic d;
        d.source = L"InfVerif";
        if (std::regex_match(line, m, re)) {
            const wstring kind = lower(m[1].str());
            d.severity = kind == L"error" ? Severity::Error : kind == L"warning" ? Severity::Warning : Severity::Info;
            d.code = m[2].str();
            d.line = std::max(0, toInt(m[3].str()) - 1);
            d.message = trim(m[4].str());
            out.push_back(d);
        } else if (std::regex_match(line, m, plain)) {
            d.severity = iequals(m[1].str(), L"ERROR") ? Severity::Error : Severity::Warning;
            d.code = m[2].str();
            d.line = 0;
            d.message = trim(m[3].str());
            out.push_back(d);
        }
    }
    return out;
}

} // namespace inf
