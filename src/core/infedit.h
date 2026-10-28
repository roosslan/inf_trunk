#pragma once

// Точечные изменения текста INF: замена значения ключа, удаление записи,
// добавление строк в секцию, переименование и удаление секций. Изменения
// вычисляются по разобранному документу и затрагивают только нужные строки —
// остальное форматирование не меняется.

#include "infdocument.h"
#include "infmodel.h"

#include <vector>

namespace inf {

struct TextEdit {
    int firstLine = 0;      // физическая строка, с которой начинается изменение
    int removeCount = 0;    // сколько строк удалить (0 — только вставка перед firstLine)
    StringList lines;       // строки, которые вставляются на место удалённых

    bool isNull() const { return removeCount == 0 && lines.empty(); }
};

// Замена фрагмента в пределах одной физической строки
struct SpanEdit {
    int line = 0;
    int column = 0;
    int length = 0;
    wstring text;
};

// Строка вида "ключ = значение1,значение2"
wstring formatEntry(const wstring &key, const StringList &values);

// Установить значение ключа (значение уже отформатировано: кавычки и запятые на вызывающей стороне).
// Если ключа нет — добавляется в конец секции; если нет секции — создаётся.
TextEdit setValueEdit(const InfDocument &doc, const wstring &section, const wstring &key, const wstring &value);

// Удалить первую запись с ключом. Возвращает пустое изменение, если ключа нет.
TextEdit removeKeyEdit(const InfDocument &doc, const wstring &section, const wstring &key);

// Удалить логическую строку целиком
TextEdit removeLineEdit(const InfDocument &doc, int logicalLine);

// Заменить логическую строку новым текстом (комментарий в конце строки сохраняется,
// если keepComment = true)
TextEdit replaceLineEdit(const InfDocument &doc, int logicalLine, const wstring &newText, bool keepComment = true);

// Добавить строки в конец секции (секция создаётся при отсутствии)
TextEdit appendToSectionEdit(const InfDocument &doc, const wstring &section, const StringList &lines);

// Добавить новую секцию целиком (строки включают заголовок) в конец файла
TextEdit appendSectionEdit(const InfDocument &doc, const StringList &lines);

// Удалить секцию со всеми фрагментами (изменения отсортированы снизу вверх)
std::vector<TextEdit> removeSectionEdits(const InfDocument &doc, const wstring &section);

// Заменить текст первого фрагмента секции (от заголовка до последней непустой строки)
TextEdit replaceSectionEdit(const InfDocument &doc, const wstring &section, const StringList &lines);

// Текст первого фрагмента секции (от заголовка до последней непустой строки)
StringList sectionText(const InfDocument &doc, const wstring &section);

// Переименовать секцию: заголовки и явные ссылки на неё
std::vector<SpanEdit> renameSectionEdits(const InfDocument &doc, const InfModel &model, const wstring &section,
                                         const wstring &newName);

// Применить изменения к тексту
wstring applyEdit(const wstring &text, const TextEdit &edit);
wstring applyEdits(const wstring &text, std::vector<TextEdit> edits);   // непересекающиеся
wstring applySpans(const wstring &text, std::vector<SpanEdit> edits);   // непересекающиеся

} // namespace inf
