/**
 * @file   ProjectionEditPage.h
 * @brief  投影编辑页面
 * @author BlockBox Team
 * @date   2026-08-02
 *
 * 展示并编辑单个 .litematic 投影文件：
 *   - 文件信息：名称、路径、大小、修改时间、格式版本、数据版本
 *   - 区域信息：各区域坐标、尺寸、体积、实际方块数
 *   - 材料清单：聚合各区域方块统计（按数量降序）
 *   - 元数据编辑：名称 / 作者 / 描述（通过 LitematicEditor 写回）
 *   - 格式转换：导出为 .schematic（Schematica）/ .schem（Sponge）
 *
 * 本页嵌入实例管理页（InstanceManagePage）的内容栈中，通过
 * setCurrentProjection() 指定要编辑的投影文件。返回与标题复用主界面
 * TopBar（由 MainWindow 在进入本页时设置）。
 */

#pragma once

#include <QWidget>
#include <QString>

#include "utils/CommandAssistant/LitematicReader.h"

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QTextEdit;
class QPushButton;
class QTreeWidget;
class QComboBox;
class QVBoxLayout;

/**
 * @brief 投影编辑页面
 */
class ProjectionEditPage : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectionEditPage(QWidget *parent = nullptr);
    ~ProjectionEditPage() override;

    /**
     * @brief 指定要编辑的投影文件并重新加载信息
     * @param filePath .litematic 文件绝对路径
     */
    void setCurrentProjection(const QString &filePath);

    /** 重置为未加载状态（实例切换时调用） */
    void reset();

signals:
    /** 元数据已修改保存（供列表页刷新） */
    void projectionEdited(const QString &filePath);

private slots:
    void onSaveMetadataClicked();
    void onExportSchematicClicked();
    void onExportSpongeClicked();
    void onOpenFolderClicked();
    void onConvertVersionClicked();
    void onVersionComboChanged(int index);

private:
    void initUI();
    void loadProjection();
    void populateRegions();
    void populateMaterials();
    void resizeTreeToContent(QTreeWidget *tree);
    QString formatFileSize(qint64 size) const;
    QString formatTimestamp(qint64 msecs) const;

    // 设置卡片与设置行辅助（与设置页样式对齐）
    QVBoxLayout *createSettingsCard(QVBoxLayout *parentLayout,
                                    const QString &title = QString(),
                                    bool startCollapsed = false);
    QHBoxLayout *appendSettingRow(QVBoxLayout *cardLayout,
                                  const QString &title,
                                  const QString &desc = QString(),
                                  bool isLast = false);

    QString m_filePath;         ///< 投影文件绝对路径
    bool m_loaded = false;      ///< 是否成功解析
    LitematicInfo m_info;       ///< 已读取的投影信息

    // 顶部
    QLabel *m_titleLabel;

    // 信息区
    QLabel *m_nameLabel;
    QLabel *m_pathLabel;
    QLabel *m_sizeLabel;
    QLabel *m_modifiedLabel;
    QLabel *m_versionLabel;
    QLabel *m_dataVersionLabel;
    QLabel *m_statsLabel;

    // 区域信息
    QTreeWidget *m_regionTree;

    // 材料清单
    QTreeWidget *m_materialTree;
    QLabel *m_materialSummary;

    // 编辑区
    QLineEdit *m_nameEdit;
    QLineEdit *m_authorEdit;
    QTextEdit *m_descEdit;

    // 底部按钮
    QPushButton *m_saveBtn;
    QPushButton *m_openFolderBtn;
    QPushButton *m_exportSchematicBtn;
    QPushButton *m_exportSpongeBtn;

    // 版本转换
    QLabel *m_currentVersionLabel;
    QComboBox *m_targetVersionCombo;
    QLabel *m_targetVersionHint;
    QPushButton *m_convertVersionBtn;
};
