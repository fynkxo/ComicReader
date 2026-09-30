#include "LibraryDatabase.h"
#include "../archive/IComicArchive.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

#include <memory>

Q_LOGGING_CATEGORY(lcDb, "comicreader.db")

namespace ComicReader {

namespace {

/// 漫画文件扩展名
bool isComicFile(const QString &suffix)
{
    const QString s = suffix.toLower();
    return s == QStringLiteral("zip") || s == QStringLiteral("cbz");
}

} // namespace

LibraryDatabase::LibraryDatabase() = default;

LibraryDatabase::~LibraryDatabase()
{
    close();
}

QString LibraryDatabase::defaultDatabasePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath(QStringLiteral("comicreader.db"));
}

bool LibraryDatabase::open(const QString &dbPath, QString *error)
{
    close();

    const QString dir = QFileInfo(dbPath).absolutePath();
    if (!QDir().mkpath(dir)) {
        if (error)
            *error = QStringLiteral("无法创建数据目录: %1").arg(dir);
        return false;
    }

    // 每个实例用唯一连接名，避免与其他实例冲突
    const QString connName = QStringLiteral("comicreader_%1")
                                 .arg(reinterpret_cast<quintptr>(this), 0, 16);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        if (error)
            *error = QStringLiteral("打开数据库失败: %1").arg(m_db.lastError().text());
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connName);
        return false;
    }

    // 启用外键约束，保证书签随漫画级联删除
    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

    if (!ensureSchema(error)) {
        close();
        return false;
    }

    m_open = true;
    return true;
}

void LibraryDatabase::close()
{
    if (!m_open)
        return;
    const QString name = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
    m_open = false;
}

bool LibraryDatabase::isOpen() const
{
    return m_open;
}

bool LibraryDatabase::ensureSchema(QString *error)
{
    QSqlQuery q(m_db);

    // comics：书架主表
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS comics ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  path TEXT NOT NULL UNIQUE,"
            "  title TEXT NOT NULL DEFAULT '',"
            "  size_bytes INTEGER NOT NULL DEFAULT 0,"
            "  page_count INTEGER NOT NULL DEFAULT 0,"
            "  current_page INTEGER NOT NULL DEFAULT 0,"
            "  file_modified INTEGER NOT NULL DEFAULT 0,"
            "  added_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),"
            "  last_read_at TEXT"
            ")"))) {
        if (error)
            *error = QStringLiteral("创建 comics 表失败: %1").arg(q.lastError().text());
        return false;
    }

    // bookmarks：书签表，外键级联删除
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS bookmarks ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  comic_id INTEGER NOT NULL,"
            "  page INTEGER NOT NULL DEFAULT 0,"
            "  note TEXT NOT NULL DEFAULT '',"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),"
            "  FOREIGN KEY(comic_id) REFERENCES comics(id) ON DELETE CASCADE"
            ")"))) {
        if (error)
            *error = QStringLiteral("创建 bookmarks 表失败: %1").arg(q.lastError().text());
        return false;
    }

    // 索引：加速按修改时间排序与按漫画查询书签
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_comics_last_read "
                          "ON comics(last_read_at)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_bookmarks_comic "
                          "ON bookmarks(comic_id)"));
    return true;
}

