/**
 * @file   ChatSettingsDialog.h
 * @brief  聊天设置弹窗：居中的模糊遮罩卡片，管理当前会话整理项与聊天通用偏好
 * @author BlockBox Team
 * @date   2026-11-20
 *
 * 与好友私信页共用的「居中窗口」：点聊天页头部的「设置」打开，
 * 上半部分是当前会话的置顶 / 免打扰，下半部分是发送方式与聊天字号。
 * 改动即时生效，点卡片外的遮罩或「完成」关闭。
 */
#pragma once

#include "AppDialogBase.h"

class CustomCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QShowEvent;
class QWidget;

class ChatSettingsDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit ChatSettingsDialog(QWidget *parent = nullptr);

    /**
     * @brief 指定当前打开的会话
     * @param key   ChatUiStore 的会话 key（f12 / g3）；为空表示没有会话，只显示通用设置
     * @param name  会话名（好友昵称 / 群名）
     * @param group key 是否是群聊
     */
    void setConversation(const QString &key, const QString &name, bool group);

signals:
    /** 弹窗里清空了这条会话的草稿（聊天页据此把输入框里的文字也清掉） */
    void draftCleared();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void initUI();
    void initStyle();
    /** 一行设置：左侧标题 + 说明，右侧滑块开关 */
    QWidget *makeSwitchRow(QWidget *parent, const QString &title, const QString &hint,
                           CustomCheckBox **toggleOut);
    /** 让字号分段按钮停在当前档位上 */
    void syncFontButtons();
    /** 把备注输入框里的内容写进本机记录（失去焦点 / 回车 / 关闭弹窗都会调） */
    void commitRemark();

    QString m_key;
    QWidget *m_card = nullptr;
    QScrollArea *m_bodyScroll = nullptr;
    QWidget *m_conversationSection = nullptr;
    QLabel *m_conversationName = nullptr;
    QLineEdit *m_remarkEdit = nullptr;
    CustomCheckBox *m_pinSwitch = nullptr;
    CustomCheckBox *m_muteSwitch = nullptr;
    CustomCheckBox *m_hidePreviewSwitch = nullptr;
    QWidget *m_draftRow = nullptr;
    CustomCheckBox *m_enterSwitch = nullptr;
    CustomCheckBox *m_p2pSwitch = nullptr;
    QPushButton *m_smallButton = nullptr;
    QPushButton *m_normalButton = nullptr;
    QPushButton *m_largeButton = nullptr;
};
