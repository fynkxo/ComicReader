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
    QuickDialogs2
    Sql
)

qt_standard_project_setup()

# ---- 可执行目标 ----
qt_add_executable(ComicReader
    src/main.cpp
    src/core/archive/IComicArchive.h
    src/core/archive/ArchiveFactory.cpp
    src/core/archive/ZipArchiveReader.cpp
    # 7-Zip 支持：仅在 -DCOMICREADER_ENABLE_7ZIP=ON 时编译
    $<$<BOOL:${COMICREADER_ENABLE_7ZIP}>:src/core/archive/SevenZipArchiveReader.cpp>
    src/core/archive/ComicReaderController.h
    src/core/archive/ComicReaderController.cpp
    src/core/archive/PageCache.h
    src/core/archive/PageCache.cpp
    src/core/database/LibraryDatabase.h
    src/core/database/LibraryDatabase.cpp
    src/core/database/LibraryModel.h
    src/core/database/LibraryModel.cpp
    src/core/metadata/ComicInfoParser.h
    src/core/metadata/ComicInfoParser.cpp
)

# ---- ZIP 解压依赖（zlib）----
# 由 E:/vcpkg-export 的 vcpkg toolchain 提供（见 CMAKE_TOOLCHAIN_FILE）。
# 注：vcpkg 的 zlib 不提供 ZLIBConfig.cmake，依赖其 wrapper 回退到 FindZLIB 模块。
find_package(ZLIB REQUIRED)

target_link_libraries(ComicReader PRIVATE ZLIB::ZLIB)

# ---- 7-Zip 引擎（可选）：支持 .rar / .cbr / .7z / .cb7 ----
# 缺失时自动禁用 7-Zip 支持，其余功能不受影响。
#
# 本项目的 Qt/zlib 来自 E:\vcpkg-export（导出树，无 ports/ 无法安装新包），
# 而 7-Zip 是独立编译、无 ABI 依赖的库，因此可安全地引用另一棵 vcpkg 树。
set(SEVENZIP_SEARCH_ROOT "" CACHE PATH "7-Zip 所在的 vcpkg installed 目录")
if(NOT SEVENZIP_SEARCH_ROOT)
    foreach(candidate
            "$ENV{VCPKG_ROOT}/installed/x64-windows"
            "D:/Z-CODER/vcpkg/installed/x64-windows")
        if(EXISTS "${candidate}/share/7zip")
            set(SEVENZIP_SEARCH_ROOT "${candidate}")
            break()
        endif()
    endforeach()
endif()

if(SEVENZIP_SEARCH_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${SEVENZIP_SEARCH_ROOT}")
endif()

# 开关：7-Zip 引擎支持处于"实现完成、待验证"状态。
# vcpkg 的 7zip 端口只提供内部头文件（缺少 CInArchive / CInFile 等封装），
# 需自行实现 IInStream 与 IArchiveExtractCallback，暂不纳入默认构建。
option(COMICREADER_ENABLE_7ZIP "启用 7-Zip 支持（.rar/.cbr/.7z/.cb7）" OFF)

find_package(7zip CONFIG QUIET)
if(7zip_FOUND AND COMICREADER_ENABLE_7ZIP)
    target_link_libraries(ComicReader PRIVATE 7zip::7zip)
    target_compile_definitions(ComicReader PRIVATE COMICREADER_HAS_7ZIP)
    message(STATUS "7-Zip support enabled: .rar / .cbr / .7z / .cb7")
else()
    message(STATUS "7-Zip support DISABLED (set -DCOMICREADER_ENABLE_7ZIP=ON to try)")
endif()

# ---- QML 资源（编译进可执行文件，路径与 main.cpp 中的 qrc:/qml/Main.qml 对应）----
# 注：Qt6 的 qt_add_executable 不会处理 .qrc 源文件，必须用 qt_add_resources。
qt_add_resources(ComicReader "qmlresources"
    PREFIX "/"
    FILES
        qml/LibraryView.qml
        qml/Main.qml
        qml/ReaderView.qml
        qml/SinglePageView.qml
        qml/DoublePageView.qml
        qml/WebtoonView.qml
)

# ---- 链接 Qt 库 ----
target_link_libraries(ComicReader
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickDialogs2
        Qt6::Sql
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
