# INF Editor — оснастка MMC для редактирования INF-файлов драйверов Windows
cmake_minimum_required(VERSION 3.21)
project(inf_editor VERSION 2.0.1 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    message(FATAL_ERROR "Оснастка MMC собирается только компилятором MSVC")
endif()

# статическая среда выполнения (и MFC): оснастке не нужен VC++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00)
add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /MP)

# Ядро: разбор, анализ, проверка и генерация INF без зависимостей от интерфейса
add_library(infcore STATIC
    src/core/infstr.cpp
    src/core/inflexer.cpp
    src/core/infdocument.cpp
    src/core/inffile.cpp
    src/core/infknowledge.cpp
    src/core/infmodel.cpp
    src/core/infvalidator.cpp
    src/core/infedit.cpp
    src/core/infgenerator.cpp
    src/core/infverif.cpp
    src/core/infhelp.cpp
    src/core/cabwriter.cpp
    src/core/drvstore.cpp
)
target_include_directories(infcore PUBLIC src/core)
target_link_libraries(infcore PUBLIC ole32 user32 setupapi cabinet)

# Тесты ядра
add_executable(inf_editor_tests tests/tst_inf_core.cpp)
target_link_libraries(inf_editor_tests PRIVATE infcore shell32)
enable_testing()
add_test(NAME inf_editor_tests COMMAND inf_editor_tests)

option(INF_EDITOR_SNAPIN "Собирать оснастку MMC" ON)

# Оснастка MMC (MFC подключена статически)
if(INF_EDITOR_SNAPIN)
add_library(inf_snapin SHARED
    src/snapin/stdafx.cpp
    src/snapin/snapinapp.cpp
    src/snapin/registration.cpp
    src/snapin/dataobject.cpp
    src/snapin/componentdata.cpp
    src/snapin/component.cpp
    src/snapin/about.cpp
    src/snapin/nodes.cpp
    src/snapin/inffiledoc.cpp
    src/snapin/images.cpp
    src/snapin/pages.cpp
    src/snapin/dialogs.cpp
    src/snapin/texteditor.cpp
    src/snapin/wizard.cpp
    src/snapin/inf_snapin.rc
    src/snapin/inf_snapin.def
)
target_include_directories(inf_snapin PRIVATE src/snapin)
target_compile_definitions(inf_snapin PRIVATE _USRDLL _WINDLL APP_VERSION=L"${PROJECT_VERSION}")
set_target_properties(inf_snapin PROPERTIES
    OUTPUT_NAME inf_snapin)
target_precompile_headers(inf_snapin PRIVATE src/snapin/stdafx.h)
# статическая MFC указывается перед mmc.lib: иначе символы MFC берутся из её модуля apimfc.obj
target_link_libraries(inf_snapin PRIVATE infcore $<IF:$<CONFIG:Debug>,uafxcwd,uafxcw> mmc comctl32 shell32 shlwapi uxtheme)
endif()
