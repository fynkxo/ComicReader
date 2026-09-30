#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMutex>

namespace ComicReader {

/// 页面数据 LRU 缓存
///
/// 按总字节数限制容量，超出后淘汰最久未使用的页面。
/// 线程安全：可从 GUI 线程读取、从预加载线程写入。
class PageCache
{
public:
    explicit PageCache(qint64 maxBytes = 64ll * 1024 * 1024)
        : m_maxBytes(maxBytes) {}

    /// 设置容量上限，并立即触发淘汰
    void setMaxBytes(qint64 maxBytes);

    qint64 maxBytes() const { return m_maxBytes; }
    qint64 usedBytes() const { QMutexLocker lock(&m_mutex); return m_usedBytes; }
    int count() const { QMutexLocker lock(&m_mutex); return int(m_order.size()); }

    /// 查询页面是否已缓存
    bool contains(int index) const;

    /// 写入页面（空数据视为失败，不写入）
    void insert(int index, const QByteArray &data);

    /// 取出页面；命中则返回数据并刷新其活跃度
    QByteArray take(int index);

    void clear();

    /// 预估某页加入后是否仍能放入缓存（超过单页上限时返回 false）
    bool wouldFit(qint64 pageBytes) const;

private:
    /// 调用方必须已持有 m_mutex
    void evictLocked();

    mutable QMutex m_mutex;
    QHash<int, QByteArray> m_data;   ///< 页面索引 -> 数据
    QList<int> m_order;              ///< 活跃度排序，尾部最久未使用
    qint64 m_maxBytes = 0;
    qint64 m_usedBytes = 0;
    mutable int m_hitCount = 0;
    mutable int m_missCount = 0;

public:
    /// 缓存命中统计（用于调试与测试）
    int hitCount() const { QMutexLocker lock(&m_mutex); return m_hitCount; }
    int missCount() const { QMutexLocker lock(&m_mutex); return m_missCount; }
    void resetStats() { QMutexLocker lock(&m_mutex); m_hitCount = m_missCount = 0; }
};

} // namespace ComicReader
