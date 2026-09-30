#include "ComicReaderController.h"
#include "IComicArchive.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QString>

namespace ComicReader {

ComicReaderController::ComicReaderController(QObject *parent)
    : QObject(parent)
{
}

ComicReaderController::~ComicReaderController() = default;

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

    emit comicChanged();
    emit currentPageChanged();
    emit statusMessageChanged();
    return true;
}

void ComicReaderController::closeComic()
{
    if (!m_archive)
        return;
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
    const QByteArray raw = m_archive->pageData(index);
    if (raw.isEmpty())
        return {};
    return raw;
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
