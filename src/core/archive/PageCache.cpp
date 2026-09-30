#include "PageCache.h"

namespace ComicReader {

void PageCache::setMaxBytes(qint64 maxBytes)
{
    QMutexLocker lock(&m_mutex);
    m_maxBytes = qMax<qint64>(0, maxBytes);
    evictLocked();
}

bool PageCache::contains(int index) const
{
    QMutexLocker lock(&m_mutex);
    return m_data.contains(index);
}

void PageCache::insert(int index, const QByteArray &data)
{
    if (data.isEmpty())
        return;

    QMutexLocker lock(&m_mutex);

    // 重复写入时先扣减旧数据
    const auto it = m_data.constFind(index);
    if (it != m_data.constEnd()) {
        m_usedBytes -= it->size();
        m_data.erase(m_data.find(index));
        m_order.removeAll(index);
    }

    m_data.insert(index, data);
    m_order.append(index);
    m_usedBytes += data.size();

    evictLocked();
}

QByteArray PageCache::take(int index)
{
    QMutexLocker lock(&m_mutex);

    const auto it = m_data.constFind(index);
    if (it == m_data.constEnd()) {
        ++m_missCount;
        return {};
    }
    ++m_hitCount;

    // 刷新活跃度：移到列表末尾
    m_order.removeAll(index);
    m_order.append(index);
    return it.value();
}

void PageCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_data.clear();
    m_order.clear();
    m_usedBytes = 0;
    m_hitCount = 0;
    m_missCount = 0;
}

bool PageCache::wouldFit(qint64 pageBytes) const
{
    if (pageBytes <= 0)
        return false;
    if (m_maxBytes <= 0)
        return false;
    // 单页就超过总容量时不缓存，交由调用方直接使用
    return pageBytes <= m_maxBytes;
}

void PageCache::evictLocked()
{
    // 从最久未使用的一端淘汰，直到容量达标
    while (m_usedBytes > m_maxBytes && !m_order.isEmpty()) {
        const int victim = m_order.takeFirst();
        const auto it = m_data.constFind(victim);
        if (it != m_data.constEnd()) {
            m_usedBytes -= it->size();
            m_data.erase(m_data.find(victim));
        }
    }
    if (m_usedBytes < 0)
        m_usedBytes = 0;
}

} // namespace ComicReader
