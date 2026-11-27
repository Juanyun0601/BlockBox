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

    /** 详细信息编辑框上方的格式栏：撤销重做、加粗斜体下划线删除线、段落样式、列表、链接、清格式 */
    QWidget *createFormatBar(QWidget *parent);
    /** 加粗 / 斜体 / 下划线 / 删除线，作用在选区或之后输入的文字上 */
    void toggleInlineStyle(int kind, bool on);
    /** 段落样式：0 正文 / 1 小标题 / 2 标题（作用在选区覆盖的整段） */
    void applyTextStyle(int level);
    /** 有序 / 无序列表开关（作用在选区覆盖的整段） */
    void toggleList(bool ordered);
    void insertLink();
    void clearFormatting();
    /** 让格式栏按钮跟着光标处的实际格式走 */
    void syncFormatBar();
    /** 换资源 / 重新预填后，把上一份内容留下的「下一段输入格式」清掉 */
    void resetDetailEditingState();
    /** 正文默认字号（磅）；标题按它的倍数放大，导出 HTML 里的字号也以它为准 */
    qreal bodyPointSize() const;

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

    QComboBox *m_styleCombo = nullptr;
    QPushButton *m_boldButton = nullptr;
    QPushButton *m_italicButton = nullptr;
    QPushButton *m_underlineButton = nullptr;
    QPushButton *m_strikeButton = nullptr;
    QPushButton *m_bulletButton = nullptr;
    QPushButton *m_numberButton = nullptr;
    QPushButton *m_undoButton = nullptr;
    QPushButton *m_redoButton = nullptr;

    ModInfo m_modInfo;
    QString m_originalSummary;    ///< 详情页当前显示的原文简介
    QString m_originalDetailHtml; ///< 详情页当前显示的原文详细信息（HTML）
    QString m_targetLang;         ///< 提交的目标语言（本版固定简体中文）
    ContentTranslation m_existing; ///< 已有翻译（自己的最新提交优先，其次公开翻译）
    bool m_loadingExisting = false;
};
