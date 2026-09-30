针对基于 **Qt (C++ / QML)** 开发的跨平台（Windows / macOS / Linux）多语言本地漫画阅读器，以下是完整的落地规划方案。

---

## 一、 系统架构设计

```
┌─────────────────────────────────────────────────────────────┐
│                    UI 层 (QML / Quick)                       │
│     书架视图 │ 阅读器界面 │ 图书馆管理 │ 设置面板 (多语言)       │
└──────────────────────────────┬──────────────────────────────┘
                               │ C++ / QML 交互层 (Q_PROPERTY / Signals)
┌──────────────────────────────▼──────────────────────────────┐
│                    核心业务逻辑层 (C++)                      │
│   漫画解析器   │  渲染与图像处理  │   数据同步/缓存   │  数据库/元数据   │
│ (Zip/Rar/7z/PDF)│  (OpenGL/Vulkan) │   (Memory/Disk)  │   (SQLite)    │
└──────────────────────────────┬──────────────────────────────┘
                               │ 依赖库支持
┌──────────────────────────────▼──────────────────────────────┐
│                   底层基础库与依赖 (Third-Party)             │
│   libarchive   │  KArchive/Poppler │   SQLite3   │   spdlog     │
└─────────────────────────────────────────────────────────────┘

```

---

## 二、 核心功能模块设计

### 1. 档案与文件解析模块 (Archive Parser)

本地漫画通常以解压包格式（`CBZ`/`CBR`/`CB7`）或传统压缩包/PDF存储。

* **推荐方案**：采用 **`libarchive`** 或 Qt 生态的 **`KArchive`** (KDE 框架组件，支持 Memory Buffer 读取，无需解压到临时目录即可实时读取图片数据)。
* **支持格式**：
* **压缩包**：`.zip`, `.cbz`, `.rar`, `.cbr`, `.7z`, `.cb7`, `.tar`
* **文档格式**：`.pdf`（基于 Poppler 或 PDFium）
* **图像目录**：支持读取本地普通文件夹。



### 2. 高性能图像渲染与处理 (Rendering Engine)

漫画阅读器的核心体验在于“加载速度”与“缩放流畅度”。

* **UI 框架建议**：采用 **QML + Qt Quick**，借助 Hardware Accelerated Scene Graph（OpenGL / Vulkan / Metal / Direct3D）进行渲染。
* **关键优化技术**：
* **异步预加载（Pre-loading Buffer）**：后台线程池（`QThreadPool`）提前读取并解码当前页的前后 2~3 页。
* **超大图与长图切割（Tile Rendering）**：条漫（Webtoon）或单页像素极大时，利用 Texture Tiling 避免显存溢出（OOM）。
* **图像处理（Image Processing）**：提供双页拼页、单页自动切页、自动裁剪白边、色彩矫正/锐化/黑白模式（通过 Custom Shader / Image processing 过滤器）。



### 3. 数据管理与元数据提取 (Database & Library)

* **数据库**：使用 **SQLite** (Qt 内部 `QSqlDatabase`) 管理阅读历史、书签、收藏夹、标签系统（Tag System）。
* **元数据支持**：解析 `ComicInfo.xml`（ComicRack 标准元数据格式）并建立本地索引。
* **高频扫描**：通过 `QFileSystemWatcher` 与异步文件扫描，保证快速加载上万本本地漫画库。

### 4. 阅读模式 (Reading Modes)

* **单页模式** (Single Page)
* **双页模式** (Double Page / 自动判断封面/从右往左日漫模式)
* **条漫/连续滚动模式** (Continuous Vertical Scroll / Webtoon)

---

## 三、 国际化与多语言方案 (i18n)

Qt 提供了原生的国际化支持框架（`QTranslator` & `lupdate`/`lrelease` 工具链）。

### 1. 多语言架构设计

支持语言：**中文 (zh_CN / zh_TW)**、**英文 (en_US)**、**法文 (fr_FR)**。

* **翻译资源放置**：
```text
assets/
└── translations/
    ├── app_zh_CN.qm
    ├── app_en_US.qm
    └── app_fr_FR.qm

```


* **动态切换机制**：在 C++ 侧封装 `LanguageManager` 类并注入 QML，实现**无需重启应用**实时切换 UI 语言。

