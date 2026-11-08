/**
 * @file   LobbyPublishDialog.h
 * @brief  「发布到联机大厅」对话框：房间名 / 简介 / 封面 / Markdown 介绍 / 前置整合包 / 保存模板
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 实例助手联机页的发布入口用它收集房间资料：
 *   - 封面先传论坛图床（POST /api/upload），发布 body 里只带 /uploads/ 路径；
 *   - 前置整合包二选一：在线整合包（CurseForge/Modrinth 挑选，带下载地址）
 *     或名称 + 网盘链接（纯展示，加入者自己去看）；
 *   - 勾「保存为发布模板」把这套常填项存到本机，下次发布自动填回。
 *
 * 确认后走 validateAndFill() 校验并把结果填回 ForumLobbyRoom，封面若已上传则填 room->cover。
 */
#pragma once

#include "utils/ForumLobbyService.h"

#include <QDialog>
#include <QJsonObject>
#include <QPointer>
#include <QString>

class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QLabel;
class QStackedWidget;
class QPushButton;
class QCheckBox;
class QVBoxLayout;
class QComboBox;

class LobbyPublishDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LobbyPublishDialog(QWidget *parent = nullptr);

    /** 打开时把推荐标题等填进表单（模板在构造里已自动套用） */
    void setDefaultTitle(const QString &title);
    /**
     * @brief 校验 room 名等必填项，全部通过时把结果填回 room
     * @return false 表示有不合法项（内部已弹提示），不要继续发布
     */
    bool validateAndFill(ForumLobbyRoom *room);

    /** 是否勾了「保存为发布模板」；确认后 MultiplayerPage 用它决定写不写模板 */
    bool shouldSaveTemplate() const;

    /** 发布成功后调用：若勾了模板就落盘（cover 用已上传的 url 更新 coverPath） */
    void persistTemplateIfRequested();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void pickCoverImage();
    void removeCoverImage();
    void applyTemplate();
    /** 弹「预设」菜单：把常用介绍模板（生存服 / 建筑服 / 小游戏…）填进 Markdown 介绍 */
    void showMdPresetMenu();

private:
    void buildUi();
    void applyStyles();
    void showCoverPreview(const QImage &image);
    void refreshModpackInfo();
    void packOnlineFromResource(const QString &name, const QString &downloadUrl,
                                const QJsonObject &meta);
    void uploadCoverIfPicked();
    void updateSaveTemplateUi();

    QVBoxLayout *m_layout = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QLineEdit *m_descEdit = nullptr;
    QSpinBox *m_playersSpin = nullptr;

    // 封面行
    QPushButton *m_coverPickBtn = nullptr;
    QPushButton *m_coverRemoveBtn = nullptr;
    QLabel *m_coverPreview = nullptr;
    QString m_coverPath;      ///< 本地选中的封面文件路径（还没上传）
    QString m_coverUrl;       ///< 已上传的 /uploads/xxx（模板里带过来的或上传成功后的）
    bool m_coverDirty = false; ///< 用户动过封面（选了新图或点了移除），决定是否重新上传
    bool m_coverUploadBusy = false;

    // Markdown 介绍
    QPushButton *m_mdPresetBtn = nullptr;
    QPlainTextEdit *m_mdEdit = nullptr;

    // 前置整合包（二选一）
    QLabel *m_packModeHint = nullptr;
    QPushButton *m_packSelectBtn = nullptr;
    QPushButton *m_packClearBtn = nullptr;
    QLineEdit *m_packNameEdit = nullptr;
    QLineEdit *m_packUrlEdit = nullptr;
    QLabel *m_packOnlineInfo = nullptr;
    QString m_packMode;       ///< online / link / 空
    QString m_packMetaJson;   ///< 在线整合包信息快照

    // 模板行
    QCheckBox *m_saveTemplateCheck = nullptr;
    QLabel *m_templateHint = nullptr;
    QPushButton *m_applyTemplateBtn = nullptr;

    ForumLobbyService::PublishTemplate m_template;

    /** 连接对话框内控件与限制（例如 spin 范围），独立成函数避免构造函数里堆太多 */
    void connectChanges();
};
