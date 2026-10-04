/**
 * @file   VoiceSettingsDialog.h
 * @brief  语音识别服务配置对话框
 * @author BlockBox Team
 * @date   2026-09-12
 */

#ifndef VOICESETTINGSDIALOG_H
#define VOICESETTINGSDIALOG_H

#include "AppDialogBase.h"

#include "components/VoiceInputController.h"

class QCheckBox;
class QLineEdit;

/**
 * @brief 语音识别服务配置弹窗（居中圆角模糊卡片）
 *
 * 配置 OpenAI 兼容的转写端点（/v1/audio/transcriptions）：
 * API 地址、模型 ID 与 API Key。兼容 OpenAI、SiliconFlow、Groq、
 * 本地 whisper.cpp / faster-whisper-server 等同一协议的服务。
 */
class VoiceSettingsDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 弹出配置对话框
     * @param parent 宿主窗口
     * @param config 传入当前配置作为初始值，确认后被更新为用户填写的结果
     * @return 用户点击「保存」返回 true，取消返回 false
     */
    static bool configure(QWidget *parent, VoiceInputController::Config &config);

private:
    explicit VoiceSettingsDialog(QWidget *parent, const VoiceInputController::Config &config);

    void initUI();
    void initStyle();
    void onOk();

    VoiceInputController::Config m_config;
    bool m_accepted = false;

    QLineEdit *m_apiUrlEdit = nullptr;
    QLineEdit *m_modelEdit = nullptr;
    QLineEdit *m_keyEdit = nullptr;
    QCheckBox *m_showKeyCheck = nullptr;
};

#endif // VOICESETTINGSDIALOG_H
