#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTranslator>
#include <QLocale>
#include <QUrl>

int main(int argc, char *argv[])
{
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
