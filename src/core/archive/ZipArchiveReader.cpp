#include "IComicArchive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <zlib.h>

namespace ComicReader {

namespace {

quint16 readU16(const QByteArray &d, int off)
{
    return static_cast<quint16>(static_cast<quint8>(d.at(off)))
           | (static_cast<quint16>(static_cast<quint8>(d.at(off + 1))) << 8);
}

quint32 readU32(const QByteArray &d, int off)
{
    return static_cast<quint32>(static_cast<quint8>(d.at(off)))
           | (static_cast<quint32>(static_cast<quint8>(d.at(off + 1))) << 8)
           | (static_cast<quint32>(static_cast<quint8>(d.at(off + 2))) << 16)
           | (static_cast<quint32>(static_cast<quint8>(d.at(off + 3))) << 24);
}

constexpr int kEocdFixedSize = 22;
constexpr quint32 kEocdSignature = 0x06054b50;
constexpr quint32 kCentralSignature = 0x02014b50;
constexpr quint32 kLocalSignature = 0x04034b50;

} // namespace

bool ZipArchiveReader::open(const QString &path, QString *error)
{
    m_entries.clear();
    m_pageIndexes.clear();

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法打开文件: %1").arg(m_file.errorString());
        return false;
    }

    const qint64 fileSize = m_file.size();
    if (fileSize < kEocdFixedSize) {
        if (error)
            *error = QStringLiteral("文件过小，不是有效的 ZIP 归档");
        m_file.close();
        return false;
    }

    // 从尾部向前搜索 EOCD（注释长度可变，最多 65535）
    const int tailSize = static_cast<int>(qMin<qint64>(fileSize, kEocdFixedSize + 0xffff));
    m_file.seek(fileSize - tailSize);
    const QByteArray tail = m_file.read(tailSize);

    int eocdPos = -1;
    for (int i = tail.size() - kEocdFixedSize; i >= 0; --i) {
        if (readU32(tail, i) == kEocdSignature) {
            eocdPos = i;
            break;
        }
    }
    if (eocdPos < 0) {
        if (error)
            *error = QStringLiteral("未找到 ZIP 中央目录结尾记录（文件可能已损坏）");
        m_file.close();
        return false;
    }

    const quint16 entryCount = readU16(tail, eocdPos + 10);
    const quint32 cdSize = readU32(tail, eocdPos + 12);
    const quint32 cdOffset = readU32(tail, eocdPos + 16);

    if (!m_file.seek(cdOffset)) {
        if (error)
            *error = QStringLiteral("无法定位 ZIP 中央目录");
        m_file.close();
        return false;
    }
    const QByteArray central = m_file.read(cdSize);

    int pos = 0;
    for (int i = 0; i < entryCount && pos + 46 <= central.size(); ++i) {
        if (readU32(central, pos) != kCentralSignature)
            break;

        CentralEntry entry;
        const quint16 flags = readU16(central, pos + 8);
        entry.compressionMethod = readU16(central, pos + 10);
        entry.compressedSize = readU32(central, pos + 20);
        entry.uncompressedSize = readU32(central, pos + 24);
        entry.localHeaderOffset = readU32(central, pos + 42);

        const quint16 nameLen = readU16(central, pos + 28);
        const quint16 extraLen = readU16(central, pos + 30);
        const quint16 commentLen = readU16(central, pos + 32);

        const QByteArray rawName = central.mid(pos + 46, nameLen);
        // UTF-8 标志位（bit 11）未置位时按本地编码解读
        entry.name = (flags & 0x0800) ? QString::fromUtf8(rawName)
                                      : QString::fromLocal8Bit(rawName);
        entry.isDirectory = entry.name.endsWith(QLatin1Char('/'));

        m_entries.append(entry);
        pos += 46 + nameLen + extraLen + commentLen;
    }

    // 收集漫画页：排除目录与隐藏文件
    m_pageIndexes.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        const CentralEntry &e = m_entries.at(i);
        if (e.isDirectory || e.uncompressedSize == 0)
            continue;
        const QString base = QFileInfo(e.name).fileName();
        if (base.startsWith(QLatin1Char('.')))
            continue;
        if (!isImageFile(QFileInfo(base).suffix()))
            continue;
        m_pageIndexes.append(i);
    }

    if (m_pageIndexes.isEmpty()) {
        if (error)
            *error = QStringLiteral("归档中未找到任何图片");
        m_file.close();
        return false;
    }

    // 页面按文件名自然排序（page2 < page10）
    std::sort(m_pageIndexes.begin(), m_pageIndexes.end(), [this](int a, int b) {
        return naturalLessThan(m_entries.at(a).name, m_entries.at(b).name);
    });

    return true;
}

