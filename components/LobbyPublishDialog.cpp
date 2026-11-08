/**
 * @file   LobbyPublishDialog.cpp
 * @brief  「发布到联机大厅」对话框实现
 * @author BlockBox Team
 * @date   2026-11-07
 */

#include "LobbyPublishDialog.h"

#include "AppMessageBox.h"
#include "OnlineResourceDialog.h"
#include "ForumKit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <utils/ThemeManager.h>
#include <utils/StyleKit.h>

namespace {

QJsonObject metaFromResource(const OnlineResourceDialog::SelectedResource &res)
{
    QJsonObject meta;
    meta.insert(QStringLiteral("platform"), res.platform);
    if (!res.author.isEmpty())
        meta.insert(QStringLiteral("author"), res.author);
    if (!res.version.isEmpty())
        meta.insert(QStringLiteral("version"), res.version);
    if (!res.fileName.isEmpty())
        meta.insert(QStringLiteral("file_name"), res.fileName);
    if (res.fileSize > 0)
        meta.insert(QStringLiteral("file_size"), double(res.fileSize));
    if (!res.pageUrl.isEmpty())
        meta.insert(QStringLiteral("page_url"), res.pageUrl);
    return meta;
}

} // namespace

LobbyPublishDialog::LobbyPublishDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("发布到联机大厅"));
    setMinimumWidth(430);

    m_template = ForumLobbyService::loadPublishTemplate();
    buildUi();

    // 有模板就自动填回上次的发布设置：文案 / 人数 / 封面 / 整合包一次到位
    if (!m_template.isEmpty())
    {
        applyTemplate();
    }
}

