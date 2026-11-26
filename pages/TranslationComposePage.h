/**
 * @file   TranslationComposePage.h
 * @brief  创建 / 完善翻译页：翻译在线详情页的简介与详细信息，提交后进入审核
 * @author BlockBox Team
 * @date   2026-11-21
 *
 * 从 CurseForge / Modrinth 在线详情页（模组、整合包、资源包、光影包等）
 * 的「创建翻译 / 完善翻译」按钮进入。编辑器预填原文（或已有翻译），
 * 用户原地翻译后提交到方块盒子服务器，审核通过后才对所有人可见。
 * 提交需要登录论坛账号；本版目标语言固定简体中文。
 */
#pragma once

#include <QWidget>

#include "components/ForumKit.h"
#include "utils/mod/ModData.h"
#include "utils/translation/ContentTranslationService.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;

class TranslationComposePage : public QWidget
{
    Q_OBJECT

public:
    explicit TranslationComposePage(QWidget *parent = nullptr);

    /** 进入页面：带入资源信息与原文（原文用于预填编辑器） */
    void open(const ModInfo &info, const QString &originalSummary,
              const QString &originalDetailHtml);

signals:
    /** 未登录想提交翻译：请求跳到论坛账户页登录 */
    void loginRequested();
    /** 提交成功：回到详情页 */
    void submitted();

private:
    void buildUi();
    void updateState();
    /** 拉取自己的最新提交与公开翻译，决定编辑器预填内容与状态行文案 */
    void loadExisting();
    void prefillFrom(const ContentTranslation &translation);
    void prefillFromOriginal();
    void submit();
    void setStatus(const QString &text, bool error = false);

    QWidget *m_loginHost = nullptr;
    QWidget *m_mainHost = nullptr;

    QLabel *m_projectTitleLabel = nullptr;
    QLabel *m_projectSourceLabel = nullptr;
    QLabel *m_existingStatusLabel = nullptr;
    QComboBox *m_langCombo = nullptr;
    QLineEdit *m_summaryEdit = nullptr;
    QTextEdit *m_detailEdit = nullptr;
    QPushButton *m_submitButton = nullptr;
    QLabel *m_statusLabel = nullptr;

    ModInfo m_modInfo;
    QString m_originalSummary;    ///< 详情页当前显示的原文简介
    QString m_originalDetailHtml; ///< 详情页当前显示的原文详细信息（HTML）
    QString m_targetLang;         ///< 提交的目标语言（本版固定简体中文）
    ContentTranslation m_existing; ///< 已有翻译（自己的最新提交优先，其次公开翻译）
    bool m_loadingExisting = false;
};
