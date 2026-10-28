// Общий предкомпилированный заголовок оснастки
#pragma once

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif

#include <afxwin.h>
#include <afxdlgs.h>
#include <afxcmn.h>
#include <afxdtctl.h>
#include <atlimage.h>

#include <mmc.h>
#include <richedit.h>
#include <richole.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tom.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Переключение состояния модуля MFC в каждой точке входа из MMC
#define MANAGE_STATE() AFX_MANAGE_STATE(AfxGetStaticModuleState())
