// Диалог правки текста INF (файла целиком или секции) с подсветкой синтаксиса
#pragma once

#include <string>

// line — строка (с нуля), на которую поставить курсор; -1 — начало текста.
// Возвращает true, если текст изменён и подтверждён кнопкой «ОК».
bool runTextEditor(CWnd *parent, const std::wstring &title, std::wstring &text, int line);
