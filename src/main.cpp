#include <QCommandLineParser>
#include <QElapsedTimer>
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

    // 创建阅读控制器并注入 QML 上下文
    ComicReader::ComicReaderController controller;

    // 若命令行指定了漫画，加载后让 QML 初始直接进入阅读界面
    bool initialLoaded = false;
    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        const QString path = QFileInfo(positional.first()).absoluteFilePath();
        initialLoaded = controller.openComic(path);
    }

    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
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
