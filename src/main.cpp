#include <QCommandLineParser>
#include <cstdio>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTranslator>
#include <QLocale>
#include <QUrl>
#include <QTextStream>

#include "core/archive/ComicReaderController.h"
#include "core/archive/IComicArchive.h"
#include "core/archive/PageCache.h"
#include "core/database/LibraryDatabase.h"
#include "core/metadata/ComicInfoParser.h"
#include "core/database/LibraryModel.h"

int main(int argc, char *argv[])
{
    // --selftest 模式：命令行解析前提前处理，用于无 GUI 环境下验证解压核心逻辑
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            QTextStream out(stdout);
            if (i + 1 >= argc) {
                out << "FAIL: --selftest 需要提供漫画路径\n";
                return 2;
            }
            const QString target = QFileInfo(QString::fromLocal8Bit(argv[i + 1]))
                                       .absoluteFilePath();
            std::unique_ptr<ComicReader::IComicArchive> archive(
                ComicReader::createComicArchive(target));
            if (!archive) {
                out << "FAIL: 不支持或不存在: " << target << "\n";
                return 2;
            }
            QString error;
            if (!archive->open(target, &error)) {
                out << "FAIL: 打开失败: " << error << "\n";
                return 2;
            }
            const auto pages = archive->pages();
            out << "OK: 类型=" << (archive->type() == ComicReader::ArchiveType::ZipArchive
                                       ? "ZIP" : "Folder")
                << " 页数=" << pages.size() << "\n";
            int ok = 0;
            for (int p = 0; p < pages.size(); ++p) {
                const QByteArray data = archive->pageData(p);
                const bool valid = !data.isEmpty() && data.size() == qint64(pages.at(p).size);
                out << (valid ? "  [OK] " : "  [FAIL] ")
                    << "第" << (p + 1) << "页 " << pages.at(p).name
                    << " 解压=" << data.size() << " 字节 (期望 "
                    << pages.at(p).size << ")\n";
                if (valid)
                    ++ok;
            }
            out << (ok == pages.size() ? "SELFTEST PASSED" : "SELFTEST FAILED")
                << " (" << ok << "/" << pages.size() << ")\n";
            out.flush();
            return ok == pages.size() ? 0 : 1;
        }
        if (qstrcmp(argv[i], "--cachetest") == 0) {
            // 验证 LRU 缓存：同一页二次读取应命中缓存
            QTextStream out(stdout);
            if (i + 1 >= argc) {
                out << "FAIL: --cachetest 需要提供漫画路径\n";
                return 2;
            }
            const QString target = QFileInfo(QString::fromLocal8Bit(argv[i + 1]))
                                       .absoluteFilePath();
            ComicReader::PageCache cache(64ll * 1024 * 1024);
            cache.resetStats();

            // 1) 淘汰逻辑：容量 100 字节，放入 3 个各 60 字节
            ComicReader::PageCache small(100);
            small.insert(0, QByteArray(60, 'a'));
            small.insert(1, QByteArray(60, 'b'));
            small.insert(2, QByteArray(60, 'c'));
            out << "容量淘汰: count=" << small.count()
                << " used=" << small.usedBytes() << "B (上限 100B)\n";
            const bool evictOk = small.usedBytes() <= 100;
            out << (evictOk ? "  [OK] 未超容量\n" : "  [FAIL] 超出容量\n");

            // 2) LRU 顺序：0 被淘汰，2 应仍在
            const bool lruOk = !small.contains(0) && small.contains(2);
            out << (lruOk ? "  [OK] LRU 正确淘汰最久未用页\n"
                          : "  [FAIL] LRU 淘汰顺序错误\n");

            // 3) 真实数据计时
            std::unique_ptr<ComicReader::IComicArchive> archive(
                ComicReader::createComicArchive(target));
            QString error;
            if (!archive || !archive->open(target, &error)) {
                out << "FAIL: 打开失败: " << error << "\n";
                return 2;
            }
            const auto pages = archive->pages();
            if (pages.isEmpty()) {
                out << "FAIL: 无页面\n";
                return 2;
            }

            QElapsedTimer timer;
            timer.start();
            const QByteArray first = archive->pageData(0);
            const qint64 coldMs = timer.elapsed();

            timer.restart();
            cache.insert(0, first);
            const QByteArray second = cache.take(0);
            const qint64 warmMs = timer.elapsed();

            const bool hitOk = !second.isEmpty() && second == first;
            out << "冷读取(解压): " << coldMs << " ms (" << first.size() << " 字节)\n";
            out << "热读取(缓存): " << warmMs << " ms\n";
            out << (hitOk ? "  [OK] 缓存内容与原始数据一致\n"
                          : "  [FAIL] 缓存数据不一致\n");

            // 4) 缓存容量压力测试：缓存仅容纳 2 页，连续写入 6 页应发生淘汰
            //    但任何时刻已写入的页都应能取回（未被淘汰的那部分）
            constexpr int kBenchPageBytes = 2 * 1024 * 1024;   // 2MB/页
            constexpr int kBenchPages = 6;
            ComicReader::PageCache bench(kBenchPageBytes * 2ll);  // 仅容纳 2 页
            QByteArray pageData(kBenchPageBytes, 'x');

            QElapsedTimer benchTimer;
            benchTimer.start();
            for (int p = 0; p < kBenchPages; ++p)
                bench.insert(p, pageData);
            const qint64 fillMs = benchTimer.elapsed();

            // 缓存必须始终不超过容量
            const bool capOk = bench.usedBytes() <= kBenchPageBytes * 2ll;
            // 至少应保留最近写入的页
            const bool newestOk = bench.contains(kBenchPages - 1);
            // 最早写入的页应已被淘汰
            const bool oldestEvicted = !bench.contains(0);

            // 命中路径耗时（共享内存，仅复制引用计数）
            benchTimer.restart();
            qint64 retrieved = 0;
            for (int p = 0; p < kBenchPages; ++p) {
                if (!bench.take(p).isEmpty())
                    ++retrieved;
            }
            const qint64 hitAllMs = benchTimer.elapsed();

            out << "容量压力(" << kBenchPages << " 页 x "
                << (kBenchPageBytes / 1024 / 1024) << "MB, 缓存 2 页): 写入 "
                << fillMs << " ms, 命中 " << retrieved << " 页用时 " << hitAllMs
                << " ms, 占用 " << (bench.usedBytes() / 1024 / 1024) << "MB\n";
            out << (capOk ? "  [OK] 占用未超容量\n" : "  [FAIL] 超出容量\n");
            out << (newestOk ? "  [OK] 保留最新写入的页\n" : "  [FAIL] 最新页被误淘汰\n");
            out << (oldestEvicted ? "  [OK] 最早写入的页已淘汰\n"
                                  : "  [FAIL] 最早页未被淘汰\n");

            const bool benchOk = capOk && newestOk && oldestEvicted;
            const bool allOk = evictOk && lruOk && hitOk && benchOk;
            out << (allOk ? "CACHETEST PASSED" : "CACHETEST FAILED") << "\n";
            out.flush();
            return allOk ? 0 : 1;
        }
        if (qstrcmp(argv[i], "--dbtest") == 0) {
            // 验证 SQLite 图书馆：导入 -> 进度持久化 -> 书签 -> 级联删除
            // 注意：SQL 驱动插件的加载依赖 QCoreApplication 实例，
            // 而本分支在 QGuiApplication 构造之前返回，需就地创建。
            QCoreApplication app(argc, argv);
            QTextStream out(stdout);
            if (i + 1 >= argc) {
                out << "FAIL: --dbtest 需要提供漫画目录\n";
                return 2;
            }
            const QString comicDir =
                QFileInfo(QString::fromLocal8Bit(argv[i + 1])).absoluteFilePath();
            const QString dbPath =
                QDir(QDir::tempPath()).filePath(QStringLiteral("comicreader_dbtest.db"));
            QFile::remove(dbPath);   // 从干净状态开始

            ComicReader::LibraryDatabase db;
            QString error;
            if (!db.open(dbPath, &error)) {
                out << "FAIL: 打开数据库失败: " << error << "\n";
                return 2;
            }
            out << "数据库: " << dbPath << "\n";

            int skipped = 0;
            const int added = db.importDirectory(comicDir, &skipped);
            out << "导入: 新增 " << added << " 本, 跳过 " << skipped << " 项, 库中共 "
                << db.count() << " 本\n";
            if (added <= 0) {
                out << "FAIL: 未导入任何漫画\n";
                return 1;
            }

            // 进度持久化：写入后立即重开数据库验证落盘
            const auto comics = db.allComics();
            if (comics.isEmpty()) {
                out << "FAIL: 列表为空\n";
                return 1;
            }
            const ComicReader::ComicEntry first = comics.first();
            out << "首本: " << first.title << " (" << first.pageCount << " 页)\n";

            if (!db.updateProgress(first.id, 3)) {
                out << "FAIL: 写入进度失败\n";
                return 1;
            }
            db.close();

            ComicReader::LibraryDatabase db2;
            if (!db2.open(dbPath, &error)) {
                out << "FAIL: 重开数据库失败: " << error << "\n";
                return 2;
            }
            const ComicReader::ComicEntry reread = db2.comicByPath(first.path);
            const bool progressOk = reread.currentPage == 3;
            out << (progressOk ? "  [OK] 进度已持久化 (current_page=3)\n"
                               : "  [FAIL] 进度未持久化\n");

            // 书签 + 外键级联删除
            const bool bmAdd = db2.addBookmark(first.id, 1, QStringLiteral("test"));
            const int bmCount = db2.bookmarksFor(first.id).size();
            out << (bmAdd && bmCount == 1 ? "  [OK] 书签已添加\n"
                                          : "  [FAIL] 书签添加失败\n");

            db2.removeComic(first.id);
            const int bmAfter = db2.bookmarksFor(first.id).size();
            out << (bmAfter == 0 ? "  [OK] 删除漫画时书签级联清理\n"
                                  : "  [FAIL] 书签未级联删除\n");
            db2.close();
            QFile::remove(dbPath);

            const bool allOk = progressOk && bmAdd && bmCount == 1 && bmAfter == 0;
            out << (allOk ? "DBTEST PASSED" : "DBTEST FAILED") << "\n";
            out.flush();
            return allOk ? 0 : 1;
        }
        if (qstrcmp(argv[i], "--dbdump") == 0) {
            // 打印当前图书馆内容（诊断用）
            QCoreApplication app(argc, argv);
            // 必须与 GUI 模式设置一致，否则 defaultDatabasePath() 会解析到不同位置
            QCoreApplication::setOrganizationName(QStringLiteral("ComicReader"));
            QCoreApplication::setOrganizationDomain(QStringLiteral("comicreader.local"));
            QCoreApplication::setApplicationName(QStringLiteral("ComicReader"));
            QTextStream out(stdout);
            ComicReader::LibraryDatabase db;
            QString error;
            const QString path = ComicReader::LibraryDatabase::defaultDatabasePath();
            if (!db.open(path, &error)) {
                out << "FAIL: 打开数据库失败: " << error << "\n";
                return 2;
            }
            out << "数据库: " << path << "\n";
            out << "共 " << db.count() << " 本\n";
            const auto comics = db.allComics();
            for (const ComicReader::ComicEntry &c : comics) {
                out << "  [" << c.id << "] " << c.title
                    << " 页数=" << c.pageCount
                    << " 进度=" << (c.currentPage + 1) << "/" << c.pageCount
                    << " 书签=" << db.bookmarksFor(c.id).size() << "\n";
                if (!c.series.isEmpty())
                    out << "        系列: " << c.series << "\n";
                if (!c.writer.isEmpty())
                    out << "        作者: " << c.writer << "\n";
                if (!c.publisher.isEmpty())
                    out << "        出版社: " << c.publisher << "\n";
                if (!c.tags.isEmpty())
                    out << "        标签: " << c.tags << "\n";
                if (!c.languageIso.isEmpty() || !c.ageRating.isEmpty())
                    out << "        语言/分级: " << c.languageIso << " / " << c.ageRating << "\n";
                out << "        " << c.path << "\n";
            }
            out.flush();
            return 0;
        }
        if (qstrcmp(argv[i], "--dbsetpage") == 0 && i + 2 < argc) {
            QCoreApplication app(argc, argv);
            QCoreApplication::setOrganizationName(QStringLiteral("ComicReader"));
            QCoreApplication::setOrganizationDomain(QStringLiteral("comicreader.local"));
            QCoreApplication::setApplicationName(QStringLiteral("ComicReader"));
            QTextStream out(stdout);
            ComicReader::LibraryDatabase db;
            QString error;
            if (!db.open(ComicReader::LibraryDatabase::defaultDatabasePath(), &error)) {
                out << "FAIL: " << error << "\n";
                return 2;
            }
            const int id = QString::fromLocal8Bit(argv[i + 1]).toInt();
            const int page = QString::fromLocal8Bit(argv[i + 2]).toInt();
            const bool ok = db.updateProgress(id, page);
            out << (ok ? "已设置漫画 " : "设置失败 ") << id << " 进度为第 "
                << (page + 1) << " 页\n";
            out.flush();
            return ok ? 0 : 1;
        }
        if (qstrcmp(argv[i], "--metatest") == 0) {
            // 验证 ComicInfo.xml 解析：完整字段 + 缺失 + 损坏 XML
            QCoreApplication app(argc, argv);
            QTextStream out(stdout);
            bool allOk = true;

            // 1) 正常解析
            if (i + 1 < argc) {
                std::unique_ptr<ComicReader::IComicArchive> archive(
                    ComicReader::createComicArchive(
                        QFileInfo(QString::fromLocal8Bit(argv[i + 1])).absoluteFilePath()));
                QString error;
                if (archive && archive->open(argv[i + 1], &error)) {
                    ComicReader::ComicMetadata meta;
                    if (ComicReader::ComicInfoParser::parseFromArchive(archive.get(), &meta, &error)) {
                        out << "  标题: " << meta.title << "\n";
                        out << "  系列: " << meta.series << "\n";
                        out << "  作者: " << meta.writer << "\n";
                        out << "  出版社: " << meta.publisher << "\n";
                        out << "  简介: " << meta.summary.left(40) << "...\n";
                        out << "  标签: " << meta.tags.join(", ") << "\n";
                        out << "  语言: " << meta.languageIso
                            << "  分级: " << meta.ageRating << "\n";
                        out << "  页数: " << meta.pageCount
                            << "  发布: " << meta.publishDate.toString(Qt::ISODate) << "\n";
                        const bool ok = !meta.title.isEmpty() && !meta.series.isEmpty()
                                        && !meta.writer.isEmpty() && meta.pageCount == 3
                                        && meta.tags.size() == 2
                                        && !meta.publishDate.isNull();
                        out << (ok ? "  [OK] 元数据字段解析正确\n"
                                   : "  [FAIL] 字段解析不完整\n");
                        allOk = allOk && ok;
                    } else {
                        // 归档内没有 ComicInfo.xml 属于正常情况（多数漫画不带元数据），
                        // 此时仅校验健壮性用例，不计入失败。
                        if (error.contains(QStringLiteral("未找到"))) {
                            out << "  [SKIP] 该归档不含 ComicInfo.xml，跳过字段校验\n";
                        } else {
                            out << "  [FAIL] 解析失败: " << error << "\n";
                            allOk = false;
                        }
                    }
                } else {
                    out << "  [FAIL] 打开归档失败: " << error << "\n";
                    allOk = false;
                }
            }

            // 2) 损坏 XML 不应崩溃
            ComicReader::ComicMetadata dummy;
            QString err2;
            const bool badOk = !ComicReader::ComicInfoParser::parse(
                QByteArray("<ComicInfo><Title>未闭合"), &dummy, &err2);
            out << (badOk ? "  [OK] 损坏 XML 被正确拒绝\n"
                          : "  [FAIL] 损坏 XML 未被拒绝\n");
            allOk = allOk && badOk;

            // 3) 空内容
            ComicReader::ComicMetadata dummy2;
            const bool emptyOk = !ComicReader::ComicInfoParser::parse(
                QByteArray("   "), &dummy2, nullptr);
            out << (emptyOk ? "  [OK] 空内容被正确拒绝\n" : "  [FAIL] 空内容未被拒绝\n");
            allOk = allOk && emptyOk;

            out << (allOk ? "METATEST PASSED" : "METATEST FAILED") << "\n";
            out.flush();
            return allOk ? 0 : 1;
        }
        if (qstrcmp(argv[i], "--addfolder") == 0 && i + 1 < argc) {
            // 扫描目录并导入到正式书架（便于脚本化批量导入）
            QCoreApplication app(argc, argv);
            QCoreApplication::setOrganizationName(QStringLiteral("ComicReader"));
            QCoreApplication::setOrganizationDomain(QStringLiteral("comicreader.local"));
            QCoreApplication::setApplicationName(QStringLiteral("ComicReader"));
            QTextStream out(stdout);
            ComicReader::LibraryDatabase db;
            QString error;
            if (!db.open(ComicReader::LibraryDatabase::defaultDatabasePath(), &error)) {
                out << "FAIL: 打开数据库失败: " << error << "\n";
                return 2;
            }
            const QString dir =
                QFileInfo(QString::fromLocal8Bit(argv[i + 1])).absoluteFilePath();
            int skipped = 0;
            const int added = db.importDirectory(dir, &skipped);
            out << "扫描 " << dir << ": 新增 " << added << " 本, 跳过 " << skipped
                << " 项, 书架现有 " << db.count() << " 本\n";
            out.flush();
            return 0;
        }
    }

    QGuiApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("ComicReader"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("comicreader.local"));
    QCoreApplication::setApplicationName(QStringLiteral("ComicReader"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // 多语言支持：根据系统语言加载对应翻译文件（MVP 阶段的静态支持，
    // 运行时动态切换语言将在第二阶段实现）。
    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &localeName : uiLanguages) {
        if (translator.load(QLocale(localeName),
                            QStringLiteral("app"),
                            QStringLiteral("_"),
                            QStringLiteral(":/i18n"))) {
            app.installTranslator(&translator);
            break;
        }
    }

    QQmlApplicationEngine engine;

    // 使用 Basic 样式，跨平台外观一致
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // 解析命令行：ComicReader.exe <漫画文件或文件夹>
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("ComicReader - cross-platform local comic reader"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("comic"),
        QStringLiteral("Comic archive (.zip/.cbz) or image folder to open."));
    parser.process(app);

    // 打开图书馆数据库（失败不影响阅读功能，仅失去进度记忆）
    ComicReader::LibraryDatabase library;
    QString dbError;
    if (!library.open(ComicReader::LibraryDatabase::defaultDatabasePath(), &dbError))
        qWarning() << "打开数据库失败，进度记忆不可用:" << dbError;

    // 创建阅读控制器并注入 QML 上下文
    ComicReader::ComicReaderController controller;
    controller.setDatabase(library.isOpen() ? &library : nullptr);

    // 书架模型与封面提供器（封面在渲染线程按需生成）
    ComicReader::LibraryModel libraryModel(library.isOpen() ? &library : nullptr);
    libraryModel.refresh();
    ComicReader::ComicCoverProvider *coverProvider =
        new ComicReader::ComicCoverProvider(&libraryModel);
    engine.rootContext()->setContextProperty(QStringLiteral("appLibrary"),
                                             &libraryModel);
    engine.rootContext()->setContextProperty(QStringLiteral("appCover"),
                                             coverProvider);
    engine.addImageProvider(QStringLiteral("cover"), coverProvider);

    // 阅读进度变化后刷新书架，使进度条与页码保持最新
    QObject::connect(&controller, &ComicReader::ComicReaderController::currentPageChanged,
                     &libraryModel, [&libraryModel] { libraryModel.refresh(); });

    // 退出前保存进度
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller,
                     [&controller] { controller.saveProgress(); });

    // 若命令行指定了漫画，加载后让 QML 初始直接进入阅读界面
    bool initialLoaded = false;
    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        const QString path = QFileInfo(positional.first()).absoluteFilePath();
        initialLoaded = controller.openComic(path);
    }

    engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("initialComicLoaded"),
                                             initialLoaded);

    // 注册页面图像提供者：image://comicpage/<index>
    engine.addImageProvider(QStringLiteral("comicpage"),
                             new ComicReader::ComicPageImageProvider(&controller));

    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
