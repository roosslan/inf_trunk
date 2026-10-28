// Страницы свойств оснастки. Окно свойств MMC работает в отдельном потоке,
// поэтому страница получает снимок данных, а изменения передаёт в основной
// поток через MMCPropertyChangeNotify.
#pragma once

#include "componentdata.h"

bool targetHasPages(const Target &t);
HRESULT addPropertyPages(const Target &t, CComponentData &owner, LPPROPERTYSHEETCALLBACK provider, LONG_PTR handle);