### 2. 代码中的多语言规范

* **C++ 代码**：所有用户可见字符串必须使用 `tr()` 包裹。
```cpp
QString title = tr("Library");

```


* **QML 代码**：使用 `qsTr()` 包裹字符串。
```qml
Text {
    text: qsTr("Next Page")
}

```



---

## 四、 跨平台适配与技术选型

| 模块/功能         | 选型推荐                                           | 说明                                              |
| ----------------- | -------------------------------------------------- | ------------------------------------------------- |
| **GUI 框架**      | **Qt 6.x (QML / Qt Quick)**                        | 现代化 UI，硬件加速，动画流畅，跨平台适配能力强。 |
| **构建系统**      | **CMake**                                          | Qt6 默认推荐构建工具，易于依赖第三方库。          |
| **解压/归档解析** | **libarchive** / **KArchive**                      | 高效提取流文件，无需临时写入硬盘。                |
| **图像加载**      | **Qt Image formats** + **libwebp**                 | 保证 WebP, PNG, JPEG, AVIF 格式支持。             |
| **日志框架**      | **spdlog** 或 Qt 默认 `qInstallMessageHandler`     | 高性能多线程日志。                                |
| **打包工具**      | **Windows**: `windeployqt` + Inno Setup / NSIS<br> |

<br>**macOS**: `macdeployqt` + create-dmg<br>

<br>**Linux**: AppImage / Flatpak | 自动化打包脚本。 |

---

## 五、 工程骨架与构建说明（已搭建）

### 目录结构

```text
ComicReader/
├── CMakeLists.txt              # 顶层构建脚本
├── run.bat                     # 一键启动脚本（配置 Qt 运行时环境变量）
├── .gitignore
├── src/
│   ├── main.cpp                # 程序入口：QML 引擎 + QTranslator
│   └── core/                   # 核心业务逻辑（按模块规划，见 src/core/README.md）
│       ├── archive/            #   档案解析器
│       ├── rendering/          #   渲染与图像处理
│       ├── database/           #   SQLite 数据管理
│       └── metadata/           #   ComicInfo.xml 解析
├── qml/
│   └── Main.qml                # 主窗口（书架占位界面）
└── assets/
    └── translations/           # 多语言 .ts 文件
        ├── app_zh_CN.ts
        ├── app_en_US.ts
        └── app_fr_FR.ts
```

### 依赖环境（当前机器）

| 组件 | 版本 / 路径 |
| ---- | ----------- |
| Qt | 6.9.1（来自 `E:\vcpkg-export\installed\x64-windows`） |
| 编译器 | MSVC 19.51（Visual Studio 2026，x64） |
| 构建工具 | CMake + Ninja |

> 说明：本地 vcpkg（`D:\Z-CODER\vcpkg`）下载 Qt 源码速度过慢（约 20 KB/s），
> 因此直接复用已导出的 `E:\vcpkg-export` 预编译库，跳过数小时的编译过程。

### 配置与编译

```powershell
# 1. 进入 MSVC 开发环境并配置（首次）
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
cmake -B build -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE=E:/vcpkg-export/scripts/buildsystems/vcpkg.cmake ^
  -DCMAKE_CXX_COMPILER=cl ^
  -DCMAKE_BUILD_TYPE=Release

# 2. 编译
cmake --build build --config Release
```

> 注：必须显式指定 `CMAKE_TOOLCHAIN_FILE` 指向 `E:/vcpkg-export`。
> 本机环境变量 `CMAKE_TOOLCHAIN_FILE` 指向另一个未完成的 vcpkg 安装，
> 若不覆盖会导致 `find_package(ZLIB)` 命中错误的 zlib。

### 运行

直接双击 `run.bat`，或指定要打开的漫画：

```powershell
.\run.bat                                  # 从书架界面打开
build\ComicReader.exe "D:\Comics\foo.cbz"  # 直接打开指定漫画
```

> 运行时需要以下环境变量（`run.bat` 已自动配置）：
> - `PATH` → `%QTDIR%\bin`（Qt6 DLL）
> - `QML_IMPORT_PATH` → `%QTDIR%\Qt6\qml`（QML 模块）
> - `QT_PLUGIN_PATH` → `%QTDIR%\Qt6\plugins`（`qwindows.dll` 平台插件）

