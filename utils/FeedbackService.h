/**
 * @file   FeedbackService.h
 * @brief  用户反馈服务：提交软件问题反馈 / 查询我的反馈列表
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 「设置 > 关于」的反馈入口与软件内反馈页共用这一个入口。
 * 所有请求都转发给 ForumAccountService（自动带上登录 Cookie、
 * 论坛地址和统一的错误解析），本服务只负责拼接口与解析数据结构；
 * 反馈存放在论坛服务器，官网/论坛网页版提交的反馈在同一个后台处理。
 *
 * 回调风格与 ForumAccountService 一致：onOk 返回解析好的结构体，
 * onFail 的 message 可直接展示给用户（论坛返回的中文错误原样透传）。
 */
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

#include "ForumAccountService.h"
#include "ForumContentService.h"

/**
 * 提交反馈的结构化字段（服务端会校验并组装成正文）。
 * 问题反馈的字段与 GitHub Issue 模板（.github/ISSUE_TEMPLATE/bug_report.yml）一致。
 */
struct FeedbackDraft
{
    QString type;        ///< bug（问题反馈）/ feature（功能建议）
    QString appVersion;  ///< 问题反馈必填：启动器版本（launcher 提交时默认当前版本）
    QString platform;    ///< 问题反馈必填：运行平台（下拉五选一）
    QString osVersion;   ///< 选填：系统版本
    QString description; ///< 必填：问题描述 / 建议内容
    QString steps;       ///< 问题反馈必填：复现步骤
    QString expected;    ///< 选填：期望结果
    QString actual;      ///< 选填：实际结果
    QString logs;        ///< 选填：日志
    QString screenshots; ///< 选填：截图 / 录屏说明
    QString scene;       ///< 功能建议选填：使用场景
};

/** 一条反馈（我的反馈列表行） */
struct FeedbackItem
{
    int id = 0;
    QString type;        ///< bug（问题反馈）/ feature（功能建议）
    QString source;      ///< web（网页提交）/ launcher（启动器提交）
    QString content;     ///< 反馈正文
    QString appVersion;  ///< 启动器提交时附带的版本号
    QString status;      ///< open（待处理）/ resolved（已处理）/ ignored（已忽略）
    QString reply;       ///< 管理员的处理回复
    qint64 handledAt = 0;
    qint64 createdAt = 0;

    bool isValid() const { return id > 0; }
};

/** 公开反馈列表的一条（仿 GitHub Issues 列表行，所有人可见） */
struct PublicFeedbackItem
{
    int id = 0;
    QString type;        ///< bug / feature
    QString status;      ///< open / resolved / ignored
    QString title;       ///< 列表标题（服务端从正文提取的第一行描述）
    QString content;     ///< 反馈正文（展开详情用）
    QString reply;       ///< 管理员的公开回复
    qint64 handledAt = 0;
    qint64 createdAt = 0;
    QString authorName;  ///< 提交人昵称，空表示已注销

    bool isValid() const { return id > 0; }
};

/** 各状态的反馈计数（公开列表页签用） */
struct FeedbackCounts
{
    int open = 0;
    int resolved = 0;
    int ignored = 0;
};

class FeedbackService : public QObject
{
    Q_OBJECT

public:
    using FailHandler = ForumAccountService::FailHandler;

    static FeedbackService *instance();

    /** 提交一条反馈（来源固定为 launcher，自动附带当前版本号；结构化字段由服务端组装成正文） */
    void submitFeedback(const FeedbackDraft &draft,
                        std::function<void(int feedbackId)> onOk, FailHandler onFail = nullptr);

    /** 我的反馈列表（含处理状态与管理员回复） */
    void fetchMyFeedbacks(int page,
                          std::function<void(const QVector<FeedbackItem> &,
                                             const ForumPagination &)> onOk,
                          FailHandler onFail = nullptr);

    /** 公开反馈列表（所有人可见，仿 GitHub Issues 列表页；status = all/open/resolved/ignored） */
    void fetchPublicFeedbacks(const QString &status, int page,
                              std::function<void(const QVector<PublicFeedbackItem> &,
                                                 const FeedbackCounts &,
                                                 const ForumPagination &)> onOk,
                              FailHandler onFail = nullptr);

private:
    explicit FeedbackService(QObject *parent = nullptr);

    static FeedbackItem parseFeedback(const QJsonObject &object);
};