QList<PageEntry> ZipArchiveReader::pages() const
{
    QList<PageEntry> result;
    result.reserve(m_pageIndexes.size());
    for (int idx : m_pageIndexes)
        result.append({QFileInfo(m_entries.at(idx).name).fileName(),
                       m_entries.at(idx).uncompressedSize});
    return result;
}

QByteArray ZipArchiveReader::pageData(int index) const
{
    if (index < 0 || index >= m_pageIndexes.size())
        return {};
    return fileData(m_entries.at(m_pageIndexes.at(index)).name);
}

QByteArray ZipArchiveReader::fileData(const QString &name) const
{
    // 定位中央目录中的同名条目（大小写不敏感，兼容部分工具写入的大小写差异）
    int found = -1;
    const QString target = name.toLower();
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).name.toLower() == target) {
            found = i;
            break;
        }
    }
    if (found < 0)
        return {};

    // m_file 非线程安全：预加载线程与 GUI 线程可能并发读取
    QMutexLocker locker(&m_mutex);
    if (!m_file.isOpen())
        return {};

    const CentralEntry &entry = m_entries.at(found);

    // 定位本地文件头，跳过其中的变长字段
    if (!m_file.seek(entry.localHeaderOffset))
        return {};
    const QByteArray localHeader = m_file.read(30);
    if (localHeader.size() < 30 || readU32(localHeader, 0) != kLocalSignature)
        return {};

    const quint16 nameLen = readU16(localHeader, 26);
    const quint16 extraLen = readU16(localHeader, 28);
    if (!m_file.seek(entry.localHeaderOffset + 30 + nameLen + extraLen))
        return {};

    const QByteArray compressed = m_file.read(entry.compressedSize);
    if (compressed.size() != entry.compressedSize)
        return {};

    // method 0 = 已存储；method 8 = deflate；其余不支持
    if (entry.compressionMethod == 0)
        return compressed;
    if (entry.compressionMethod != 8)
        return {};
    if (entry.uncompressedSize <= 0 || entry.uncompressedSize > 512ll * 1024 * 1024)
        return {};   // 防御异常/超大尺寸

    QByteArray out;
    out.resize(static_cast<int>(entry.uncompressedSize));

    z_stream stream{};
    // 负窗口位表示原始 deflate 数据流（ZIP 不含 zlib 头）
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return {};

    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());

    const int ret = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    if (ret != Z_STREAM_END)
        return {};
    return out;
}

// ---------------------------------------------------------------------------
// FolderArchiveReader
// ---------------------------------------------------------------------------

bool FolderArchiveReader::open(const QString &path, QString *error)
{
    m_dirPath = path;
    m_pages.clear();

    QDir dir(path);
    if (!dir.exists()) {
        if (error)
            *error = QStringLiteral("文件夹不存在: %1").arg(path);
        return false;
    }

    const QStringList files = dir.entryList(QDir::Files | QDir::Readable, QDir::Name);
    for (const QString &file : files) {
        const QFileInfo info(dir.filePath(file));
        if (!isImageFile(info.suffix()))
            continue;
        m_pages.append({file, info.size()});
    }

    if (m_pages.isEmpty()) {
        if (error)
            *error = QStringLiteral("文件夹中没有受支持的图片");
        return false;
    }

    std::sort(m_pages.begin(), m_pages.end(),
              [](const PageEntry &a, const PageEntry &b) {
                  return naturalLessThan(a.name, b.name);
              });

    return true;
}

QList<PageEntry> FolderArchiveReader::pages() const
{
    return m_pages;
}

QByteArray FolderArchiveReader::pageData(int index) const
{
    if (index < 0 || index >= m_pages.size())
        return {};

    // 每次使用独立的 QFile 实例，天然线程安全，无需加锁
    QFile file(QDir(m_dirPath).filePath(m_pages.at(index).name));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QByteArray FolderArchiveReader::fileData(const QString &name) const
{
    if (m_dirPath.isEmpty())
        return {};

    // 只取文件名，天然剥离目录部分，避免路径穿越
    const QString base = QFileInfo(name).fileName();
    if (base.isEmpty())
        return {};

    QFile file(QDir(m_dirPath).filePath(base));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace ComicReader

