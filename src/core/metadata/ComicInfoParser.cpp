#include "ComicInfoParser.h"
#include "../archive/IComicArchive.h"

#include <QLoggingCategory>
#include <QXmlStreamReader>

Q_LOGGING_CATEGORY(lcMeta, "comicreader.meta")

namespace ComicReader {

namespace {

/// ComicInfo.xml 可能使用的文件名（部分工具写为小写）
const char *const kCandidateNames[] = {"ComicInfo.xml", "comicinfo.xml"};

/// 解析 ISO 日期（ComicInfo 常见 2020-01-02 或带时间形式）
QDate parseIsoDate(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return {};
    return QDate::fromString(t.left(10), Qt::ISODate);
}

/// 清洗文本：折叠空白并去除首尾空白
QString clean(const QString &text)
{
    QString t = text.simplified();
    return t;
}

} // namespace

bool ComicInfoParser::parse(const QByteArray &xml, ComicMetadata *out, QString *error)
{
    if (!out)
        return false;
    if (xml.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("ComicInfo.xml 内容为空");
        return false;
    }

    // 许多 Windows 工具写出的 XML 带 UTF-8 BOM，QXmlStreamReader 会因此报
    // "Start tag expected"，故先剥离 BOM 再解析。
    QByteArray data = xml;
    if (data.startsWith("\xEF\xBB\xBF"))
        data.remove(0, 3);

    QXmlStreamReader reader(data);
    ComicMetadata meta;

    // 遍历策略：只处理根元素 <ComicInfo> 的直接子元素（叶子节点）。
    // 根元素自身与 <Pages> 等容器节点含子元素，若对其调用 readElementText()
    // 会报 "Expected character data"，因此必须显式跳过其子树。
    // 注意：readElementText() 必须在 StartElement 上调用，且它会一并消费掉
    // 配对的 EndElement，故下面的深度计数需相应 -1。
    if (!reader.readNextStartElement()) {
        if (error)
            *error = QStringLiteral("未找到根元素: %1").arg(reader.errorString());
        return false;
    }

    int depth = 1;
    while (!reader.atEnd() && depth > 0) {
        const QXmlStreamReader::TokenType type = reader.readNext();

        if (type == QXmlStreamReader::EndElement) {
            --depth;
            continue;
        }
        if (type != QXmlStreamReader::StartElement)
            continue;

        ++depth;
        const QString tag = reader.name().toString();

        if (depth == 2) {
            // 根的直接子元素：叶子节点，读取其文本
            const QString value = clean(reader.readElementText());
            --depth;   // readElementText 已消费配对的 EndElement

            if (tag.compare(QStringLiteral("Title"), Qt::CaseInsensitive) == 0)
                meta.title = value;
            else if (tag.compare(QStringLiteral("Series"), Qt::CaseInsensitive) == 0)
                meta.series = value;
        else if (tag.compare(QStringLiteral("Summary"), Qt::CaseInsensitive) == 0)
            meta.summary = value;
        else if (tag.compare(QStringLiteral("Notes"), Qt::CaseInsensitive) == 0)
            meta.notes = value;
        else if (tag.compare(QStringLiteral("Publisher"), Qt::CaseInsensitive) == 0)
            meta.publisher = value;
        else if (tag.compare(QStringLiteral("Writer"), Qt::CaseInsensitive) == 0)
            meta.writer = value;
        else if (tag.compare(QStringLiteral("Penciller"), Qt::CaseInsensitive) == 0)
            meta.penciller = value;
        else if (tag.compare(QStringLiteral("Inker"), Qt::CaseInsensitive) == 0)
            meta.inker = value;
        else if (tag.compare(QStringLiteral("Colorist"), Qt::CaseInsensitive) == 0)
            meta.colorist = value;
        else if (tag.compare(QStringLiteral("Letterer"), Qt::CaseInsensitive) == 0)
            meta.letterer = value;
        else if (tag.compare(QStringLiteral("CoverArtist"), Qt::CaseInsensitive) == 0)
            meta.coverArtist = value;
        else if (tag.compare(QStringLiteral("Editor"), Qt::CaseInsensitive) == 0)
            meta.editor = value;
        else if (tag.compare(QStringLiteral("Translator"), Qt::CaseInsensitive) == 0)
            meta.translator = value;
        else if (tag.compare(QStringLiteral("Imprint"), Qt::CaseInsensitive) == 0)
            meta.imprint = value;
        else if (tag.compare(QStringLiteral("Web"), Qt::CaseInsensitive) == 0)
            meta.web = value;
        else if (tag.compare(QStringLiteral("PageCount"), Qt::CaseInsensitive) == 0)
            meta.pageCountHint = value;
        else if (tag.compare(QStringLiteral("Manga"), Qt::CaseInsensitive) == 0)
            meta.manga = value;
        else if (tag.compare(QStringLiteral("StoryArc"), Qt::CaseInsensitive) == 0)
            meta.storyArc = value;
        else if (tag.compare(QStringLiteral("StoryArcNumber"), Qt::CaseInsensitive) == 0)
            meta.storyArcNumber = value;
        else if (tag.compare(QStringLiteral("SeriesGroup"), Qt::CaseInsensitive) == 0)
            meta.seriesGroup = value;
        else if (tag.compare(QStringLiteral("AgeRating"), Qt::CaseInsensitive) == 0)
            meta.ageRating = value;
        else if (tag.compare(QStringLiteral("LanguageISO"), Qt::CaseInsensitive) == 0)
            meta.languageIso = value;
        else if (tag.compare(QStringLiteral("Genre"), Qt::CaseInsensitive) == 0) {
            // Genre 可重复出现，逐个收集为标签
            if (!value.isEmpty() && !meta.tags.contains(value))
                meta.tags.append(value);
        } else if (tag.compare(QStringLiteral("Pages"), Qt::CaseInsensitive) == 0) {
            // 忽略逐页信息（Page 元素），仅消费文本避免解析器落后
        } else if (tag.compare(QStringLiteral("Tags"), Qt::CaseInsensitive) == 0) {
            // 复合作者字段，映射到 Writer
            if (meta.writer.isEmpty())
                meta.writer = value;
        } else if (tag.compare(QStringLiteral("PublishDate"), Qt::CaseInsensitive) == 0)
            meta.publishDate = parseIsoDate(value);
        else if (tag.compare(QStringLiteral("DateAdded"), Qt::CaseInsensitive) == 0)
            meta.dateAdded = parseIsoDate(value);
        else if (tag.compare(QStringLiteral("LastMarked"), Qt::CaseInsensitive) == 0)
            meta.lastMarked = parseIsoDate(value);
        }
        // depth > 2（如 <Pages><Page/>）：容器子树不处理，继续向下遍历
    }

    if (reader.hasError()) {
        if (error)
            *error = QStringLiteral("XML 解析失败: %1 (行 %2)")
                         .arg(reader.errorString()).arg(reader.lineNumber());
        return false;
    }

    // PageCount 转为整数；异常值忽略并保留 0
    bool ok = false;
    const int pages = meta.pageCountHint.toInt(&ok);
    meta.pageCount = ok && pages > 0 ? pages : 0;

    if (!meta.isValid()) {
        if (error)
            *error = QStringLiteral("ComicInfo.xml 中未找到有效元数据");
        return false;
    }

    *out = meta;
    return true;
}

bool ComicInfoParser::parseFromArchive(IComicArchive *archive,
                                       ComicMetadata *out, QString *error)
{
    if (!archive || !out)
        return false;

    bool foundAny = false;
    for (const char *name : kCandidateNames) {
        const QByteArray xml = archive->fileData(QString::fromLatin1(name));
        if (xml.isEmpty())
            continue;
        foundAny = true;
        if (parse(xml, out, error))
            return true;
        // 找到文件但解析失败时继续尝试下一个候选名
    }

    // 仅在从未找到候选文件时才报告"未找到"，避免覆盖具体的解析错误
    if (!foundAny && error)
        *error = QStringLiteral("归档中未找到 ComicInfo.xml");
    return false;
}

} // namespace ComicReader
