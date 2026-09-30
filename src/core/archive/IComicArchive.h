#pragma once

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QMutex>
#include <QString>
#include <QStringList>

namespace ComicReader {

/// 漫画来源类型
enum class ArchiveType {
    ZipArchive,   ///< .zip / .cbz 等 ZIP 容器
    ImageFolder   ///< 普通图片文件夹
};

/// 归档中的单页信息
struct PageEntry {
    QString name;       ///< 条目原始名称（ZIP 内为完整路径）
    qint64 size = 0;    ///< 未压缩字节数
};

/// 漫画档案读取器接口：对外提供页列表与页数据
class IComicArchive
{
public:
    virtual ~IComicArchive() = default;

    /// 打开归档，失败时返回 false 并写入 errorString
    virtual bool open(const QString &path, QString *error) = 0;

    /// 全部页面（已按自然顺序排序，仅含图片）
    virtual QList<PageEntry> pages() const = 0;

    /// 读取指定页的原始图像字节
    /// 注意：可能从后台线程调用，实现必须保证线程安全。
    virtual QByteArray pageData(int index) const = 0;

    /// 按条目名读取任意文件（如 ComicInfo.xml），不存在时返回空
    /// name 可为完整路径，大小写不敏感。
    virtual QByteArray fileData(const QString &name) const;

    virtual ArchiveType type() const = 0;
};

/// 根据扩展名判断归档类型；不支持则返回 false
bool isSupportedComic(const QString &path);

/// 是否为受支持的图片扩展名
bool isImageFile(const QString &suffix);

/// 数字自然排序：page2 < page10（漫画页码常见前导零差异）
bool naturalLessThan(const QString &a, const QString &b);

/// ZIP 容器读取器（.zip / .cbz）
class ZipArchiveReader : public IComicArchive
{
public:
    bool open(const QString &path, QString *error) override;
    QList<PageEntry> pages() const override;
    QByteArray pageData(int index) const override;
    QByteArray fileData(const QString &name) const override;
    ArchiveType type() const override { return ArchiveType::ZipArchive; }

private:
    struct CentralEntry {
        QString name;
        quint16 compressionMethod = 0;
        qint64 compressedSize = 0;
        qint64 uncompressedSize = 0;
        qint64 localHeaderOffset = 0;
        bool isDirectory = false;
    };

    mutable QFile m_file;
    mutable QMutex m_mutex;        ///< 保护 m_file：后台线程可能并发读取
    QList<CentralEntry> m_entries;  ///< 含目录等全部条目
    QList<int> m_pageIndexes;        ///< m_entries 中属于漫画页的下标
};

/// 普通图片文件夹读取器
class FolderArchiveReader : public IComicArchive
{
public:
    bool open(const QString &path, QString *error) override;
    QList<PageEntry> pages() const override;
    QByteArray pageData(int index) const override;
    QByteArray fileData(const QString &name) const override;
    ArchiveType type() const override { return ArchiveType::ImageFolder; }

private:
    QString m_dirPath;
    QList<PageEntry> m_pages;
};

/// 工厂：创建匹配的读取器，path 为空时返回 nullptr
IComicArchive *createComicArchive(const QString &path);

} // namespace ComicReader
