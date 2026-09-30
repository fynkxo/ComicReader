#include <QCommandLineParser>
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
