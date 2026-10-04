/**
 * @file   BedrockCommandAssistantPage.h
 * @brief  基岩版实例助手 - 快捷指令页面：中文 → 基岩版英文指令转换
 * @author BlockBox Team
 * @date   2026-08-25
 *
 * 用于 BedrockInstanceAssistantWindow 的「快捷指令」标签页。
 * 功能与 Java 版 InstanceAssistantWindow 快捷指令一致，但使用基岩版指令数据库。
 */
#ifndef BEDROCKCOMMANDASSISTANTPAGE_H
#define BEDROCKCOMMANDASSISTANTPAGE_H

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include "utils/CommandAssistant/BlockRegistry.h"
#include "utils/CommandAssistant/CommandCompleter.h"
#include "utils/CommandAssistant/CommandDatabase.h"
#include "utils/CommandAssistant/CommandTranslator.h"

class BedrockCommandAssistantPage : public QWidget
{
    Q_OBJECT

public:
    explicit BedrockCommandAssistantPage(QWidget *parent = nullptr);
    ~BedrockCommandAssistantPage() override;

    /** 设置实例路径（基岩版 com.mojang 目录） */
    void setInstancePath(const QString &path);

private slots:
    void onCopyClicked();
    void onInjectClicked();

private:
    void initUI();
    void applyThemeStyles();
    void refreshCompletion();
    void refreshTranslation();
    void flushDebouncedInput();
    void showHistoryDialog();
    void showPresetsMenu();
    void applyPreset(const CommandPreset &preset);
    void addHistory(const QString &input, const QString &output);
    void loadHistory();
    void saveHistory();

    // ---- UI 控件 ----
    QLabel *m_contextLabel;
    QLineEdit *m_commandInput;
    QListWidget *m_completionList;
    QLineEdit *m_englishOutput;
    QPushButton *m_copyBtn;
    QPushButton *m_injectBtn;
    QPushButton *m_historyBtn;
    QPushButton *m_presetBtn;

    // ---- 核心模块 ----
    CommandDatabase *m_commandDb;
    CommandTranslator *m_translator;
    CommandCompleter *m_completer;
    CommandContext m_commandContext;
    QString m_instancePath;

    // ---- 防抖 ----
    QTimer *m_inputDebounceTimer;
    QString m_lastCompletionInput;

    // ---- 历史记录 ----
    QList<QPair<QString, QString>> m_history;
};

#endif // BEDROCKCOMMANDASSISTANTPAGE_H
