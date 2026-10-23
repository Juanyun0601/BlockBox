/**
 * @file   AiModelFetchRow.h
 * @brief  「获取服务商可用模型列表」控件（按钮 + 状态提示）
 * @author BlockBox Team
 * @date   2026-10-22
 *
 * AI 模型设置中复用：把该控件放到「API 地址 / API Key」输入框下方，
 * 用户点击按钮即可拉取服务商当前可用的模型列表，结果通过 modelsReady() 交给调用方。
 */

#ifndef AIMODELFETCHROW_H
#define AIMODELFETCHROW_H

#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

#include "utils/AiModelListFetcher.h"

class QLabel;
class QPushButton;

class AiModelFetchRow : public QWidget
{
    Q_OBJECT

public:
    explicit AiModelFetchRow(QWidget *parent = nullptr);

    /// 绑定 API 地址与密钥的取值函数（通常指向界面上的输入框，保证读到最新内容）
    void setSourceProviders(std::function<QString()> apiUrlProvider,
                            std::function<QString()> apiKeyProvider);

signals:
    /// 获取成功：ids 为服务商当前可用的模型 ID
    void modelsReady(const QStringList &ids);

    /// 获取失败：error 为面向用户的中文原因
    void fetchFailed(const QString &error);

private:
    void startFetch();
    void onFetchFinished(const AiModelListResult &result);
    void setStatus(const QString &text, const QString &toolTip = QString());

    QLabel *m_statusLabel;
    QPushButton *m_fetchButton;
    AiModelListFetcher *m_fetcher;
    std::function<QString()> m_apiUrlProvider;
    std::function<QString()> m_apiKeyProvider;
};

#endif // AIMODELFETCHROW_H