void LobbyPublishDialog::buildUi()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *body = new QWidget(scroll);
    m_layout = new QVBoxLayout(body);
    m_layout->setContentsMargins(16, 14, 16, 14);
    m_layout->setSpacing(10);

    // ── 房间名 / 简介 / 人数 ──
    auto *form = new QFormLayout();
    form->setSpacing(8);
    form->setLabelAlignment(Qt::AlignRight);

    m_titleEdit = new QLineEdit(body);
    m_titleEdit->setMaxLength(40);
    form->addRow(tr("房间名"), m_titleEdit);

    m_descEdit = new QLineEdit(body);
    m_descEdit->setMaxLength(200);
    m_descEdit->setPlaceholderText(tr("一句话介绍，可不填"));
    form->addRow(tr("简介"), m_descEdit);

    m_playersSpin = new QSpinBox(body);
    m_playersSpin->setRange(1, 64);
    m_playersSpin->setValue(8);
    form->addRow(tr("人数上限"), m_playersSpin);

    m_layout->addLayout(form);

    // ── 封面图 ──
    auto *coverRow = new QHBoxLayout();
    coverRow->setSpacing(10);
    m_coverPreview = new QLabel(body);
    m_coverPreview->setObjectName(QStringLiteral("lpCoverPreview"));
    m_coverPreview->setFixedSize(160, 90);
    m_coverPreview->setAlignment(Qt::AlignCenter);
    m_coverPreview->setText(tr("16:9 封面图\n可不设"));
    coverRow->addWidget(m_coverPreview);

    auto *coverBtns = new QVBoxLayout();
    coverBtns->setSpacing(6);
    m_coverPickBtn = new QPushButton(tr("选择图片…"), body);
    m_coverRemoveBtn = new QPushButton(tr("移除封面"), body);
    m_coverRemoveBtn->setEnabled(false);
    coverBtns->addWidget(m_coverPickBtn);
    coverBtns->addWidget(m_coverRemoveBtn);
    coverBtns->addStretch();
    coverRow->addLayout(coverBtns, 1);
    m_layout->addLayout(coverRow);

    // ── Markdown 介绍 ──
    auto *mdHeaderRow = new QHBoxLayout();
    mdHeaderRow->setSpacing(8);
    auto *mdTitle = new QLabel(tr("详细介绍（支持 Markdown，网页端会渲染）"), body);
    mdTitle->setObjectName(QStringLiteral("lpSectionTitle"));
    mdHeaderRow->addWidget(mdTitle);
    mdHeaderRow->addStretch(1);
    m_mdPresetBtn = new QPushButton(tr("预设"), body);
    m_mdPresetBtn->setCursor(Qt::PointingHandCursor);
    m_mdPresetBtn->setFixedHeight(24);
    m_mdPresetBtn->setToolTip(tr("插入常用的介绍模板，例如生存服、建筑服、小游戏"));
    connect(m_mdPresetBtn, &QPushButton::clicked, this, &LobbyPublishDialog::showMdPresetMenu);
    mdHeaderRow->addWidget(m_mdPresetBtn);
    m_layout->addLayout(mdHeaderRow);
    m_mdEdit = new QPlainTextEdit(body);
    m_mdEdit->setObjectName(QStringLiteral("lpMdEdit"));
    m_mdEdit->setPlaceholderText(tr("玩法说明、注意事项、模组介绍…\n支持 **加粗**、`代码`、> 引用、![](图片链接) 等语法，可不填"));
    m_mdEdit->setFixedHeight(110);
    m_layout->addWidget(m_mdEdit);

    // ── 前置整合包 ──
    auto *packTitle = new QLabel(tr("前置整合包（需要装的整合包，可不选）"), body);
    packTitle->setObjectName(QStringLiteral("lpSectionTitle"));
    m_layout->addWidget(packTitle);

    auto *packPickRow = new QHBoxLayout();
    packPickRow->setSpacing(8);
    m_packSelectBtn = new QPushButton(tr("从在线资源挑选…"), body);
    m_packClearBtn = new QPushButton(tr("清除"), body);
    m_packClearBtn->setEnabled(false);
    packPickRow->addWidget(m_packSelectBtn, 1);
    packPickRow->addWidget(m_packClearBtn);
    m_layout->addLayout(packPickRow);

    m_packOnlineInfo = new QLabel(body);
    m_packOnlineInfo->setObjectName(QStringLiteral("lpPackInfo"));
    m_packOnlineInfo->setWordWrap(true);
    m_packOnlineInfo->hide();
    m_layout->addWidget(m_packOnlineInfo);

    auto *packNameRow = new QHBoxLayout();
    packNameRow->setSpacing(8);
    auto *packNameTitle = new QLabel(tr("或手填："), body);
    packNameTitle->setObjectName(QStringLiteral("lpPackInfo"));
    m_packNameEdit = new QLineEdit(body);
    m_packNameEdit->setMaxLength(200);
    m_packNameEdit->setPlaceholderText(tr("整合包名称"));
    m_packUrlEdit = new QLineEdit(body);
    m_packUrlEdit->setMaxLength(500);
    m_packUrlEdit->setPlaceholderText(tr("网盘 / 下载链接（http:// 或 https://）"));
    packNameRow->addWidget(packNameTitle);
    packNameRow->addWidget(m_packNameEdit, 1);
    packNameRow->addWidget(m_packUrlEdit, 2);
    m_layout->addLayout(packNameRow);

    m_packModeHint = new QLabel(body);
    m_packModeHint->setObjectName(QStringLiteral("lpPackInfo"));
    m_packModeHint->setWordWrap(true);
    m_layout->addWidget(m_packModeHint);

    // ── 模板行 ──
    auto *tplRow = new QHBoxLayout();
    tplRow->setSpacing(8);
    m_saveTemplateCheck = new QCheckBox(tr("把这次的发布设置存为模板"), body);
    m_saveTemplateCheck->setChecked(true);
    tplRow->addWidget(m_saveTemplateCheck);
    tplRow->addStretch();
    m_templateHint = new QLabel(body);
    m_templateHint->setObjectName(QStringLiteral("lpPackInfo"));
    tplRow->addWidget(m_templateHint);
    m_layout->addLayout(tplRow);

    // ── 底部按钮 ──
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, body);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("发布"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_layout->addWidget(buttons);

    scroll->setWidget(body);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    connectChanges();
    applyStyles();
}

