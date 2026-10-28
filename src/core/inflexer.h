#pragma once

// Лексический разбор одной физической строки INF-файла.
// Используется и парсером, и подсветкой синтаксиса, поэтому работает
// построчно: состояние между строками — только признак продолжения (\).

#include "infstr.h"

#include <vector>

namespace inf {

enum class TokenKind {
    SectionHeader,  // [имя секции] вместе со скобками
    Key,            // часть записи до знака '='
    Equals,         // '='
    Value,          // значение без кавычек
    QuotedString,   // значение в кавычках вместе с кавычками
    Comma,          // ','
    Comment,        // от ';' до конца строки
    Continuation,   // завершающий '\'
    Error           // нераспознанный или ошибочный фрагмент
};

struct Token {
    TokenKind kind;
    int start = 0;
    int length = 0;
};

struct LexResult {
    std::vector<Token> tokens;
    bool continues = false;      // строка заканчивается '\' и продолжается следующей
    bool unterminatedQuote = false;
    wstring error;               // текст синтаксической ошибки, если есть
};

// Разбирает физическую строку. continued = true, если предыдущая строка
// закончилась символом продолжения: в такой строке ключа быть не может.
LexResult lexLine(const wstring &line, bool continued);

// Снимает кавычки: "a""b" -> a"b. Для строк без кавычек возвращает обрезанный текст.
wstring unquote(const wstring &raw);

// Заключает значение в кавычки, если без них оно будет разобрано иначе.
wstring quoteIfNeeded(const wstring &value);

// Всегда заключает значение в кавычки (удваивая внутренние кавычки).
wstring quote(const wstring &value);

} // namespace inf
