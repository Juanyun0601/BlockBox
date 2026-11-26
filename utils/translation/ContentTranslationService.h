/**
 * @file   ContentTranslationService.h
 * @brief  社区内容翻译服务：详情页人工翻译的拉取与提交（提交后进入审核）
 * @author BlockBox Team
 * @date   2026-11-21
 *
 * 在线详情页（CurseForge / Modrinth 的模组、整合包、资源包、光影包等）
 * 的「创建翻译 / 完善翻译」功能共用这一个入口。翻译数据存放在论坛服务器，
 * 所有请求都转发给 ForumAccountService（自动带上登录 Cookie、论坛地址和
 * 统一的错误解析），本服务只负责拼接口与解析数据结构。
 *
 * 服务端接口契约（服务端后续按此实现；客户端对 404 / 报错一律按
 * 「暂无翻译」或「提交失败」处理，不影响详情页其他功能）：
 * - GET  /api/content-translations?source=&project_id=&lang=
 *      返回审核通过的公开翻译：{"translation": {...}}；没有时 404 或 translation 为 null。
 * - GET  /api/content-translations/mine?source=&project_id=&lang=   （需要登录）
 *      返回自己最新一条提交（含待审核 / 已驳回）：{"translation": {...}}。
 * - POST /api/content-translations   （需要登录）
 *      body: source, project_id, project_name, lang, summary, detail_html,
 *            source_summary, source_detail_html
 *      服务端入库状态为待审核（pending），审核通过后才对所有人公开。
 *
 * 回调风格与 ForumAccountService / FeedbackService 一致：
 * onOk 返回解析好的结构体，onFail 的 message 可直接展示给用户。
 */
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

#include "../ForumAccountService.h"

/** 一份内容翻译（公开翻译或自己的提交） */
struct ContentTranslation
{
    QString source;      ///< 资源来源：curseforge / modrinth
    QString projectId;   ///< 资源在该来源下的项目 id
    QString lang;        ///< 翻译目标语言（如 zh-CN）
    QString summary;     ///< 翻译后的简介
    QString detailHtml;  ///< 翻译后的详细信息（HTML）
    QString authorName;  ///< 提交人昵称（公开翻译返回时用于展示）
    QString status;      ///< approved（已通过）/ pending（待审核）/ rejected（已驳回）
    qint64 updatedAt = 0;

    bool isValid() const { return !summary.isEmpty() || !detailHtml.isEmpty(); }
    bool isApproved() const { return status == QStringLiteral("approved"); }
    bool isPending() const { return status == QStringLiteral("pending"); }
    bool isRejected() const { return status == QStringLiteral("rejected"); }
};

class ContentTranslationService : public QObject
{
    Q_OBJECT

public:
    using FailHandler = ForumAccountService::FailHandler;

    static ContentTranslationService *instance();

    /**
     * 翻译条的目标语言。启动器界面目前只有中文，固定简体中文；
     * lang 字段随数据一起保存，服务端支持多语言后这里改成跟随启动器语言即可。
     */
    static QString defaultTargetLang() { return QStringLiteral("zh-CN"); }

    /** 拉取某资源审核通过的公开翻译；暂无翻译时 onOk 收到 isValid()=false 的结构 */
    void fetchTranslation(const QString &source, const QString &projectId, const QString &lang,
                          std::function<void(const ContentTranslation &)> onOk,
                          FailHandler onFail = nullptr);

    /** 拉取自己对该资源的最新提交（含待审 / 驳回），用于「完善翻译」预填与状态展示；需要登录 */
    void fetchMyTranslation(const QString &source, const QString &projectId, const QString &lang,
                            std::function<void(const ContentTranslation &)> onOk,
                            FailHandler onFail = nullptr);

    /** 提交一份翻译：服务端入库为「待审核」，审核通过后才对所有人可见；需要登录 */
    void submitTranslation(const ContentTranslation &draft,
                           const QString &sourceSummary, const QString &sourceDetailHtml,
                           const QString &projectName,
                           std::function<void()> onOk, FailHandler onFail = nullptr);

    /** 服务端 JSON → 翻译结构 */
    static ContentTranslation parseTranslation(const QJsonObject &object);
};
