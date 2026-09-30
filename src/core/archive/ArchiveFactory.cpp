#include "IComicArchive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace ComicReader {

/// 是否为受支持的图片扩展名
bool isImageFile(const QString &suffix)
{
    static const QSet<QString> kImageSuffixes = {
        QStringLiteral("jpg"),  QStringLiteral("jpeg"),
        QStringLiteral("png"),  QStringLiteral("gif"),
        QStringLiteral("bmp"),  QStringLiteral("webp"),
        QStringLiteral("tif"),  QStringLiteral("tiff"),
        QStringLiteral("avif"),
    };
    return kImageSuffixes.contains(suffix.toLower());
}

/// 数字自然排序：page2 < page10（漫画页码常见前导零差异）
bool naturalLessThan(const QString &a, const QString &b)
{
    const QString ua = a.toLower();
    const QString ub = b.toLower();
    int i = 0;
    int j = 0;
    while (i < ua.size() && j < ub.size()) {
        const QChar ca = ua.at(i);
        const QChar cb = ub.at(j);
        if (ca.isDigit() && cb.isDigit()) {
            // 比较连续数字段，按数值大小排序
            int si = i;
            int sj = j;
            while (i < ua.size() && ua.at(i).isDigit())
                ++i;
            while (j < ub.size() && ub.at(j).isDigit())
                ++j;
            const qlonglong na = ua.mid(si, i - si).toLongLong();
            const qlonglong nb = ub.mid(sj, j - sj).toLongLong();
            if (na != nb)
                return na < nb;
        } else {
            if (ca != cb)
                return ca < cb;
            ++i;
            ++j;
        }
    }
    return ua.size() - i < ub.size() - j;
}

bool isSupportedComic(const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir())
        return true;

    const QString suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("zip") || suffix == QStringLiteral("cbz"))
        return true;
    return isImageFile(suffix);
}

IComicArchive *createComicArchive(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return nullptr;
    if (info.isDir())
        return new FolderArchiveReader();
    return new ZipArchiveReader();
}

} // namespace ComicReader
