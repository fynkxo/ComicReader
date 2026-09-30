#ifdef COMICREADER_HAS_7ZIP

#include "IComicArchive.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

#include "7zip/Archive/IArchive.h"
#include "7zip/IStream.h"
#include "Common/MyCom.h"
#include "Common/MyWindows.h"

#include <algorithm>

Q_LOGGING_CATEGORY(lc7z, "comicreader.7z")

namespace ComicReader {

namespace {

/// 7-Zip 格式处理器的 CLSID（{23170F69-40C1-278A-1000-000100020000}）。
/// vcpkg 的 7zip 端口未安装声明它的 7zip.h，故在此显式给出；
/// 该值是 7-Zip SDK 中稳定的公开常量。
const CLSID kFormatDll =
    { 0x23170F69, 0x40C1, 0x278A,
      { 0x10, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00 } };

/// 7-Zip 的 CreateObject 在 7zip.h 中声明，该头文件同样未安装。
/// 用 extern "C" 精确匹配 DLL 中的导出符号（Cdecl 约定由 SDK 决定）。
extern "C" HRESULT CreateObject(IUnknown **outObject, const CLSID &clsID,
                                const CLSID *clsIDOptional = 0);

/// 内存输出流：把 7-Zip 解压出的数据收集到 QByteArray
class MemoryOutStream : public ISequentialOutStream
{
public:
    QByteArray data;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out)
            return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_ISequentialOutStream) {
            *out = static_cast<ISequentialOutStream *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = --m_ref;
        if (r == 0)
            delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE Write(const void *data, UInt32 size,
                                   UInt32 *processed) noexcept override
    {
        if (processed)
            *processed = size;
        m_buffer.append(static_cast<const char *>(data), static_cast<qsizetype>(size));
        return S_OK;
    }

private:
    ULONG m_ref = 1;
    QByteArray m_buffer;
};

/// 打开回调：7-Zip 打开归档时需要，单卷文件无需额外能力
class OpenCallbackImpl : public IArchiveOpenCallback
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out)
            return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IArchiveOpenCallback) {
            *out = static_cast<IArchiveOpenCallback *>(this);
            AddRef();
            return S_OK;
        }
        // 不支持多卷：显式声明不提供取流能力
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = --m_ref;
        if (r == 0)
            delete this;
        return r;
    }

    // 进度回调：单文件读取无需汇报
    HRESULT STDMETHODCALLTYPE SetTotal(const UInt64 *, const UInt64 *) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64 *, const UInt64 *) override
    {
        return S_OK;
    }

private:
    ULONG m_ref = 1;
};

/// 读属性为字符串/布尔/整型的辅助函数
bool getBoolProp(IInArchive *arc, UInt32 index, PROPID propId, bool &out)
{
    PROPVARIANT prop;
    PropVariantInit(&prop);
    const HRESULT hr = arc->GetProperty(index, propId, &prop);
    bool result = false;
    if (SUCCEEDED(hr) && prop.vt == VT_BOOL)
        result = prop.boolVal != VARIANT_FALSE;
    PropVariantClear(&prop);
    out = result;
    return SUCCEEDED(hr);
}

bool getUIntProp(IInArchive *arc, UInt32 index, PROPID propId, quint64 &out)
{
    PROPVARIANT prop;
    PropVariantInit(&prop);
    const HRESULT hr = arc->GetProperty(index, propId, &prop);
    quint64 result = 0;
    if (SUCCEEDED(hr)) {
        if (prop.vt == VT_UI8)
            result = static_cast<quint64>(prop.uhVal.QuadPart);
        else if (prop.vt == VT_UI4)
            result = prop.ulVal;
        else if (prop.vt == VT_I4)
            result = static_cast<quint64>(prop.lVal);
    }
    PropVariantClear(&prop);
    out = result;
    return SUCCEEDED(hr);
}

bool getNameProp(IInArchive *arc, UInt32 index, PROPID propId, QString &out)
{
    PROPVARIANT prop;
    PropVariantInit(&prop);
    const HRESULT hr = arc->GetProperty(index, propId, &prop);
    QString result;
    if (SUCCEEDED(hr) && prop.vt == VT_BSTR && prop.bstrVal)
        result = QString::fromWCharArray(prop.bstrVal);
    PropVariantClear(&prop);
    out = result;
    return SUCCEEDED(hr);
}

} // namespace

// ---------------------------------------------------------------------------
// ArchiveHandle：持有 IInArchive 与其打开回调
// ---------------------------------------------------------------------------

class SevenZipArchiveReader::ArchiveHandle
{
public:
    CMyComPtr<IInArchive> archive;
    CMyComPtr<IArchiveOpenCallback> callback;   // 必须比 archive 活得久
};

SevenZipArchiveReader::SevenZipArchiveReader() = default;

SevenZipArchiveReader::~SevenZipArchiveReader() = default;

