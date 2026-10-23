/**
 * @file   CardListTranslator.cpp
 * @brief  卡片列表页「整页翻译」控制器实现
 */

#include "CardListTranslator.h"

#include "TranslatePayloadCodec.h"

#include <QRegularExpression>

#include "../utils/SettingsManager.h"
#include "../utils/translation/TranslationService.h"


namespace {

/// 送 AI 前把一段文本压成单行纯文本：去 HTML 标签、合并空白、按需截断。
/// 卡片简介常带整段 HTML 或超长描述，原样拼进请求会让整页翻译超出模型输出上限。
QString compactForAI(const QString &text, int maxLength)
{
    QString out = text;
    out.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    out.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    out = out.trimmed();
    if (out.size() > maxLength)
        out = out.left(maxLength) + QStringLiteral("…");
    return out;
}

} // namespace
CardListTranslator::CardListTranslator(QObject *parent)
    : QObject(parent)
{
}

CardListTranslator::~CardListTranslator() = default;

bool CardListTranslator::sameIds(const QList<ModInfo> &a, const QList<ModInfo> &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        if (a.at(i).id != b.at(i).id)
            return false;
    }
    return true;
}

bool CardListTranslator::containsHanText(const QString &text)
{
    for (const QChar &ch : text) {
        if (ch.script() == QChar::Script_Han)
            return true;
    }
    return false;
}

TranslationService *CardListTranslator::service()
{
    if (!m_service) {
        m_service = new TranslationService(this);
        connect(m_service, &TranslationService::listTranslationFinished,
                this, &CardListTranslator::onResponse);
        connect(m_service, &TranslationService::translationError,
                this, &CardListTranslator::onError);
    }
    return m_service;
}

void CardListTranslator::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged(busy);
}

bool CardListTranslator::syncBatch(const QList<ModInfo> &infos)
{
    if (sameIds(m_batch, infos) && !m_batch.isEmpty())
        return true;

    cancel();
    m_batch = infos;
    m_originals.clear();
    m_items.clear();
    m_translated.clear();
    m_ready = false;
    m_applied = false;
    return false;
}

void CardListTranslator::cancel()
{
    if (m_service)
        m_service->cancelCurrentRequest();
    setBusy(false);
}

void CardListTranslator::start(const QList<ModInfo> &infos)
{
    if (m_busy)
        return;

    m_batch = infos;
    m_originals = infos;
    m_items.clear();
    m_translated.clear();
    m_ready = false;
    m_applied = false;

    QStringList payload;
    for (int i = 0; i < infos.size(); ++i) {
        const ModInfo &info = infos.at(i);
        Item item;
        item.cardIndex = i;

        // 已有中文名的资源不再让 AI 重翻名称，简介仍可翻
        QString name = info.chineseName.isEmpty() ? compactForAI(info.name, 80) : QString();
        if (!name.isEmpty() && containsHanText(name))
            name.clear();
        QString desc = compactForAI(info.description, 180);
        if (!desc.isEmpty() && containsHanText(desc))
            desc.clear();

        item.wantName = !name.isEmpty();
        item.wantDesc = !desc.isEmpty();

        if (item.wantName || item.wantDesc) {
            item.marker = payload.size();
            payload << QStringLiteral("[#%1#] %2 ||| %3").arg(item.marker).arg(name, desc);
            m_items.append(item);
        } else if (!info.chineseName.isEmpty()) {
            // 名称本来就是中文：中文态直接顶上已知中文名
            m_items.append(item);
        }
    }

    if (payload.isEmpty()) {
        // 没有任何需要送 AI 的内容：直接落到中文态
        m_ready = true;
        emit finished();
        return;
    }

    setBusy(true);
    service()->translateList(payload.join(QLatin1Char('\n')),
                             SettingsManager::instance()->getTranslationConfig());
}

QMap<int, QPair<QString, QString>> CardListTranslator::parseResponse(const QString &text)
{
    return TranslatePayloadCodec::parse(text);
}
void CardListTranslator::onResponse(const QString &text)
{
    setBusy(false);

    if (m_items.isEmpty())
        return;

    const QMap<int, QPair<QString, QString>> parsed = parseResponse(text);
    if (parsed.isEmpty()) {
        m_ready = false;
        emit failed(tr("翻译结果格式异常，请稍后重试"));
        return;
    }

    int maxMarker = -1;
    for (const Item &item : m_items)
        maxMarker = qMax(maxMarker, item.marker);
    m_translated.clear();
    m_translated.resize(maxMarker + 1);
    for (auto it = parsed.cbegin(); it != parsed.cend(); ++it) {
        if (it.key() >= 0 && it.key() < m_translated.size())
            m_translated[it.key()] = it.value();
    }

    m_ready = true;
    emit finished();
}

void CardListTranslator::onError(const QString &error)
{
    setBusy(false);
    m_ready = false;
    emit failed(error);
}

QList<ModInfo> CardListTranslator::translated() const
{
    QList<ModInfo> result = m_originals;
    if (!m_ready)
        return result;

    for (const Item &item : m_items) {
        if (item.cardIndex < 0 || item.cardIndex >= result.size())
            continue;
        ModInfo &info = result[item.cardIndex];
        const QPair<QString, QString> text =
            (item.marker >= 0 && item.marker < m_translated.size())
                ? m_translated.at(item.marker)
                : qMakePair(QString(), QString());

        if (!info.chineseName.isEmpty()) {
            info.name = info.chineseName;   // 已有的中文名优先于 AI 译名
        } else if (item.wantName && !text.first.isEmpty()) {
            info.name = text.first;
        }
        if (item.wantDesc && !text.second.isEmpty())
            info.description = text.second;

        info.chineseName = info.name;   // 列表式（用 name）与瀑布流（优先 chineseName）统一显示中文标题
    }
    return result;
}