### 归档读取器自检

无需启动 GUI 即可验证解压核心逻辑：

```powershell
build\ComicReader.exe --selftest "D:\Comics\foo.cbz"
```

输出示例：

```
OK: 类型=ZIP 页数=3
  [OK] 第1页 page01.bmp 解压=960054 字节 (期望 960054)
  ...
SELFTEST PASSED (3/3)
```

支持格式：`.zip` / `.cbz`（deflate 解压）与普通图片文件夹。
页面按**自然顺序**排序（`page2` 在 `page10` 之前），自动跳过隐藏文件与
`__MACOSX` 等系统垃圾条目。

### 缓存自检

```powershell
build\ComicReader.exe --cachetest "D:\Comics\foo.cbz"
```

验证 LRU 淘汰顺序、容量上限与缓存内容一致性。

### 数据库自检与查看

```powershell
# 端到端验证：导入 -> 进度持久化 -> 书签 -> 级联删除（使用临时库）
build\ComicReader.exe --dbtest "D:\Comics"

# 查看当前图书馆内容（使用真实库，只读）
build\ComicReader.exe --dbdump

# 手动设置阅读进度（诊断用）
build\ComicReader.exe --dbsetpage <漫画id> <页码>
```

### 元数据自检

```powershell
build\ComicReader.exe --metatest "D:\Comics\some.cbz"
```

解析归档内的 `ComicInfo.xml` 并校验字段，同时验证损坏 XML 与空内容能被拒绝。



### 性能设计

- **页面 LRU 缓存**：默认上限 64 MB，超出后淘汰最久未使用的页面。
- **后台预加载**：翻页后自动预读「前 1 页 + 后 3 页」，由 `QThreadPool`
  执行，避免阻塞 GUI 线程。
- **线程安全**：`QFile` 非线程安全，`ZipArchiveReader::pageData()` 使用
  `QMutex` 保护；缓存自身加锁读写；漫画重开/关闭时通过 `generation`
  计数让在途的预加载结果作废。
- 缓存命中时**不再重复解压**，翻页基本为纯内存操作。

### 书架界面

- 以网格展示数据库中的漫画：封面（自动取归档第 1 页并缓存）、标题、作者/系列
- 封面下方显示阅读进度条与页码角标
- 工具栏：Add Folder...（扫描导入）、Open File...、Refresh（Ctrl+R）
- 封面由 `ComicCoverProvider` 在渲染线程按需生成，带内存缓存；
  路径经模型的线程安全快照获取，渲染线程不直接访问数据库

### 已解压的图片文件夹

**直接把"含图片的文件夹"当作一本漫画**，与 zip/cbz 等价对待：

- **阅读**：`FolderArchiveReader` 天然支持，Ctrl+Shift+O 可直接打开
- **入库**：扫描时"直接含图片的目录"即识别为一本
- **递归扫描**：支持 `系列/Vol.01` 这类嵌套结构（最大深度 4 层），
  例如 `D:\Comics\葬送のフリーレン\Vol.01\*.jpg`
- **边界处理**：
  - 已是漫画的目录不再深入，避免把同一本拆成多本
  - 扫描目标本身就是图片文件夹时，也会直接入库
  - 跳过隐藏文件/目录（`.` 开头）
- 目录标题保留完整名称（`Vol.01` 不会被截断成 `Vol`）
**QML 上下文属性注意事项**：`controller` / `libraryModel` / `coverProvider`
通过 `QQmlContext::setContextProperty` 注入时，QML 中**不能声明同名 property**
（会遮蔽上下文属性导致恒为 null），因此统一改名为 `appController` / `appLibrary` /
`appCover`。同理，子组件中不要把属性命名为 `model`（QML 保留名）。

### 7-Zip 支持（.rar / .cbr / .7z / .cb7）——**未完成，暂不参与构建**

**网络状况**：已明显改善。vcpkg 代理下载速度从最初的 ~20 KB/s 提升到
~740 KB/s（约 36 倍），`vcpkg install 7zip:x64-windows` 仅耗时 **1.3 分钟**。
因此瓶颈已不是网络，而是 **vcpkg 的 7zip 端口只提供内部头文件**：