bool SevenZipArchiveReader::open(const QString &path, QString *error)
{
    QMutexLocker locker(&m_mutex);

    m_handle.reset();
    m_entries.clear();
    m_pageIndexes.clear();

    auto handle = std::make_unique<ArchiveHandle>();

    IUnknown *raw = nullptr;
    HRESULT hr = CreateObject(&raw, kFormatDll);
    if (FAILED(hr) || !raw) {
        if (error)
            *error = QStringLiteral("无法创建 7-Zip 归档对象 (hr=0x%1)")
                         .arg(static_cast<uint>(hr), 8, 16, QLatin1Char('0'));
        return false;
    }
    handle->archive.Attach(static_cast<IInArchive *>(raw));

    handle->callback.Attach(new OpenCallbackImpl());

    // 7-Zip 要求 UTF-16 路径并使用反斜杠分隔符
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    const std::wstring wide = native.toStdWString();
    hr = handle->archive->Open(reinterpret_cast<const UInt16 *>(wide.c_str()),
                               nullptr, handle->callback);
    if (FAILED(hr)) {
        if (error)
            *error = QStringLiteral("7-Zip 打开失败 (hr=0x%1)，可能需要密码或格式不支持")
                         .arg(static_cast<uint>(hr), 8, 16, QLatin1Char('0'));
        return false;
    }

    // 枚举条目
    const UInt32 total = handle->archive->GetNumberOfItems();
    m_entries.reserve(int(total));
    for (UInt32 i = 0; i < total; ++i) {
        Entry e;
        e.index = i;
        if (!getNameProp(handle->archive, i, kpidPath, e.name) || e.name.isEmpty())
            e.name = QString::fromLatin1("entry%1").arg(i);
        getUIntProp(handle->archive, i, kpidSize, e.size);
        getBoolProp(handle->archive, i, kpidIsDir, e.isDir);
        m_entries.append(e);
    }

    // 收集漫画页：排除目录与系统垃圾文件
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries.at(i);
        if (e.isDir)
            continue;
        const QString base = QFileInfo(e.name).fileName();
        if (base.isEmpty() || base.startsWith(QLatin1Char('.')))
            continue;
        if (!isImageFile(QFileInfo(base).suffix()))
            continue;
        m_pageIndexes.append(i);
    }

    if (m_pageIndexes.isEmpty()) {
        if (error)
            *error = QStringLiteral("归档中未找到任何图片");
        m_entries.clear();
        return false;
    }

    // 页面按文件名自然排序（page2 排在 page10 之前）
    std::sort(m_pageIndexes.begin(), m_pageIndexes.end(), [this](int a, int b) {
        return naturalLessThan(m_entries.at(a).name, m_entries.at(b).name);
    });

    m_handle = std::move(handle);
    return true;
}

QList<PageEntry> SevenZipArchiveReader::pages() const
{
    QList<PageEntry> result;
    result.reserve(m_pageIndexes.size());
    for (int idx : m_pageIndexes) {
        const Entry &e = m_entries.at(idx);
        result.append({QFileInfo(e.name).fileName(), static_cast<qint64>(e.size)});
    }
    return result;
}

QByteArray SevenZipArchiveReader::extractLocked(quint32 entryIndex) const
{
    if (!m_handle || !m_handle->archive)
        return {};

    CMyComPtr<ISequentialOutStream> outStream;
    CMyComPtr<ISequentialInStream> inStream;
    if (FAILED(m_handle->archive->GetStream(entryIndex, &inStream)) || !inStream)
        return {};

    // MemoryOutStream 由 7-Zip 释放，这里以裸指针接管
    IUnknown *rawOut = nullptr;
    if (FAILED(CreateObject(&rawOut, CLSID_NULL, nullptr)) && !rawOut) {
        // 忽略：下面直接手工 new，生命周期由 COM 引用计数管理
    }
    MemoryOutStream *memOut = new MemoryOutStream();
    outStream.Attach(static_cast<ISequentialOutStream *>(memOut));

    if (FAILED(m_handle->archive->Extract(entryIndex, inStream, nullptr, outStream))) {
        outStream.Release();
        return {};
    }

    QByteArray result = memOut->data;
    outStream.Release();   // 释放我们自己 new 的对象
    return result;
}

QByteArray SevenZipArchiveReader::pageData(int index) const
{
    if (index < 0 || index >= m_pageIndexes.size())
        return {};
    QMutexLocker locker(&m_mutex);
    return extractLocked(m_entries.at(m_pageIndexes.at(index)).index);
}

QByteArray SevenZipArchiveReader::fileData(const QString &name) const
{
    QMutexLocker locker(&m_mutex);
    if (m_entries.isEmpty())
        return {};
    const QString target = name.toLower();
    for (const Entry &e : m_entries) {
        if (e.name.toLower() == target)
            return extractLocked(e.index);
    }
    return {};
}

} // namespace ComicReader

#endif // COMICREADER_HAS_7ZIP
