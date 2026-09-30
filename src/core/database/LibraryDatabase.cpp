#include "LibraryDatabase.h"
#include "../archive/IComicArchive.h"
#include "../metadata/ComicInfoParser.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>
#include <cstdio>

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

    // ---- 增量迁移：为旧版本数据库补充元数据列 ----
    // SQLite 不支持 IF NOT EXISTS 列定义，需查询已有列后按需 ALTER TABLE
    const QStringList existing = q.exec(QStringLiteral("PRAGMA table_info(comics)"))
                                     ? [&q] {
                                           QStringList cols;
                                           while (q.next())
                                               cols << q.value(1).toString();
                                           return cols;
                                       }()
                                     : QStringList();

    const QVector<QPair<QString, QString>> metaColumns = {
        {QStringLiteral("series"), QStringLiteral("TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("summary"), QStringLiteral("TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("writer"), QStringLiteral("TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("publisher"), QStringLiteral("TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("language_iso"), QStringLiteral("TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("age_rating"), QStringLiteral("TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("tags"), QStringLiteral("TEXT NOT NULL DEFAULT ''")},
    };
    for (const auto &col : metaColumns) {
        if (!existing.contains(col.first)) {
            q.exec(QStringLiteral("ALTER TABLE comics ADD COLUMN %1 %2")
                       .arg(col.first, col.second));
        }
    }
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
    // 元数据列（旧库可能缺失，QSqlQuery::value 越界返回空 QVariant）
    entry.series = query.value(9).toString();
    entry.summary = query.value(10).toString();
    entry.writer = query.value(11).toString();
    entry.publisher = query.value(12).toString();
    entry.languageIso = query.value(13).toString();
    entry.ageRating = query.value(14).toString();
    entry.tags = query.value(15).toString();
    return entry;
}

namespace {

/// 目录扫描的最大递归深度（Series/Vol 等层级通常 2~3 层）
constexpr int kMaxScanDepth = 4;

/// 目录内是否直接包含图片（是则该目录本身即一本漫画）
bool dirHasImages(const QString &dirPath)
{
    const QFileInfoList files =
        QDir(dirPath).entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &f : files) {
        if (f.fileName().startsWith(QLatin1Char('.')))
            continue;
        if (isImageFile(f.suffix()))
            return true;
    }
    return false;
}

} // namespace

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
        dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);

    for (const QFileInfo &info : entries) {
        const QString name = info.fileName();
        if (name.startsWith(QLatin1Char('.')))   // 跳过隐藏项
            continue;

        if (info.isDir()) {
            if (dirHasImages(info.absoluteFilePath())) {
                // 该目录直接含图片 => 本身是一本已解压的漫画，不再深入
                if (importComic(info.absoluteFilePath()))
                    ++added;
                else
                    ++skipped;
            }
            // 不含图片的目录（如 Series 层）由递归处理
            continue;
        }

        if (isComicFile(info.suffix())) {
            if (importComic(info.absoluteFilePath()))
                ++added;
            else
                ++skipped;
        }
    }

    // 若扫描目标本身就是一本"已解压的图片文件夹"，也直接入库
    if (dirHasImages(dirPath)) {
        if (importComic(dirPath))
            ++added;
        else
            ++skipped;
    }

    // 递归深入：处理 Series/Vol 这类嵌套结构
    added += importSubtreeRecursive(dirPath, 1, &skipped);

    if (knownSkipped)
        *knownSkipped = skipped;
    return added;
}

int LibraryDatabase::importSubtreeRecursive(const QString &dirPath, int depth, int *skipped)
{
    if (depth > kMaxScanDepth)
        return 0;

    int added = 0;
    const QFileInfoList subdirs =
        QDir(dirPath).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);

    for (const QFileInfo &sub : subdirs) {
        if (sub.fileName().startsWith(QLatin1Char('.')))
            continue;   // 跳过隐藏目录

        const QString path = sub.absoluteFilePath();
        if (dirHasImages(path)) {
            // 该目录直接含图片 => 就是一本已解压的漫画
            if (importComic(path))
                ++added;
            else if (skipped)
                ++(*skipped);
            // 已是漫画，不再深入其子目录（避免把同一本拆成多本）
            continue;
        }
        // 只是分类目录（如 Series），继续向下
        added += importSubtreeRecursive(path, depth + 1, skipped);
    }

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
    // 目录名整体作为标题：漫画分卷目录常命名为 "Vol.01"，
    // completeBaseName() 会把 ".01" 当扩展名剥掉，导致标题丢失分卷号。
    q.addBindValue(info.isDir() ? info.fileName() : info.completeBaseName());
    q.addBindValue(info.size());
    q.addBindValue(pages);
    q.addBindValue(info.lastModified().toSecsSinceEpoch());

    if (!q.exec()) {
        qCWarning(lcDb) << "导入失败:" << abs << q.lastError().text();
        return false;
    }
    if (q.numRowsAffected() <= 0)
        return false;

    // 解析 ComicInfo.xml（若存在）并写入元数据
    ComicMetadata meta;
    if (ComicInfoParser::parseFromArchive(archive.get(), &meta)) {
        const int id = q.lastInsertId().toInt();
        if (id > 0 && !applyMetadata(id, meta))
            qCWarning(lcDb) << "元数据写入失败:" << abs;
    }
    return true;
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
        "       file_modified, added_at, last_read_at,"
        "       series, summary, writer, publisher, language_iso,"
        "       age_rating, tags"
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
        "       file_modified, added_at, last_read_at,"
        "       series, summary, writer, publisher, language_iso,"
        "       age_rating, tags"
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

bool LibraryDatabase::applyMetadata(int comicId, const ComicMetadata &meta)
{
    if (!m_open || comicId <= 0)
        return false;

    QSqlQuery q(m_db);
    // COALESCE 语义：元数据为空字符串时不覆盖已有值
    q.prepare(QStringLiteral(
        "UPDATE comics SET"
        "  title       = CASE WHEN ? <> '' THEN ? ELSE title END,"
        "  series      = CASE WHEN ? <> '' THEN ? ELSE series END,"
        "  summary     = CASE WHEN ? <> '' THEN ? ELSE summary END,"
        "  writer      = CASE WHEN ? <> '' THEN ? ELSE writer END,"
        "  publisher   = CASE WHEN ? <> '' THEN ? ELSE publisher END,"
        "  language_iso= CASE WHEN ? <> '' THEN ? ELSE language_iso END,"
        "  age_rating  = CASE WHEN ? <> '' THEN ? ELSE age_rating END,"
        "  tags        = CASE WHEN ? <> '' THEN ? ELSE tags END"
        " WHERE id = ?"));
    const QString title = meta.title;
    const QString series = meta.series;
    const QString summary = meta.summary;
    const QString writer = meta.writer;
    const QString publisher = meta.publisher;
    const QString language = meta.languageIso;
    const QString rating = meta.ageRating;
    const QString tags = meta.tags.join(QStringLiteral(", "));

    for (int i = 0; i < 8; ++i) {
        QString v;
        switch (i) {
        case 0: v = title; break;
        case 1: v = series; break;
        case 2: v = summary; break;
        case 3: v = writer; break;
        case 4: v = publisher; break;
        case 5: v = language; break;
        case 6: v = rating; break;
        default: v = tags; break;
        }
        q.addBindValue(v);
        q.addBindValue(v);
    }
    q.addBindValue(comicId);

    if (!q.exec()) {
        qCWarning(lcDb) << "写入元数据失败:" << q.lastError().text();
        return false;
    }
    return true;
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
