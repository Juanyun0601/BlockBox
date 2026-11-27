/**
 * @file   MultiThreadDownloadPage.h
 * @brief  多线程下载页 —— 左侧导航「更多 > 多线程下载」的落地页
 * @author BlockBox Team
 * @date   2026-11-27
 *
 * 页面只管「让用户把一条直链尽快下下来」：
 *   - 上面一张卡片填写直链、保存位置、连接数、User-Agent；
 *   - 下面一张卡片显示进度条、速度、剩余时间，以及开始 / 暂停 / 继续 / 取消。
 *
 * 真正的分段并发、断点续传交给 utils/SegmentedDownloader，
 * 页面只在它回报的信号上刷新按钮与文案，不在界面线程里做任何阻塞操作。
 */
#pragma once

#include <QWidget>

#include "utils/SegmentedDownloader.h"

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;

class MultiThreadDownloadPage : public QWidget
{
    Q_OBJECT

public:
    explicit MultiThreadDownloadPage(QWidget *parent = nullptr);
    ~MultiThreadDownloadPage() override;

    /** 进入页面时调用：补上默认保存目录，并刷新一次按钮状态 */
    void refresh();

private:
    void buildUi();

    /** 校验输入并开始一次下载 */
    void startDownload();
    /** 选保存位置（文件名默认取直链里的文件名） */
    void browseSavePath();
    /** 直链变化时把「保存位置」里的文件名跟着换掉（用户手选过就不再动） */
    void syncSuggestedPath();
    /** 按下载器状态刷新状态文案、进度与按钮可用性 */
    void updateStatus();
    /** 按引擎状态决定输入区能不能改 */
    void updateInputsEnabled();
    /** 打开下载文件所在文件夹 */
    void openDownloadFolder();
    /** 现在能打开的文件路径：优先成品，其次没下完的 *.part，都没有返回空 */
    QString existingDownloadPath() const;
    QString defaultDownloadDir() const;

    SegmentedDownloader *m_downloader = nullptr;

    QLineEdit *m_urlEdit = nullptr;
    QLineEdit *m_saveEdit = nullptr;
    QLineEdit *m_userAgentEdit = nullptr;
    QSpinBox *m_threadSpin = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_detailLabel = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_pauseBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_openDirBtn = nullptr;

    bool m_autoSavePath = true;   ///< 保存路径是否还跟着直链自动生成
    bool m_rangeSupported = true; ///< 服务器是否支持分段（决定能不能暂停）
    qint64 m_lastReceived = 0;
    qint64 m_lastTotal = -1;
    qint64 m_lastSpeed = 0;
    int m_lastEta = -1;
    QString m_finishedPath;       ///< 下载完成的文件路径（「打开所在文件夹」用）
};