void LobbyPublishDialog::connectChanges()
{
    connect(m_coverPickBtn, &QPushButton::clicked, this, &LobbyPublishDialog::pickCoverImage);
    connect(m_coverRemoveBtn, &QPushButton::clicked, this, &LobbyPublishDialog::removeCoverImage);
    connect(m_packSelectBtn, &QPushButton::clicked, this, [this]() {
        OnlineResourceDialog::SelectedResource res;
        // 弹在线资源选择器：用户挑一个资源，发布时按整合包挂到房间上
        OnlineResourceDialog::getResource(this, tr("选择在线整合包"), &res);
        if (!res.valid())
            return;
        if (!res.resourceType.isEmpty() && res.resourceType != tr("整合包"))
        {
            AppMessageBox::warning(this, tr("选的不是整合包"),
                                   tr("前置要选「整合包」类型的资源，房间上才会标它是前置。"));
        }
        packOnlineFromResource(res.name, res.downloadUrl, metaFromResource(res));
    });
    connect(m_packClearBtn, &QPushButton::clicked, this, [this]() {
        m_packMode.clear();
        m_packMetaJson.clear();
        m_packNameEdit->clear();
        m_packUrlEdit->clear();
        m_packOnlineInfo->hide();
        m_packClearBtn->setEnabled(false);
        m_packModeHint->clear();
    });
    // 手填框一改就当作 link 模式（在线快照的名称/地址也走这两个框，改 lead 同样按 link 算）
    connect(m_packNameEdit, &QLineEdit::textEdited, this, [this]() {
        if (m_packMode.isEmpty() && !m_packNameEdit->text().trimmed().isEmpty())
            m_packMode = QStringLiteral("link");
        refreshModpackInfo();
    });
    connect(m_packUrlEdit, &QLineEdit::textEdited, this, [this]() {
        if (m_packMode.isEmpty() && !m_packUrlEdit->text().trimmed().isEmpty())
            m_packMode = QStringLiteral("link");
        refreshModpackInfo();
    });
    connect(m_saveTemplateCheck, &QCheckBox::toggled, this,
            &LobbyPublishDialog::updateSaveTemplateUi);
}

void LobbyPublishDialog::showMdPresetMenu()
{
    if (!m_mdPresetBtn || !m_mdEdit)
        return;

    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (const ForumKit::MarkdownPreset &preset : ForumKit::lobbyIntroPresets())
    {
        menu->addAction(preset.label, this, [this, text = preset.text]() {
            ForumKit::applyMarkdownPreset(m_mdEdit, text);
        });
    }
    menu->popup(m_mdPresetBtn->mapToGlobal(QPoint(0, m_mdPresetBtn->height() + 2)));
}

void LobbyPublishDialog::applyStyles()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString textColor  = isLight ? "#1f2937" : "#e8e8e8";
    const QString subColor   = isLight ? "#64748B" : "#9aa3b2";
    const QString fieldBorder= isLight ? "#d4d4d4" : "#55555a";
    const QString fieldBg    = isLight ? "#f4f4f4" : "#3b3b40";

    setStyleSheet(QString(
        "QDialog { background: palette(window); }"
        "QLabel#lpSectionTitle { color: %1; font-size: 12px; font-weight: 600;"
        "  background: transparent; }"
        "QLabel#lpPackInfo { color: %2; font-size: 11px; background: transparent; }"
        "QLabel#lpCoverPreview {"
        "  color: %2; font-size: 11px; background: %3;"
        "  border: 1px dashed %4; border-radius: 8px;"
        "}"
        "QPlainTextEdit#lpMdEdit {"
        "  background: %3; border: 1px solid %4; border-radius: 8px;"
        "  color: %5; font-size: 12px; padding: 6px;"
        "}"
        "QPushButton { border-radius: 6px; padding: 4px 12px; font-size: 12px;"
        "  background: %3; color: %5; border: 1px solid %4; }"
        "QPushButton:hover { border-color: %1; color: %1; }"
        "QPushButton:disabled { color: %2; border-color: %4; }"
        " QLineEdit { background: %3; border: 1px solid %4; border-radius: 8px;"
        "  color: %5; padding: 4px 8px; font-size: 12px; }"
        "QSpinBox { background: %3; border: 1px solid %4; border-radius: 8px;"
        "  color: %5; padding: 3px 8px; font-size: 12px; }"
        "QCheckBox { color: %5; font-size: 12px; }"
        "QScrollArea { background: transparent; border: none; }"
        "QWidget > QScrollBar:vertical { background: transparent; }"
    ).arg(themeColor, subColor, fieldBg, fieldBorder, textColor));
}

void LobbyPublishDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    m_titleEdit->setFocus();
}

void LobbyPublishDialog::setDefaultTitle(const QString &title)
{
    if (m_titleEdit->text().trimmed().isEmpty())
        m_titleEdit->setText(title);
}

void LobbyPublishDialog::pickCoverImage()
{
    // 系统文件选择器
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择封面图"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.webp *.gif);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull())
    {
        AppMessageBox::warning(this, tr("打不开这张图"), tr("换一张 PNG / JPG 图片试试。"));
        return;
    }
    m_coverPath = path;
    m_coverDirty = true;
    m_coverRemoveBtn->setEnabled(true);
    showCoverPreview(image);
}

