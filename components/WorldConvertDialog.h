/**
 * @file   WorldConvertDialog.h
 * @brief  存档版本转换弹窗（Java 版 ⇄ 基岩版，内置 Chunker 引擎，无需插件）
 * @author BlockBox Team
 * @date   2026-10-17
 */
#ifndef WORLDCONVERTDIALOG_H
#define WORLDCONVERTDIALOG_H

#include "components/AppDialogBase.h"
#include "utils/WorldConvert/WorldConverter.h"

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDesktopServices;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTextEdit;

namespace WorldConverter {
class WorldEngineDownloader;
}

/**
 * @brief 存档互转弹窗：选择方向/目标版本后调用内置 Chunker 引擎转换
 *
 * 由实例管理页存档卡片的「版本转化」按钮打开，
 * 输入默认为当前存档目录，输出默认在实例下 converted_worlds/ 文件夹。
 */
class WorldConvertDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @param worldDir   存档目录（含 level.dat）
     * @param worldName  存档显示名
     * @param instancePath 实例路径（用于拼接默认输出目录，可空）
     */
    explicit WorldConvertDialog(const QString &worldDir, const QString &worldName,
                                const QString &instancePath,
                                QWidget *parent = nullptr);
    ~WorldConvertDialog() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onDirectionChanged();
    void onBrowseOutput();
    void onRescanJava();
    void onPickJava();
    void onStartClicked();
    void onCancelClicked();
    void onOpenOutputClicked();
    void onTaskProgress(int percent);
    void onTaskStatus(const QString &text);
    void onTaskLog(const QString &line);
    void onTaskFinished(bool ok, int exitCode, const QString &outputPath,
                        const QString &message);
    void onEngineProgress(qint64 received, qint64 total);
    void onEngineDownloaded(bool ok, const QString &message, const QString &jarPath);

private:
    void initUI();
    void rebuildFormatCombo();
    void updateJavaLabel();
    void setRunning(bool running);
    QString resolvedFormatId() const;
    QString defaultOutputDir() const;
    void appendLog(const QString &text, const QString &colorTag = QString());
    /** 参数校验通过后启动转换（jar 已就位） */
    void startConversion(const QString &jarPath);

    QString m_worldDir;
    QString m_worldName;
    QString m_instancePath;
    QString m_javaExe;
    QString m_lastOutputPath;
    WorldConverter::WorldConvertTask *m_task = nullptr;
    WorldConverter::WorldEngineDownloader *m_engineDownloader = nullptr;

    // UI
    QRadioButton *m_rbJ2B = nullptr;
    QRadioButton *m_rbB2J = nullptr;
    QLineEdit *m_inputEdit = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QCheckBox *m_pkgBox = nullptr;
    QCheckBox *m_emptyBox = nullptr;
    QCheckBox *m_compactBox = nullptr;
    QCheckBox *m_contentBox = nullptr;
    QCheckBox *m_yBlendBox = nullptr;
    QLabel *m_javaLabel = nullptr;
    QPushButton *m_javaRescanBtn = nullptr;
    QPushButton *m_javaPickBtn = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_openOutBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTextEdit *m_logEdit = nullptr;
};

#endif // WORLDCONVERTDIALOG_H
