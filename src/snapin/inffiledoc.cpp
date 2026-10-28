#include "stdafx.h"

#include "inffiledoc.h"

#include "infverif.h"

#include <algorithm>

namespace {

constexpr size_t kMaxUndo = 100;

bool fileTime(const std::wstring &path, FILETIME &out)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return false;
    out = data.ftLastWriteTime;
    return true;
}

} // namespace

bool InfFileDoc::load(const std::wstring &path, std::wstring &error)
{
    inf::DecodedFile file;
    if (!inf::readFile(path, file, &error))
        return false;
    m_path = path;
    m_text = file.text;
    m_format = file.format;
    m_lossy = file.lossy;
    m_modified = false;
    m_undo.clear();
    m_redo.clear();
    m_verifRan = false;
    m_verifDiagnostics.clear();
    m_verifOutput.clear();
    rememberDiskTime();
    reanalyze();
    return true;
}

void InfFileDoc::setNew(const std::wstring &text, const std::wstring &suggestedName)
{
    m_path.clear();
    m_suggestedName = suggestedName;
    m_text = text;
    m_format = inf::FileFormat();
    m_modified = !text.empty();
    reanalyze();
}

bool InfFileDoc::save(std::wstring &error)
{
    if (m_path.empty()) {
        error = L"Не задано имя файла";
        return false;
    }
    if (!inf::writeFile(m_path, m_text, m_format, &error))
        return false;
    m_modified = false;
    m_lossy = false;
    rememberDiskTime();
    return true;
}

bool InfFileDoc::saveAs(const std::wstring &path, std::wstring &error)
{
    const std::wstring old = m_path;
    m_path = path;
    if (!save(error)) {
        m_path = old;
        return false;
    }
    return true;
}

std::wstring InfFileDoc::fileName() const
{
    if (!m_path.empty())
        return inf::fileName(m_path);
    return m_suggestedName.empty() ? L"Без имени.inf" : m_suggestedName;
}

std::wstring InfFileDoc::title() const
{
    return fileName() + (m_modified ? L" *" : L"");
}

void InfFileDoc::setText(const std::wstring &text)
{
    if (text == m_text)
        return;
    m_undo.push_back(m_text);
    if (m_undo.size() > kMaxUndo)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_text = text;
    m_modified = true;
    // результаты InfVerif относятся к прежнему тексту
    m_verifDiagnostics.clear();
    reanalyze();
}

bool InfFileDoc::apply(const Transform &t, std::wstring &error)
{
    std::wstring text = m_text;
    if (!t(*m_analysis, text, error))
        return false;
    setText(text);
    return true;
}

void InfFileDoc::setFormat(const inf::FileFormat &f)
{
    m_format = f;
    m_modified = true;
}

bool InfFileDoc::undo()
{
    if (m_undo.empty())
        return false;
    m_redo.push_back(m_text);
    m_text = m_undo.back();
    m_undo.pop_back();
    m_modified = true;
    m_verifDiagnostics.clear();
    reanalyze();
    return true;
}

bool InfFileDoc::redo()
{
    if (m_redo.empty())
        return false;
    m_undo.push_back(m_text);
    m_text = m_redo.back();
    m_redo.pop_back();
    m_modified = true;
    m_verifDiagnostics.clear();
    reanalyze();
    return true;
}

bool InfFileDoc::changedOnDisk() const
{
    if (m_path.empty())
        return false;
    FILETIME t{};
    if (!fileTime(m_path, t))
        return false;
    return CompareFileTime(&t, &m_diskTime) != 0;
}

void InfFileDoc::rememberDiskTime()
{
    m_diskTime = FILETIME{};
    fileTime(m_path, m_diskTime);
}

void InfFileDoc::reanalyze()
{
    m_analysis = inf::Analysis::run(m_text);
}

void InfFileDoc::setVerifResult(const inf::InfVerifResult &r)
{
    m_verifRan = r.started;
    m_verifValid = r.valid;
    m_verifOutput = r.commandLine + L"\r\n\r\n" + inf::replaceAll(r.output, L"\r\n", L"\n");
    m_verifOutput = inf::replaceAll(m_verifOutput, L"\n", L"\r\n");
    if (!r.error.empty())
        m_verifOutput += L"\r\n" + r.error;
    m_verifDiagnostics = r.diagnostics;
}

std::vector<inf::Diagnostic> InfFileDoc::diagnostics() const
{
    std::vector<inf::Diagnostic> all = m_analysis ? m_analysis->diagnostics : std::vector<inf::Diagnostic>();
    all.insert(all.end(), m_verifDiagnostics.begin(), m_verifDiagnostics.end());
    std::stable_sort(all.begin(), all.end(), [](const inf::Diagnostic &a, const inf::Diagnostic &b) { return a.line < b.line; });
    return all;
}

int InfFileDoc::count(inf::Severity s) const
{
    int n = 0;
    for (const inf::Diagnostic &d : diagnostics())
        if (d.severity == s)
            ++n;
    return n;
}
