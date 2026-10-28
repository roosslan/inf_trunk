#pragma once

// Синтаксическая модель INF-файла: логические строки, секции, значения.
// Модель строится из текста и только читает его; текст остаётся единственным
// источником истины, поэтому исходное форматирование никогда не теряется.

#include "infstr.h"

#include <unordered_map>
#include <vector>

namespace inf {

struct InfValue {
    wstring text;       // значение без кавычек
    wstring raw;        // значение так, как оно записано в файле
    bool quoted = false;
    int line = -1;      // физическая строка (с нуля) начала значения
    int column = 0;     // столбец начала значения
    int length = 0;     // длина в пределах первой физической строки

    bool isEmpty() const { return text.empty(); }
};

struct InfLine {
    enum class Kind { Blank, Comment, Section, Entry };

    Kind kind = Kind::Blank;
    int firstLine = 0;  // первая физическая строка
    int lastLine = 0;   // последняя физическая строка (с учётом продолжений '\')
    int section = -1;   // индекс секции, к которой относится строка

    // Для записей
    bool hasKey = false;
    InfValue key;
    std::vector<InfValue> values;

    // Для заголовков секций
    InfValue sectionName;

    wstring comment;    // комментарий вместе с ';'
    wstring error;      // синтаксическая ошибка
    int errorLine = -1;
    int errorColumn = 0;
    int errorLength = 0;

    wstring keyText() const { return hasKey ? key.text : wstring(); }
    wstring value(int i) const { return i >= 0 && i < int(values.size()) ? values[size_t(i)].text : wstring(); }
    InfValue valueAt(int i) const { return i >= 0 && i < int(values.size()) ? values[size_t(i)] : InfValue(); }
    bool isEntry() const { return kind == Kind::Entry; }
};

// Непрерывный фрагмент секции: заголовок и строки до следующего заголовка.
// Секция с одним именем может встречаться в файле несколько раз.
struct InfSectionChunk {
    int headerLine = 0;        // физическая строка заголовка
    int lastLine = 0;          // последняя физическая строка фрагмента
    int lastContentLine = 0;   // последняя непустая строка фрагмента
    int logicalHeader = 0;     // индекс логической строки заголовка
};

struct InfSection {
    wstring name;              // имя в том написании, в каком встретилось впервые
    std::vector<InfSectionChunk> chunks;
    std::vector<int> entries;  // индексы логических строк-записей

    int headerLine() const { return chunks.empty() ? -1 : chunks.front().headerLine; }
};

class InfDocument {
public:
    static InfDocument parse(const wstring &text);

    const StringList &physicalLines() const { return m_physical; }
    wstring physicalLine(int i) const { return i >= 0 && i < int(m_physical.size()) ? m_physical[size_t(i)] : wstring(); }
    const std::vector<InfLine> &lines() const { return m_lines; }
    const std::vector<InfSection> &sections() const { return m_sections; }
    const std::vector<int> &entriesOutsideSections() const { return m_outside; }

    int sectionIndex(const wstring &name) const;
    const InfSection *section(const wstring &name) const;
    bool hasSection(const wstring &name) const { return sectionIndex(name) >= 0; }

    // Все записи секции с заданным ключом (без учёта регистра)
    std::vector<const InfLine *> entries(const wstring &section, const wstring &key) const;
    const InfLine *entry(const wstring &section, const wstring &key) const;
    std::vector<const InfLine *> entries(const InfSection &section) const;

    // Значение первой записи ключа без кавычек; пустая строка, если её нет
    wstring value(const wstring &section, const wstring &key, int index = 0) const;

    // Имена секций, начинающихся с префикса (например, "Strings" -> Strings, Strings.0409)
    StringList sectionsWithPrefix(const wstring &prefix) const;

    // Логическая строка, в которую входит физическая строка
    int logicalLineAt(int physicalLine) const;

    // Текст логической строки целиком (все её физические строки через '\n')
    wstring lineText(int logicalLine) const;

private:
    StringList m_physical;
    std::vector<InfLine> m_lines;
    std::vector<InfSection> m_sections;
    std::vector<int> m_outside;
    std::unordered_map<wstring, int> m_index;
    std::vector<int> m_physToLogical;
};

wstring sectionKey(const wstring &name);

} // namespace inf
