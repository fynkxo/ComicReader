#pragma once

#include "PageCache.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QSet>
#include <QSize>
#include <QString>

#include <memory>

namespace ComicReader {

class IComicArchive;
class LibraryDatabase;

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

    /// 把当前进度写入数据库（下次打开可继续阅读）
    Q_INVOKABLE void saveProgress();

    /// 当前漫画在数据库中的 id，-1 表示未入库
    Q_INVOKABLE int comicId() const { return m_comicId; }

    /// 打开漫画时若数据库中已有进度，自动跳转
    Q_INVOKABLE int savedPageFor(const QString &path) const;

    /// 注入数据库（不持有所有权；为 nullptr 时功能降级但不崩溃）
    void setDatabase(LibraryDatabase *db) { m_db = db; }

    /// 页面原始尺寸（像素）；index 越界返回无效尺寸。
    /// 用于阅读器计算适应缩放与判断是否适合双页显示。
    Q_INVOKABLE QSize pageSourceSize(int index) const;

    /// 启发式判断：全部页面是否偏宽（宽高比 > 阈值），适合双页并排。
    /// 扫描至多 kProbeLimit 页以保证打开速度。
    Q_INVOKABLE bool prefersDoublePage() const;

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
    /// 请求后台预加载当前页附近的页面
    void schedulePreload();

    /// 为单页提交预加载任务（去重后交给线程池）
    void schedulePreloadOne(int index);

    /// 在线程池中执行的实际预加载工作
    void preloadPageInternal(int index, quint64 generation);

    /// 预加载单个页面（同步，供测试/预热使用）
    void preloadPage(int index);

    std::unique_ptr<IComicArchive> m_archive;
    /// 页面缓存：pageImage() 为 const，但仍需写入缓存（记忆化副作用）
    mutable PageCache m_cache;
    int m_pageCount = 0;
    int m_currentPage = 0;
    QString m_comicName;
    QString m_statusMessage;

    /// 标记漫画已重新打开，使旧的在途预加载结果作废
    quint64 m_generation = 0;
    /// 已提交预加载的页索引，避免重复提交
    QSet<int> m_preloadScheduled;
    /// 页面原始尺寸缓存（解码图片头成本较高）
    mutable QHash<int, QSize> m_pageSizes;
    /// 当前漫画在数据库中的 id，-1 表示未入库
    int m_comicId = -1;
    /// 当前漫画的绝对路径（用于关联数据库记录）
    QString m_comicPath;
    /// 数据库（非拥有指针，可为 nullptr）
    LibraryDatabase *m_db = nullptr;
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