ComicEntry LibraryDatabase::entryFromQuery(QSqlQuery &query)
{
    ComicEntry entry;
    entry.id = query.value(0).toInt();
    entry.path = query.value(1).toString();
    entry.title = query.value(2).toString();
    entry.sizeBytes = query.value(3).toLongLong();
    entry.pageCount = query.value(4).toInt();
    entry.currentPage = query.value(5).toInt();
    entry.fileModified = query.value(6).toLongLong();
    // SQLite 以文本存储时间字符串，需显式转换
    entry.addedAt = QDateTime::fromString(query.value(7).toString(),
                                         QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    entry.lastReadAt = QDateTime::fromString(query.value(8).toString(),
                                             QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    return entry;
}

int LibraryDatabase::importDirectory(const QString &dirPath, int *knownSkipped)
{
    int added = 0;
    int skipped = 0;
    if (!m_open)
        return 0;

    QDir dir(dirPath);
    if (!dir.exists())
        return 0;

    const QFileInfoList entries =
        dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo &info : entries) {
        const QString path = info.absoluteFilePath();
        if (info.isDir()) {
            // 仅当子目录直接含图片时才视为一本漫画
            bool hasImage = false;
            const QFileInfoList inner =
                QDir(path).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            for (const QFileInfo &f : inner) {
                if (isImageFile(f.suffix())) {
                    hasImage = true;
                    break;
                }
            }
            if (!hasImage)
                continue;
        } else if (!isComicFile(info.suffix())) {
            continue;
        }

        if (importComic(path))
            ++added;
        else
            ++skipped;
    }

    if (knownSkipped)
        *knownSkipped = skipped;
    return added;
}

bool LibraryDatabase::importComic(const QString &path)
{
    if (!m_open)
        return false;

    const QFileInfo info(path);
    if (!info.exists())
        return false;
    if (!info.isDir() && !isComicFile(info.suffix()))
        return false;

    const QString abs = info.absoluteFilePath();

    // 已存在则不重复导入
    if (comicByPath(abs).id > 0)
        return false;

    // 读取页数需要真正打开归档，失败则不导入
    std::unique_ptr<IComicArchive> archive(createComicArchive(abs));
    if (!archive)
        return false;

    QString error;
    if (!archive->open(abs, &error)) {
        qCWarning(lcDb) << "跳过无法打开的漫画:" << abs << error;
        return false;
    }
    const int pages = archive->pages().size();

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO comics"
        " (path, title, size_bytes, page_count, current_page, file_modified)"
        " VALUES (?, ?, ?, ?, 0, ?)"));
    q.addBindValue(abs);
    q.addBindValue(info.completeBaseName());
    q.addBindValue(info.size());
    q.addBindValue(pages);
    q.addBindValue(info.lastModified().toSecsSinceEpoch());

    if (!q.exec()) {
        qCWarning(lcDb) << "导入失败:" << abs << q.lastError().text();
        return false;
    }
    return q.numRowsAffected() > 0;
}

QVector<ComicEntry> LibraryDatabase::allComics() const
{
    QVector<ComicEntry> result;
    if (!m_open)
        return result;

    // 未读过的排在最后，其余按最近阅读时间倒序
    QSqlQuery q(m_db);
    q.exec(QStringLiteral(
        "SELECT id, path, title, size_bytes, page_count, current_page,"
        "       file_modified, added_at, last_read_at"
        " FROM comics"
        " ORDER BY (last_read_at IS NULL), last_read_at DESC, title COLLATE NOCASE"));

    while (q.next())
        result.append(entryFromQuery(q));
    return result;
}

ComicEntry LibraryDatabase::comicByPath(const QString &path) const
{
    ComicEntry entry;
    if (!m_open)
        return entry;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, path, title, size_bytes, page_count, current_page,"
        "       file_modified, added_at, last_read_at"
        " FROM comics WHERE path = ?"));
    q.addBindValue(QFileInfo(path).absoluteFilePath());
    if (q.exec() && q.next())
        entry = entryFromQuery(q);
    return entry;
}

bool LibraryDatabase::updateProgress(int comicId, int page)
{
    if (!m_open || comicId <= 0)
        return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE comics"
        " SET current_page = ?, last_read_at = datetime('now','localtime')"
        " WHERE id = ?"));
    q.addBindValue(qMax(0, page));
    q.addBindValue(comicId);
    return q.exec() && q.numRowsAffected() > 0;
}

bool LibraryDatabase::removeComic(int comicId)
{
    if (!m_open || comicId <= 0)
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM comics WHERE id = ?"));
    q.addBindValue(comicId);
    return q.exec() && q.numRowsAffected() > 0;
}

int LibraryDatabase::count() const
{
    if (!m_open)
        return 0;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM comics")) && q.next())
        return q.value(0).toInt();
    return 0;
}

QVector<Bookmark> LibraryDatabase::bookmarksFor(int comicId) const
{
    QVector<Bookmark> result;
    if (!m_open)
        return result;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, comic_id, page, note, created_at"
        " FROM bookmarks WHERE comic_id = ? ORDER BY page"));
    q.addBindValue(comicId);
    if (!q.exec())
        return result;

    while (q.next()) {
        Bookmark b;
        b.id = q.value(0).toInt();
        b.comicId = q.value(1).toInt();
        b.page = q.value(2).toInt();
        b.note = q.value(3).toString();
        b.createdAt = QDateTime::fromString(q.value(4).toString(),
                                            QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        result.append(b);
    }
    return result;
}

bool LibraryDatabase::addBookmark(int comicId, int page, const QString &note)
{
    if (!m_open || comicId <= 0)
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO bookmarks (comic_id, page, note) VALUES (?, ?, ?)"));
    q.addBindValue(comicId);
    q.addBindValue(qMax(0, page));
    q.addBindValue(note);
    return q.exec();
}

bool LibraryDatabase::removeBookmark(int bookmarkId)
{
    if (!m_open || bookmarkId <= 0)
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM bookmarks WHERE id = ?"));
    q.addBindValue(bookmarkId);
    return q.exec() && q.numRowsAffected() > 0;
}

} // namespace ComicReader
