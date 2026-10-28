// Открытый в оснастке INF-файл: текст, формат, результаты анализа,
// история изменений и результаты InfVerif
#pragma once

#include "infanalysis.h"
#include "inffile.h"
#include "infverif.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Преобразование текста файла. Получает актуальный анализ и текст;
// при ошибке возвращает false и текст ошибки.
using Transform = std::function<bool(const inf::Analysis &a, std::wstring &text, std::wstring &error)>;

class InfFileDoc {
public:
    explicit InfFileDoc(int id) : m_id(id) {}

    int id() const { return m_id; }

    bool load(const std::wstring &path, std::wstring &error);
    void setNew(const std::wstring &text, const std::wstring &suggestedName);
    bool save(std::wstring &error);
    bool saveAs(const std::wstring &path, std::wstring &error);

    const std::wstring &path() const { return m_path; }
    const std::wstring &text() const { return m_text; }
    const inf::FileFormat &format() const { return m_format; }
    std::shared_ptr<const inf::Analysis> analysis() const { return m_analysis; }
    bool isModified() const { return m_modified; }
    bool isUntitled() const { return m_path.empty(); }
    bool decodedLossy() const { return m_lossy; }

    std::wstring fileName() const;       // имя файла без пути
    std::wstring title() const;          // имя файла и признак изменения

    // Заменить текст (с записью в историю отмены)
    void setText(const std::wstring &text);
    bool apply(const Transform &t, std::wstring &error);
    void setFormat(const inf::FileFormat &f);

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    bool undo();
    bool redo();

    // Изменён ли файл на диске после загрузки или сохранения
    bool changedOnDisk() const;
    void acknowledgeDiskChange() { rememberDiskTime(); }

    // InfVerif
    void setVerifResult(const inf::InfVerifResult &r);
    bool verifRan() const { return m_verifRan; }
    bool verifValid() const { return m_verifValid; }
    const std::wstring &verifOutput() const { return m_verifOutput; }

    // Все сообщения: собственная проверка и InfVerif
    std::vector<inf::Diagnostic> diagnostics() const;
    int count(inf::Severity s) const;

private:
    void reanalyze();
    void rememberDiskTime();

    int m_id;
    std::wstring m_path;
    std::wstring m_suggestedName;
    std::wstring m_text;
    inf::FileFormat m_format;
    bool m_modified = false;
    bool m_lossy = false;
    std::shared_ptr<const inf::Analysis> m_analysis;
    std::vector<std::wstring> m_undo;
    std::vector<std::wstring> m_redo;
    FILETIME m_diskTime{};

    bool m_verifRan = false;
    bool m_verifValid = false;
    std::wstring m_verifOutput;
    std::vector<inf::Diagnostic> m_verifDiagnostics;
};
