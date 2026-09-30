#pragma once

#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>

#include <memory>

namespace ComicReader {

class IComicArchive;

/// 漫画阅读控制器：向 QML 暴露页数、翻页与页图像
class ComicReaderController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int pageCount READ pageCount NOTIFY comicChanged)
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(QString comicName READ comicName NOTIFY comicChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit ComicReaderController(QObject *parent = nullptr);
    ~ComicReaderController() override;

    /// 打开漫画（zip/cbz 或图片文件夹），成功返回 true
    Q_INVOKABLE bool openComic(const QString &path);

    /// 关闭当前漫画
    Q_INVOKABLE void closeComic();

    Q_INVOKABLE void nextPage();
    Q_INVOKABLE void previousPage();
    Q_INVOKABLE void goToPage(int index);

    int pageCount() const;
    int currentPage() const { return m_currentPage; }
    QString comicName() const { return m_comicName; }
    QString statusMessage() const { return m_statusMessage; }

    /// 供 ImageProvider 读取指定页数据
    QByteArray pageImage(int index) const;

    void setCurrentPage(int index);

signals:
    void comicChanged();
    void currentPageChanged();
    void statusMessageChanged();

private:
    std::unique_ptr<IComicArchive> m_archive;
    int m_pageCount = 0;
    int m_currentPage = 0;
    QString m_comicName;
    QString m_statusMessage;
};

/// 向 QML 提供页面图像：image://comicpage/<index>
class ComicPageImageProvider : public QQuickImageProvider
{
public:
    explicit ComicPageImageProvider(ComicReaderController *controller);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    ComicReaderController *m_controller;
};

} // namespace ComicReader
