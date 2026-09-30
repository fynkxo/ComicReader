#pragma once

#include <QDate>
#include <QString>
#include <QStringList>

namespace ComicReader {

class IComicArchive;

/// 漫画元数据（ComicRack 的 ComicInfo.xml 标准格式）
struct ComicMetadata {
    QString title;            ///< Title
    QString series;           ///< Series（系列名）
    QString summary;          ///< Summary（简介）
    QString notes;            ///< Notes
    QString publisher;        ///< Publisher
    QString writer;           ///< Writer / Penciller（作者）
    QString penciller;        ///< Penciller
    QString inker;            ///< Inker
    QString colorist;         ///< Colorist
    QString letterer;         ///< Letterer
    QString coverArtist;      ///< CoverArtist
    QString editor;           ///< Editor
    QString translator;       ///< Translator
    QString imprint;          ///< Imprint
    QString genre;            ///< Genre
    QString web;              ///< Web
    QString pageCountHint;    ///< PageCount（字符串形式，便于兼容异常值）
    QString manga;            ///< Manga（"Yes"/"No" 等）
    QString storyArc;         ///< StoryArc
    QString storyArcNumber;   ///< StoryArcNumber
    QString seriesGroup;      ///< SeriesGroup
    QString ageRating;        ///< AgeRating
    QString languageIso;      ///< LanguageISO
    QDate publishDate;        ///< 发布日期
    QDate dateAdded;          ///< 加入日期
    QDate lastMarked;         ///< 最后标记日期
    QStringList tags;         ///< 多个 <Genre> 标签
    int pageCount = 0;        ///< 由 PageCount 解析出的整数，0 表示未知

    /// 是否解析到了任何内容
    bool isValid() const { return !title.isEmpty() || !series.isEmpty()
                               || !writer.isEmpty() || !summary.isEmpty(); }
};

/// ComicInfo.xml 解析器
class ComicInfoParser
{
public:
    /// 从 XML 字节解析；失败返回 false 并写入 error
    static bool parse(const QByteArray &xml, ComicMetadata *out, QString *error = nullptr);

    /// 从漫画归档/文件夹中读取并解析 ComicInfo.xml
    /// 会依次尝试 "ComicInfo.xml" 与根目录下的 "comicinfo.xml"。
    /// 未找到或解析失败返回 false（out 保持不变）。
    static bool parseFromArchive(class IComicArchive *archive,
                                 ComicMetadata *out,
                                 QString *error = nullptr);
};

} // namespace ComicReader
