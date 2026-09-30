#include "LibraryModel.h"
#include "LibraryDatabase.h"
#include "../archive/IComicArchive.h"

#include <QBuffer>
#include <QImageReader>
#include <QList>

#include <memory>

namespace ComicReader {

LibraryModel::LibraryModel(LibraryDatabase *db, QObject *parent)
    : QAbstractListModel(parent)
    , m_db(db)
{
}

int LibraryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return int(m_rows.size());
}

QHash<int, QByteArray> LibraryModel::roleNames() const
{
    return {
        {IdRole, "comicId"},
        {TitleRole, "title"},
        {SeriesRole, "series"},
        {WriterRole, "writer"},
        {PageCountRole, "pageCount"},
        {CurrentPageRole, "currentPage"},
        {ProgressRole, "progress"},
        {PathRole, "path"},
        {HasProgressRole, "hasProgress"},
    };
}

QVariant LibraryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &row = m_rows.at(index.row());
    switch (role) {
    case IdRole:        return row.id;
    case TitleRole:     return row.title;
    case SeriesRole:    return row.series;
    case WriterRole:    return row.writer;
    case PageCountRole: return row.pageCount;
    case CurrentPageRole: return row.currentPage;
    case PathRole:      return row.path;
    case HasProgressRole: return row.currentPage > 0;
    case ProgressRole:
        if (row.pageCount > 1)
            return double(row.currentPage) / double(row.pageCount - 1);
        return (row.currentPage > 0) ? 1.0 : 0.0;
    default:
        return {};
    }
}

void LibraryModel::refresh()
{
    beginResetModel();
    m_rows.clear();

    // 渲染线程会并发读取 m_paths，整个重建过程必须在锁内完成
    QMutexLocker locker(&m_pathMutex);
    m_paths.clear();

    if (m_db && m_db->isOpen()) {
        const auto comics = m_db->allComics();
        m_rows.reserve(int(comics.size()));
        for (const ComicEntry &c : comics) {
            Row row;
            row.id = c.id;
            row.title = c.title.isEmpty() ? QStringLiteral("(未命名)") : c.title;
            row.series = c.series;
            row.writer = c.writer;
            row.pageCount = c.pageCount;
            row.currentPage = c.currentPage;
            row.path = c.path;
            m_rows.append(row);
            m_paths.insert(c.id, c.path);
        }
    }

    endResetModel();
    emit countChanged();
}

int LibraryModel::scanFolder(const QString &dirPath)
{
    if (!m_db || !m_db->isOpen() || dirPath.isEmpty())
        return 0;
    int skipped = 0;
    return m_db->importDirectory(dirPath, &skipped);
}

bool LibraryModel::removeComic(int comicId)
{
    if (!m_db || !m_db->isOpen() || comicId <= 0)
        return false;
    if (!m_db->removeComic(comicId))
        return false;
    refresh();
    return true;
}

QString LibraryModel::pathForId(int comicId) const
{
    QMutexLocker locker(&m_pathMutex);
    return m_paths.value(comicId);
}

void LibraryModel::setCurrentComicId(int id)
{
    if (m_currentId == id)
        return;
    m_currentId = id;
    emit currentComicIdChanged();
}

// ---------------------------------------------------------------------------
// ComicCoverProvider
// ---------------------------------------------------------------------------

ComicCoverProvider::ComicCoverProvider(LibraryModel *model)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_model(model)
{
}

void ComicCoverProvider::clearCache()
{
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
    m_cacheBytes = 0;
}

QImage ComicCoverProvider::requestImage(const QString &id, QSize *size,
                                         const QSize &requestedSize)
{
    bool ok = false;
    const int comicId = id.toInt(&ok);
    if (!ok || comicId <= 0 || !m_model)
        return {};

    // 1) 查缓存
    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_cache.constFind(comicId);
        if (it != m_cache.constEnd()) {
            const QImage cached = it.value();
            if (size)
                *size = cached.size();
            return cached;
        }
    }

    // 2) 从归档读取第 1 页并生成缩略图
    //    路径来自模型的线程安全快照，不直接访问数据库。
    const QString path = m_model->pathForId(comicId);
    if (path.isEmpty())
        return {};

    std::unique_ptr<IComicArchive> archive(createComicArchive(path));
    if (!archive)
        return {};
    QString error;
    if (!archive->open(path, &error))
        return {};

    const QByteArray raw = archive->pageData(0);
    if (raw.isEmpty())
        return {};

    QBuffer buffer;
    buffer.setData(raw);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
        return {};

    // 3) 缩放为缩略图（限制最长边），控制内存占用
    constexpr int kMaxEdge = 480;
    if (image.width() > kMaxEdge || image.height() > kMaxEdge)
        image = image.scaled(kMaxEdge, kMaxEdge, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);

    if (size)
        *size = image.size();

    // 4) 写入缓存，超出上限时整体清空（书架缩略图重建成本低，简单策略更优）
    {
        QMutexLocker locker(&m_mutex);
        const qint64 bytes = qint64(image.width()) * image.height() * 4;
        if (m_cacheBytes + bytes > kMaxCacheBytes) {
            m_cache.clear();
            m_cacheBytes = 0;
        }
        m_cache.insert(comicId, image);
        m_cacheBytes += bytes;
    }

    Q_UNUSED(requestedSize)
    return image;
}

} // namespace ComicReader
