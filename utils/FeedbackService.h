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

/** 一条反馈（我的反馈列表行） */
struct FeedbackItem
{
    int id = 0;
    QString source;      ///< web（网页提交）/ launcher（启动器提交）
    QString content;     ///< 反馈正文
    QString appVersion;  ///< 启动器提交时附带的版本号
    QString status;      ///< open（待处理）/ resolved（已处理）/ ignored（已忽略）
    QString reply;       ///< 管理员的处理回复
    qint64 handledAt = 0;
    qint64 createdAt = 0;

    bool isValid() const { return id > 0; }
};

class FeedbackService : public QObject
{
    Q_OBJECT

public:
    using FailHandler = ForumAccountService::FailHandler;

    static FeedbackService *instance();

    /** 提交一条反馈（来源固定为 launcher，自动附带当前版本号） */
    void submitFeedback(const QString &content,
                        std::function<void(int feedbackId)> onOk, FailHandler onFail = nullptr);

    /** 我的反馈列表（含处理状态与管理员回复） */
    void fetchMyFeedbacks(int page,
                          std::function<void(const QVector<FeedbackItem> &,
                                             const ForumPagination &)> onOk,
                          FailHandler onFail = nullptr);

private:
    explicit FeedbackService(QObject *parent = nullptr);

    static FeedbackItem parseFeedback(const QJsonObject &object);
};
