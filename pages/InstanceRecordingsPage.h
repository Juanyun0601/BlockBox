/**
 * @file   InstanceRecordingsPage.h
 * @brief  实例管理 - 录像管理页面（Replay Mod / Flashback 录像列表）
 * @author BlockBox Team
 * @date   2026-10-31
 */
#ifndef INSTANCERECORDINGSPAGE_H
#define INSTANCERECORDINGSPAGE_H

#include <QWidget>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

class QMouseEvent;

class InstanceRecordingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit InstanceRecordingsPage(QWidget *parent = nullptr);
    ~InstanceRecordingsPage() override;

    void setInstancePath(const QString &path);
    void refreshFileList();

    /** 子导航搜索框分发：同步到页内搜索框（复用既有过滤管线） */
    Q_INVOKABLE void applyPageFilter(const QString &query);

signals:
    /**
     * 空状态「快速安装录像模组」：请求安装录像模组
     * @param modKey 需要安装的模组标识（replaymod / flashback），主窗口按实例加载器决定
     */
    void quickInstallModRequested(const QString &modKey);

protected:
    /** 卡片双击：打开录像所在文件夹 */
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onSearchTextChanged(const QString &text);
    void onSourceFilterChanged(int index);
    void onTimeFilterChanged(int index);
    void onSizeFilterChanged(int index);
    void onSortChanged(int index);

private:
    /** 录像来源 */
    enum Source
    {
        ReplayMod,    // Replay Mod - replay_recordings/*.mcpr
        Flashback     // Flashback  - flashback/replays/*.zip
    };

    /** 一条录像的展示信息 */
    struct RecordingEntry
    {
        QString filePath;
        QString fileName;
        qint64 fileSize = 0;
        QDateTime lastModified;
        int source = ReplayMod;
        QString title;      // 展示标题（优先用模组内记录的名字）
        QString worldLine;  // 服务器 / 世界信息
        qint64 durationMs = -1;   // 时长（毫秒），未知为 -1
        QPixmap icon;       // 录像内置预览图，可能为空
    };

    void initUI();
    void scanFiles();
    void rebuildCards();
    void clearCards();
    void updateStats();
    QWidget *createRecordingCard(const RecordingEntry &entry);
    /** 从录像文件（zip 容器）里读元数据与预览图，填充 entry */
    void loadRecordingMeta(RecordingEntry &entry) const;
    /** 从 zip 内提取第一张 PNG 预览图 */
    QPixmap extractPreviewIcon(const QString &filePath) const;
    /** 读取 Replay Mod 的 replay.meta.txt / metaData.json */
    void parseReplayModMeta(RecordingEntry &entry, const QByteArray &metaTxt,
                            const QByteArray &metaJson) const;
    /** 读取 Flashback 的 metadata.json */
    void parseFlashbackMeta(RecordingEntry &entry, const QByteArray &metaJson) const;
    QString formatFileSize(qint64 bytes) const;
    QString formatDuration(qint64 ms) const;
    /** 指定来源对应的录像目录（绝对路径） */
    QString recordingsDir(int source) const;
    /** 当前来源筛选对应的录像目录（全部来源时返回实例根目录） */
    QString currentRecordingsDir() const;
    /** 当前是否处于任一筛选状态（来源/搜索/时间/大小） */
    bool anyFilterActive() const;
    /**
     * @brief 空状态提示与「快速安装录像模组」按钮的统一入口
     * @param text         提示文字
     * @param allowInstall 是否显示快速安装按钮（按当前来源筛选自适应）
     */
    void showEmptyState(const QString &text, bool allowInstall);

    // 筛选状态：0=全部 1=Replay Mod 2=Flashback
    int m_sourceFilter = 0;
    int m_timeFilter = 0;   // 0=全部 1=7 天 2=30 天 3=一年
    int m_sizeFilter = 0;   // 0=全部 1=<10MB 2=10~100MB 3=>100MB
    int m_sortMode = 4;     // 0=名称↑ 1=名称↓ 2=大小↓ 3=大小↑ 4=新→旧 5=旧→新（默认时间新→旧）

    // UI
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_statsLabel = nullptr;
    QComboBox *m_sourceCombo = nullptr;
    QComboBox *m_timeCombo = nullptr;
    QComboBox *m_sizeCombo = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_cardContainer = nullptr;
    QVBoxLayout *m_cardListLayout = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QWidget *m_emptyWidget = nullptr;          ///< 空状态容器（提示 + 安装按钮）
    QPushButton *m_installReplayBtn = nullptr; ///< 空状态「快速安装 Replay Mod」
    QPushButton *m_installFlashbackBtn = nullptr; ///< 空状态「快速安装 Flashback」
    QWidget *m_bottomBar = nullptr;
    QPushButton *m_openFolderBtn = nullptr;

    // 数据
    QList<RecordingEntry> m_entries;
    int m_visibleCount = 0;   // 当前筛选+排序后实际展示的条数
    QString m_currentSearch;
    QString m_instancePath;
};

#endif // INSTANCERECORDINGSPAGE_H
