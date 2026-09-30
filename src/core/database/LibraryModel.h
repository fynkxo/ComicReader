#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>

#include <memory>

namespace ComicReader {

class LibraryDatabase;

/// 书架模型：向 QML 暴露数据库中的漫画列表
///
/// 封面由 ComicCoverProvider 在渲染线程按需生成，本模型只负责
/// 维护一个"id -> 路径"的线程安全快照供其使用（渲染线程不得访问数据库）。
class LibraryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        SeriesRole,
        WriterRole,
        PageCountRole,
        CurrentPageRole,
        ProgressRole,   ///< 0.0 ~ 1.0
        PathRole,
        HasProgressRole,
    };

    explicit LibraryModel(LibraryDatabase *db, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// 从数据库重新加载
    Q_INVOKABLE void refresh();

    /// 扫描目录并导入漫画，返回新增数量（>0 表示有新书加入）
    Q_INVOKABLE int scanFolder(const QString &dirPath);

    /// 从书架移除一本书
    Q_INVOKABLE bool removeComic(int comicId);

    /// 漫画文件路径（渲染线程安全）
    QString pathForId(int comicId) const;

    /// 当前漫画 id
    int currentComicId() const { return m_currentId; }
    void setCurrentComicId(int id);

signals:
    void countChanged();
    void currentComicIdChanged();

private:
    struct Row {
        int id = 0;
        QString title;
        QString series;
        QString writer;
        int pageCount = 0;
        int currentPage = 0;
        QString path;
    };

    QList<Row> m_rows;
    LibraryDatabase *m_db = nullptr;   ///< 非拥有指针
    int m_currentId = -1;

    /// 供渲染线程读取的路径快照
    mutable QMutex m_pathMutex;
    QHash<int, QString> m_paths;

    friend class ComicCoverProvider;
};

/// 封面图像提供器：image://cover/<comicId>
///
/// 首次请求时打开归档读取第 1 页并缩放为缩略图，之后走内存缓存。
class ComicCoverProvider : public QQuickImageProvider
{
public:
    explicit ComicCoverProvider(LibraryModel *model);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    /// 漫画被移除/重载后清空缓存
    Q_INVOKABLE void clearCache();

private:
    LibraryModel *m_model;
    QMutex m_mutex;
    QHash<int, QImage> m_cache;
    qint64 m_cacheBytes = 0;
    static constexpr qint64 kMaxCacheBytes = 48ll * 1024 * 1024;
};

} // namespace ComicReader
