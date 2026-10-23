/**
 * @file   AiModelFetchRow.cpp
 * @brief  「获取服务商可用模型列表」控件实现
 * @author BlockBox Team
 * @date   2026-10-22
 */

#include "components/AiModelFetchRow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

AiModelFetchRow::AiModelFetchRow(QWidget *parent)
    : QWidget(parent)
    , m_statusLabel(new QLabel(this))
    , m_fetchButton(new QPushButton(tr("获取模型列表"), this))
    , m_fetcher(nullptr)
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_statusLabel->setObjectName(QStringLiteral("settingDesc"));
    m_statusLabel->setWordWrap(false);
    layout->addWidget(m_statusLabel, 1);

    m_fetchButton->setCursor(Qt::PointingHandCursor);
    m_fetchButton->setToolTip(tr("使用当前填写的 API 地址与密钥，拉取该服务商现在可用的模型"));
    layout->addWidget(m_fetchButton);

    connect(m_fetchButton, &QPushButton::clicked, this, &AiModelFetchRow::startFetch);
}

void AiModelFetchRow::setSourceProviders(std::function<QString()> apiUrlProvider,
                                         std::function<QString()> apiKeyProvider)
{
    m_apiUrlProvider = std::move(apiUrlProvider);
    m_apiKeyProvider = std::move(apiKeyProvider);
}

void AiModelFetchRow::setStatus(const QString &text, const QString &toolTip)
{
    m_statusLabel->setText(text);
    m_statusLabel->setToolTip(toolTip.isEmpty() ? text : toolTip);
}

void AiModelFetchRow::startFetch()
{
    if (m_fetcher && m_fetcher->isRunning())
        return;

    const QString apiUrl = m_apiUrlProvider ? m_apiUrlProvider().trimmed() : QString();
    const QString apiKey = m_apiKeyProvider ? m_apiKeyProvider().trimmed() : QString();

    if (apiUrl.isEmpty()) {
        setStatus(tr("请先填写 API 地址"));
        return;
    }

    // 本地端点（Ollama / LM Studio 等）通常无需密钥，其余服务商必须先填 Key
    const QString lowerUrl = apiUrl.toLower();
    const bool localEndpoint = lowerUrl.contains(QLatin1String("localhost"))
                               || lowerUrl.contains(QLatin1String("127.0.0.1"))
                               || lowerUrl.contains(QLatin1String("0.0.0.0"))
                               || lowerUrl.contains(QLatin1String("[::1]"));
    if (apiKey.isEmpty() && !localEndpoint) {
        setStatus(tr("请先填写 API Key 再获取模型列表"));
        return;
    }

    m_fetchButton->setEnabled(false);
    m_fetchButton->setText(tr("获取中…"));
    setStatus(tr("正在获取模型列表…"));

    if (!m_fetcher) {
        m_fetcher = new AiModelListFetcher(this);
        connect(m_fetcher, &AiModelListFetcher::finished, this, &AiModelFetchRow::onFetchFinished);
    }
    m_fetcher->fetch(apiUrl, apiKey);
}

void AiModelFetchRow::onFetchFinished(const AiModelListResult &result)
{
    m_fetchButton->setEnabled(true);
    m_fetchButton->setText(tr("获取模型列表"));

    if (!result.ok) {
        setStatus(tr("获取失败"), result.error);
        emit fetchFailed(result.error);
        return;
    }

    setStatus(tr("已获取 %1 个可用模型").arg(result.ids.size()), result.endpoint);
    emit modelsReady(result.ids);
}
