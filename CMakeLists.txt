cmake_minimum_required(VERSION 3.21)

project(ComicReader
    VERSION 0.1.0
    DESCRIPTION "Cross-platform local comic reader (Qt6 / QML)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Qt 6 ----
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
)

qt_standard_project_setup()

# ---- 可执行目标 ----
qt_add_executable(ComicReader
    src/main.cpp
)

# ---- QML 资源（编译进可执行文件，路径与 main.cpp 中的 qrc:/qml/Main.qml 对应）----
# 注：Qt6 的 qt_add_executable 不会处理 .qrc 源文件，必须用 qt_add_resources。
qt_add_resources(ComicReader "qmlresources"
    PREFIX "/"
    FILES
        qml/Main.qml
)

# ---- 链接 Qt 库 ----
target_link_libraries(ComicReader
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

# ---- 平台属性 ----
set_target_properties(ComicReader PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.comicreader.app"
    MACOSX_BUNDLE_BUNDLE_NAME "Comic Reader"
)

# ---- 多语言翻译（可选：依赖 Qt6::LinguistTools / lrelease）----
# 注：若未安装 qttools 组件，则跳过翻译生成，应用以默认语言运行。
find_package(Qt6 COMPONENTS LinguistTools QUIET)
if(COMMAND qt_add_translations)
    qt_add_translations(ComicReader
        TS_FILES
            assets/translations/app_zh_CN.ts
            assets/translations/app_en_US.ts
            assets/translations/app_fr_FR.ts
    )
endif()

# ---- 安装规则 ----
install(TARGETS ComicReader
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
