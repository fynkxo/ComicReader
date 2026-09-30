#include "ComicReaderController.h"
#include "IComicArchive.h"
#include "PageCache.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QTimer>

namespace ComicReader {

namespace {

/// 预加载范围：当前页之后 3 页、之前 1 页
constexpr int kPreloadAhead = 3;
constexpr int kPreloadBehind = 1;
/// 默认页面缓存上限 64 MB
constexpr qint64 kDefaultCacheBytes = 64ll * 1024 * 1024;
/// 判断是否适合双页时最多探测的页数
constexpr int kProbeLimit = 12;
/// 宽高比大于该值视为"偏宽"页面（常见跨页/双页扫描稿）
constexpr double kWideAspectRatio = 0.95;

} // namespace

ComicReaderController::ComicReaderController(QObject *parent)
    : QObject(parent)
    , m_cache(kDefaultCacheBytes)
{
}

ComicReaderController::~ComicReaderController()
{
    // 等待在途预加载结束，避免归档对象被后台线程访问时析构
    QThreadPool::globalInstance()->waitForDone(3000);
}

bool ComicReaderController::openComic(const QString &path)
{
    closeComic();

    if (path.isEmpty()) {
        m_statusMessage = tr("No comic path provided");
        emit statusMessageChanged();
        return false;
    }

    std::unique_ptr<IComicArchive> archive(createComicArchive(path));
    if (!archive) {
        m_statusMessage = tr("File not found: %1").arg(path);
        emit statusMessageChanged();
        return false;
    }

    QString error;
    if (!archive->open(path, &error)) {
        m_statusMessage = error.isEmpty() ? tr("Failed to open comic") : error;
        emit statusMessageChanged();
        return false;
    }

    m_archive = std::move(archive);
    m_pageCount = m_archive->pages().size();
    m_currentPage = 0;
    m_comicName = QFileInfo(path).fileName();
    m_statusMessage = tr("Loaded %n page(s)", nullptr, m_pageCount);

    m_cache.clear();
    m_preloadScheduled.clear();
    m_pageSizes.clear();
    ++m_generation;   // 使之前在途的预加载结果作废

    emit comicChanged();
    emit currentPageChanged();
    emit statusMessageChanged();
    return true;
}

void ComicReaderController::closeComic()
{
    if (!m_archive)
        return;
    // 先递增代次并清空标记，使在途预加载结果不再写入缓存
    ++m_generation;
    m_preloadScheduled.clear();
    m_cache.clear();
    m_pageSizes.clear();
    m_archive.reset();
    m_pageCount = 0;
    m_currentPage = 0;
    m_comicName.clear();
    emit comicChanged();
    emit currentPageChanged();
}

int ComicReaderController::pageCount() const
{
    return m_pageCount;
}

void ComicReaderController::setCurrentPage(int index)
{
    if (m_pageCount <= 0)
        return;
    const int clamped = qBound(0, index, m_pageCount - 1);
    if (clamped == m_currentPage)
        return;
    m_currentPage = clamped;
    emit currentPageChanged();
    schedulePreload();
}

void ComicReaderController::nextPage()
{
    setCurrentPage(m_currentPage + 1);
}

void ComicReaderController::previousPage()
{
    setCurrentPage(m_currentPage - 1);
}

void ComicReaderController::goToPage(int index)
{
    setCurrentPage(index);
}

QByteArray ComicReaderController::pageImage(int index) const
{
    if (!m_archive)
        return {};

    // 1) 优先命中缓存（预加载已就绪时为纯内存拷贝，无需解压）
    const QByteArray cached = m_cache.take(index);
    if (!cached.isEmpty())
        return cached;

    // 2) 未命中：同步读取（首次打开当前页时的兜底路径）
    return m_archive->pageData(index);
}

void ComicReaderController::schedulePreload()
{
    if (!m_archive || m_pageCount <= 0)
        return;

    // 预加载在后台线程执行，延迟到事件循环空闲时再提交，避免与翻页争用
    QTimer::singleShot(0, this, [this] {
        if (!m_archive)
            return;

        const int center = m_currentPage;
        for (int offset = 1; offset <= kPreloadAhead; ++offset) {
            const int target = center + offset;
            if (target < m_pageCount)
                schedulePreloadOne(target);
        }
        for (int offset = 1; offset <= kPreloadBehind; ++offset) {
            const int target = center - offset;
            if (target >= 0)
                schedulePreloadOne(target);
        }
    });
}

void ComicReaderController::schedulePreloadOne(int index)
{
    if (index < 0 || index >= m_pageCount)
        return;
    if (m_cache.contains(index))
        return;
    if (m_preloadScheduled.contains(index))
        return;

    m_preloadScheduled.insert(index);
    const quint64 generation = m_generation;

    // 捕获 controller 指针；若期间关闭漫画，析构函数会等待任务结束，
    // 因此在 run() 中访问成员是安全的。
    QThreadPool::globalInstance()->start(
        [this, index, generation] { preloadPageInternal(index, generation); });
}

void ComicReaderController::preloadPage(int index)
{
    if (!m_archive)
        return;
    preloadPageInternal(index, m_generation);
}

void ComicReaderController::preloadPageInternal(int index, quint64 generation)
{
    if (!m_archive)
        return;

    // 漫画已被重新打开/关闭：结果作废
    if (generation != m_generation)
        return;

    const QByteArray data = m_archive->pageData(index);
    if (data.isEmpty())
        return;

    // 超过缓存容量的超大页不入缓存（避免反复解压仍会命中不了）
    if (m_cache.wouldFit(data.size()))
        m_cache.insert(index, data);
}

QSize ComicReaderController::pageSourceSize(int index) const
{
    if (!m_archive || index < 0 || index >= m_pageCount)
        return {};

    // 尺寸探测代价高（需解码图片头），缓存结果
    const auto it = m_pageSizes.constFind(index);
    if (it != m_pageSizes.constEnd())
        return it.value();

    const QByteArray data = m_archive->pageData(index);
    if (data.isEmpty())
        return {};

    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QSize size = reader.size();   // 仅读图片头，不解码像素

    if (size.isValid())
        m_pageSizes.insert(index, size);
    return size;
}

bool ComicReaderController::prefersDoublePage() const
{
    if (!m_archive || m_pageCount <= 0)
        return false;

    // 至少两页才谈得上双页
    if (m_pageCount < 2)
        return false;

    int checked = 0;
    int wide = 0;
    const int limit = qMin(m_pageCount, kProbeLimit);
    for (int i = 0; i < limit; ++i) {
        const QSize size = pageSourceSize(i);
        if (!size.isValid() || size.height() <= 0)
            continue;
        ++checked;
        if (double(size.width()) / double(size.height()) >= kWideAspectRatio)
            ++wide;
    }

    if (checked == 0)
        return false;
    // 绝大多数页面偏宽才判定为双页漫画
    return wide * 2 >= checked;
}

// ---------------------------------------------------------------------------
// ComicPageImageProvider
// ---------------------------------------------------------------------------

ComicPageImageProvider::ComicPageImageProvider(ComicReaderController *controller)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_controller(controller)
{
}

QImage ComicPageImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    if (size)
        *size = QSize();

    bool ok = false;
    const int index = id.toInt(&ok);
    if (!ok || index < 0)
        return {};

    const QByteArray raw = m_controller->pageImage(index);
    if (raw.isEmpty())
        return {};

    // 从内存缓冲区解码，避免落地临时文件
    QBuffer buffer;
    buffer.setData(raw);
    buffer.open(QIODevice::ReadOnly);

    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
        return {};

    if (size)
        *size = image.size();

    // 请求了缩放尺寸时按需缩放（保持宽高比）
    if (!requestedSize.isEmpty() && requestedSize.width() > 0) {
        const QSize scaled = image.size().scaled(requestedSize, Qt::KeepAspectRatio);
        return image.scaled(scaled, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return image;
}

} // namespace ComicReader