void LobbyPublishDialog::removeCoverImage()
{
    m_coverPath.clear();
    m_coverUrl.clear();
    m_coverDirty = true;
    m_coverRemoveBtn->setEnabled(false);
    m_coverPreview->setPixmap(QPixmap());
    m_coverPreview->setText(tr("16:9 封面图\n可不设"));
}

void LobbyPublishDialog::showCoverPreview(const QImage &image)
{
    const QPixmap pixmap = QPixmap::fromImage(
        image.scaled(m_coverPreview->size(), Qt::KeepAspectRatioByExpanding,
                     Qt::SmoothTransformation));
    m_coverPreview->setPixmap(pixmap.copy(0, 0, m_coverPreview->width(), m_coverPreview->height()));
}

void LobbyPublishDialog::packOnlineFromResource(const QString &name, const QString &downloadUrl,
                                                const QJsonObject &meta)
{
    m_packMode = QStringLiteral("online");
    m_packNameEdit->setText(name);
    m_packUrlEdit->setText(downloadUrl);
    m_packMetaJson = QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact));
    refreshModpackInfo();
}

void LobbyPublishDialog::refreshModpackInfo()
{
    // 两个框都空且没选过在线资源 = 没设前置
    const bool hasContent = !m_packMode.isEmpty()
                            || !m_packNameEdit->text().trimmed().isEmpty()
                            || !m_packUrlEdit->text().trimmed().isEmpty();
    if (!hasContent)
    {
        m_packModeHint->setText(tr("其他玩家加入前会收到提示，告诉他们先装这个整合包。"));
        m_packOnlineInfo->hide();
        m_packClearBtn->setEnabled(false);
        return;
    }

    m_packClearBtn->setEnabled(true);
    if (m_packMode == QLatin1String("online"))
    {
        const QString version = QJsonDocument::fromJson(m_packMetaJson.toUtf8())
                                    .object()
                                    .value(QStringLiteral("version"))
                                    .toString();
        const QString name = m_packNameEdit->text().trimmed().isEmpty()
                                 ? tr("（还没选）")
                                 : m_packNameEdit->text().trimmed();
        m_packOnlineInfo->setText(tr("已选在线整合包：%1%2\n加入者可以直接从软件里下载安装")
                                      .arg(name,
                                           version.isEmpty() ? QString()
                                                             : QStringLiteral(" · %1").arg(version)));
        m_packOnlineInfo->show();
        m_packModeHint->setText(tr("框里就是它的名称和下载地址，改成网盘链接也行（会按链接模式发布）。"));
    }
    else
    {
        m_packOnlineInfo->hide();
        m_packModeHint->setText(tr("按名称 + 链接发布：加入者会看到整合包名和链接，自己去下载。"));
    }
}

void LobbyPublishDialog::uploadCoverIfPicked()
{
    if (m_coverPath.isEmpty())
        return;
    if (m_coverUploadBusy)
        return;
    m_coverUploadBusy = true;
    m_coverPickBtn->setEnabled(false);
    ForumLobbyService::instance()->uploadImage(
        m_coverPath,
        [this](const QString &url, const QString &message) {
            m_coverUploadBusy = false;
            m_coverPickBtn->setEnabled(true);
            if (url.isEmpty())
            {
                AppMessageBox::warning(this, tr("封面上传失败"),
                                       message.isEmpty() ? tr("封面没传上去，这次先不带封面发布。")
                                                         : message);
                m_coverUrl.clear();
                return;
            }
            m_coverUrl = url;
        });
}

void LobbyPublishDialog::updateSaveTemplateUi()
{
    if (m_saveTemplateCheck->isChecked())
        m_templateHint->setText(tr("下次发布直接用这套设置"));
    else if (!m_template.isEmpty())
        m_templateHint->setText(tr("模板还在，本次照旧填进表单里"));
    else
        m_templateHint->clear();
}

bool LobbyPublishDialog::shouldSaveTemplate() const
{
    return m_saveTemplateCheck && m_saveTemplateCheck->isChecked();
}