- 已安装：`7zip.dll`（导出 `CreateObject` 等 17 个函数）、C/CPP 头文件
- **缺失**：`7zip.h`（声明 `CreateObject`）、`CInArchive`、`CInFile` 等常用封装
- 7-Zip 26.x 的 `IInArchive` 与经典 SDK 签名不同：
  `Open(IInStream*, ...)` 需要自行实现输入流，
  `Extract(..., IArchiveExtractCallback*)` 需要自行实现解压回调

**当前状态**：

- `src/core/archive/SevenZipArchiveReader.cpp`：已写好读取器骨架
  （内存输出流、打开回调、条目枚举、自然排序、线程安全互斥），
  但**尚未补齐 `IInStream` 与 `IArchiveExtractCallback`**，暂时无法编译通过
- 已用 `option(COMICREADER_ENABLE_7ZIP OFF)` 将其**排除出默认构建**，
  现有功能不受影响

启用方式（需先补完上述两个接口）：

```powershell
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=... -DCOMICREADER_ENABLE_7ZIP=ON
```

**替代方案**（更简单、零编译成本）：集成**外部 7-Zip 可执行文件**，
在需要时调用 `7z.exe` 列出/解压指定条目，避免 COM 绑定。

### 已知问题

- **翻译未编译**：当前 Qt 未包含 `qttools`（`lrelease`）组件，
  `CMakeLists.txt` 中已将其改为可选（`find_package(... LinguistTools QUIET)`），
  缺失时自动跳过 `.qm` 生成，程序以默认语言启动。
  安装 `qttools[linguist]` 后即可启用中/英/法三语言。

---

## 六、 项目实施路线图 (Roadmap)

### 第一阶段：MVP (最小可行性产品) - 核心阅读体验

* [x] 搭建 Qt6 + CMake 跨平台基础工程骨架。
* [x] 实现基础解压读取器（支持加载 `.zip` / `.cbz` / 图像文件夹）。
* [x] 基于 QML 完成单页阅读界面，支持翻页、缩放与自适应屏幕宽度/高度。
* [ ] 集成 `QTranslator`，实现中/英/法 3 种语言的静态界面支持。
    （界面字符串已用 `qsTr()` 包裹并提供 `.ts` 文件，待安装 `qttools` 后启用编译）

### 第二阶段：核心功能增强与流畅度优化

* [x] 加入后台线程预加载机制与图片缓存池（LRU Cache）。
* [x] 实现日漫模式（双页从右往左）与条漫模式（Webtoon 垂直滚动）。
    - 顶部工具栏可随时切换「单页 / 双页 / 条漫」
    - 双页支持日漫右起阅读（右页为奇数页）
    - 条漫基于 `ListView` 委托复用，仅加载可视区域附近页面
    - 新增 `pageSourceSize()` / `prefersDoublePage()` 供界面计算适应尺寸
* [ ] 支持 `.rar`/`.cbr` 与 `.7z` 格式解析。
* [ ] 支持实时动态切换系统语言。

### 第三阶段：本地漫画库管理 (Library)

* [x] 引入 SQLite 数据库，实现漫画扫描与本地图书库构建。
    - `comics` / `bookmarks` 两张表，书签外键级联删除
    - 打开漫画时自动入库，下次打开自动恢复到上次阅读位置
    - 数据库位于 `%APPDATA%\ComicReader\ComicReader\comicreader.db`
* [x] 支持读取 `ComicInfo.xml` 元数据并展示封面、作者、分类等信息。
    - 解析 ComicRack 标准字段（标题/系列/作者/标签/语言/分级/日期等）
    - 支持从 cbz 内部读取，自动写入数据库并覆盖文件名
    - 旧数据库自动增量迁移新增列
* [x] 添加阅读历史、进度记录、书签管理功能。
    - 进度在关闭漫画与应用退出时落盘

### 第四阶段：高级特性与跨平台打包发布

* [ ] 针对 Windows / macOS (Apple Silicon & Intel) / Linux 进行 UI 缩放与快捷键适配。
* [ ] 图像着色器优化（自动去噪、黑白增强、滤镜处理）。
* [ ] 配置 CI/CD 自动化构建流程（GitHub Actions），自动打包三端发布文件。