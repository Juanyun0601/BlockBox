#ifndef ONLINERESOURCEDIALOG_H
#define ONLINERESOURCEDIALOG_H

#include "AppDialogBase.h"

#include "utils/mod/ModData.h"

#include <QColor>
#include <QPixmap>
#include <QStringList>

class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QToolButton;

class CurseForgeAPI;
class ModrinthAPI;

/**
 * @brief 在线资源挑选弹窗（CurseForge / Modrinth）
 *
 * 按「版本平台 → 资源类型 → 搜索资源 → 选择版本」四步引导，
 * 从 CurseForge 或 Modrinth 挑选 Java 版 / 基岩版资源，
 * 确认后返回所选版本的下载地址。
 */
class OnlineResourceDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 选中的网络资源
     */
    struct SelectedResource {
        QString edition;      // "java" / "bedrock"
        QString platform;     // "curseforge" / "modrinth"
        QString resourceType; // 模组 / 资源包 / ...
        QString name;
        QString author;
        QString version;      // 版本显示名
        QString fileName;     // 下载文件名
        QString downloadUrl;  // 实际下载地址
        qint64 fileSize = 0;
        QStringList gameVersions;
        QStringList loaders;
        QString pageUrl;

        bool valid() const { return !downloadUrl.isEmpty(); }
    };

    /**
     * @brief 从 CurseForge / Modrinth 选择资源（Java 版 / 基岩版），返回所选版本下载地址
     * @param out 非空时回填所选资源完整信息
     */
    static QString getResource(QWidget *parent, const QString &caption,
                               SelectedResource *out = nullptr);

    /** @brief 返回确认后的结果（所选版本的下载地址） */
    QString resultPath() const { return m_result; }

    explicit OnlineResourceDialog(QWidget *parent);

private:
    enum class ResLevel { Edition, Type, Search, Version };

    // 页面构建
    void buildResStepper();
    QWidget *buildResPages();
    QWidget *buildResEditionPage();
    QWidget *buildResTypePage();
    QWidget *buildResSearchPage();
    QWidget *buildResVersionPage();
    QWidget *buildFooter();

    // 流程逻辑
    void setupResApis();
    void applyMcimToResApis();
    void setResLevel(ResLevel lv);
    void renderResStepper();
    void renderResPages();
    void resChooseEditionPlatform(const QString &edition, const QString &platform);
    void populateResTypes();
    void performResSearch(int page);
    void onResSearchCompleted(const ModSearchResult &result);
    void onResSearchFailed(const QString &error);
    void onResDetailReceived(const ModInfo &detail);
    void onResDetailFailed(const QString &error);
    void openResVersions(int idx);
    void populateResVersionList();
    void buildResSelected();

    // 通用
    void updateFooterState();
    void acceptResult();
    void initStyle();
    static QPixmap makeTileIcon(const QString &letter, const QColor &color, int size);

    // 状态
    bool m_mobileMode = false;   ///< 移动端（安卓/鸿蒙）：触屏布局
    ResLevel m_resLevel = ResLevel::Edition;
    bool m_resEditionChosen = false;
    bool m_resEditionIsJava = true;
    QString m_resPlatform;    // "curseforge" / "modrinth"
    QString m_resTypeCf;      // 当前资源类型的 CurseForge classId
    QString m_resTypeMr;      // 当前资源类型的 Modrinth project_type
    QString m_resTypeLabel;   // 显示名（模组 / 资源包…）
    QString m_resQuery;
    int m_resPage = 0;
    bool m_resLoading = false;
    QList<ModInfo> m_resResults;
    ModInfo m_resInfo;
    ModVersionFile m_resVersion;
    SelectedResource m_resSelected;
    CurseForgeAPI *m_cfApi = nullptr;
    ModrinthAPI *m_mrApi = nullptr;

    // 控件
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QToolButton *m_closeBtn = nullptr;

    QFrame *m_resStepper = nullptr;
    QHBoxLayout *m_resStepperLayout = nullptr;

    QStackedWidget *m_resStack = nullptr;
    QWidget *m_resEditionPage = nullptr;
    QWidget *m_resTypePage = nullptr;
    QWidget *m_resSearchPage = nullptr;
    QWidget *m_resVersionPage = nullptr;
    QListWidget *m_resEditionList = nullptr;
    QListWidget *m_resTypeList = nullptr;
    QLineEdit *m_resSearchEdit = nullptr;
    QPushButton *m_resSearchBtn = nullptr;
    QPushButton *m_resPrevBtn = nullptr;
    QPushButton *m_resNextBtn = nullptr;
    QLabel *m_resPageLabel = nullptr;
    QLabel *m_resStatusLabel = nullptr;
    QListWidget *m_resResultList = nullptr;
    QLabel *m_resVersionHeader = nullptr;
    QListWidget *m_resVersionList = nullptr;

    QLabel *m_statusLabel = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_okBtn = nullptr;

    QString m_result;
};

#endif // ONLINERESOURCEDIALOG_H