void LobbyPublishDialog::applyTemplate()
{
    m_titleEdit->setText(m_template.title);
    m_descEdit->setText(m_template.description);
    m_playersSpin->setValue(m_template.maxPlayers > 0 ? m_template.maxPlayers : 8);
    m_mdEdit->setPlainText(m_template.md);

    m_coverPath = QFileInfo(m_template.coverPath).exists() ? m_template.coverPath : QString();
    m_coverUrl = m_template.cover;
    m_coverDirty = false;
    m_coverRemoveBtn->setEnabled(!m_coverPath.isEmpty() || !m_coverUrl.isEmpty());
    if (!m_coverPath.isEmpty())
    {
        QImageReader reader(m_coverPath);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (!image.isNull())
            showCoverPreview(image);
        else
        {
            // 本地文件失效就只留已上传的路径，界面回到占位文案
            m_coverPath.clear();
            m_coverPreview->setText(tr("已用上次封面\n（可重新选择）"));
        }
    }
    else if (!m_coverUrl.isEmpty())
    {
        m_coverPreview->setText(tr("已用上次封面\n（可重新选择）"));
    }

    m_packMode = m_template.modpackMode;
    m_packMetaJson = m_template.modpackMeta;
    m_packNameEdit->setText(m_template.modpackName);
    m_packUrlEdit->setText(m_template.modpackUrl);
    refreshModpackInfo();
    updateSaveTemplateUi();
}

bool LobbyPublishDialog::validateAndFill(ForumLobbyRoom *room)
{
    if (!room)
        return false;

    const QString title = m_titleEdit->text().trimmed();
    if (title.size() < 2)
    {
        AppMessageBox::warning(this, tr("房间名太短"), tr("房间名至少需要 2 个字。"));
        return false;
    }

    // 封面：新选了图先把本地上传压进去（同步等待论坛返回）
    if (m_coverDirty && !m_coverPath.isEmpty())
    {
        QString uploaded;
        QMetaObject::invokeMethod(
            this,
            [this, &uploaded]() {
                m_coverUploadBusy = true;
                ForumLobbyService::instance()->uploadImage(
                    m_coverPath,
                    [this, &uploaded](const QString &url, const QString &message) {
                        uploaded = url;
                        m_coverUploadBusy = false;
                        if (url.isEmpty())
                            AppMessageBox::warning(
                                this, tr("封面没传上去"),
                                message.isEmpty()
                                    ? tr("这次先不带封面发布，回头可再编辑。")
                                    : message);
                    });
            },
            Qt::BlockingQueuedConnection);
        m_coverUrl = uploaded;
    }
    else if (m_coverDirty && m_coverPath.isEmpty())
    {
        m_coverUrl.clear();
    }

    room->title = title;
    room->description = m_descEdit->text().trimmed();
    room->maxPlayers = m_playersSpin->value();
    room->cover = m_coverUrl;
    room->md = m_mdEdit->toPlainText().trimmed();
    room->modpackMode.clear();
    room->modpackName.clear();
    room->modpackUrl.clear();
    room->modpackMeta.clear();
    if (!m_packMode.isEmpty())
    {
        QString name;
        QString url;
        if (m_packMode == QLatin1String("online"))
        {
            name = m_packNameEdit->text().trimmed();
            url = m_packUrlEdit->text().trimmed();
        }
        else
        {
            name = m_packNameEdit->text().trimmed();
            url = m_packUrlEdit->text().trimmed();
        }
        if (name.isEmpty())
        {
            AppMessageBox::warning(this, tr("整合包没填名"),
                                   tr("前置整合包需要至少填个名字，清掉或补全后再发布。"));
            return false;
        }
        room->modpackMode = m_packMode;
        room->modpackName = name;
        room->modpackUrl = url;
        room->modpackMeta = m_packMetaJson;
    }
    return true;
}

void LobbyPublishDialog::persistTemplateIfRequested()
{
    if (!m_saveTemplateCheck || !m_saveTemplateCheck->isChecked())
        return;

    ForumLobbyService::PublishTemplate tpl;
    tpl.title = m_titleEdit->text().trimmed();
    tpl.description = m_descEdit->text().trimmed();
    tpl.maxPlayers = m_playersSpin->value();
    tpl.cover = m_coverUrl;
    tpl.coverPath = m_coverPath;
    tpl.md = m_mdEdit->toPlainText().trimmed();
    tpl.modpackMode = m_packMode;
    tpl.modpackName = m_packNameEdit->text().trimmed();
    tpl.modpackUrl = m_packUrlEdit->text().trimmed();
    tpl.modpackMeta = m_packMetaJson;
    ForumLobbyService::savePublishTemplate(tpl);
}
