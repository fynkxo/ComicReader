#pragma once

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

namespace ComicReader {

/// 图书馆中的一本漫画
struct ComicEntry {
    int id = -1;              ///< 数据库主键，-1 表示未持久化
    QString path;             ///< 归档文件或文件夹路径（唯一）
    QString title;            ///< 展示标题，默认为文件名
    qint64 sizeBytes = 0;     ///< 文件大小
    int pageCount = 0;        ///< 页数
    int currentPage = 0;      ///< 上次阅读到的页
    qint64 fileModified = 0;  ///< 文件 mtime（秒），用于检测外部改动
    QDateTime addedAt;        ///< 加入书架时间
    QDateTime lastReadAt;     ///< 最近阅读时间
    // ---- 来自 ComicInfo.xml 的元数据（可为空）----
    QString series;           ///< 系列
    QString summary;          ///< 简介
    QString writer;           ///< 作者
    QString publisher;        ///< 出版社
    QString languageIso;      ///< 语言
    QString ageRating;        ///< 年龄分级
    QString tags;             ///< 标签（以 ", " 连接）
};

/// 书签
struct Bookmark {
    int id = -1;
    int comicId = 0;
    int page = 0;
    QString note;
    QDateTime createdAt;
};

/// SQLite 数据库：书架、阅读进度与书签
///
/// 采用应用数据目录下的 comicreader.db。所有写操作都在 GUI 线程执行
/// （写入量小，SQLite 单文件开销可接受），读取走内存缓存。
class LibraryDatabase
{
public:
    LibraryDatabase();
    ~LibraryDatabase();

    /// 打开数据库，失败返回 false 并写入 error
    bool open(const QString &dbPath, QString *error = nullptr);
    void close();
    bool isOpen() const;

    /// 默认数据库路径（QStandardPaths::AppDataLocation/comicreader.db）
    static QString defaultDatabasePath();

    // ---- 书架 ----

    /// 扫描目录，导入其中的漫画（.zip/.cbz/图片文件夹），返回新增数量
    /// alreadyKnown 中的路径会被跳过
    int importDirectory(const QString &dirPath, int *knownSkipped = nullptr);

    /// 导入单个漫画文件
    bool importComic(const QString &path);

    /// 用 ComicInfo.xml 元数据更新书籍信息
    bool applyMetadata(int comicId, const struct ComicMetadata &meta);

    /// 书籍总数
    int count() const;

    /// 全部书籍，按最近阅读时间倒序
    QVector<ComicEntry> allComics() const;

    /// 按路径查找
    ComicEntry comicByPath(const QString &path) const;

    /// 更新阅读进度；同时更新 last_read_at
    bool updateProgress(int comicId, int page);

    /// 从图书馆移除
    bool removeComic(int comicId);

    // ---- 书签 ----

    QVector<Bookmark> bookmarksFor(int comicId) const;
    bool addBookmark(int comicId, int page, const QString &note = QString());
    bool removeBookmark(int bookmarkId);

private:
    bool ensureSchema(QString *error);
    static ComicEntry entryFromQuery(class QSqlQuery &query);

    QSqlDatabase m_db;   ///< 值类型：QSqlDatabase 自身管理连接生命周期
    bool m_open = false;
};

} // namespace ComicReader
