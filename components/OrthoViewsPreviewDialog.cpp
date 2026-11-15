/**
 * @file   OrthoViewsPreviewDialog.cpp
 * @brief  投影三视图预览与保存对话框实现
 * @author BlockBox Team
 * @date   2026-11-14
 */

#include "OrthoViewsPreviewDialog.h"

#include "AppMessageBox.h"
#include "utils/StyleKit.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPair>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QList>

namespace {

/** 预览图的最大显示尺寸（等比缩放放下，原图始终全分辨率保存） */
constexpr int kPreviewMaxWidth = 320;
constexpr int kPreviewMaxHeight = 440;

/** 预览用等比缩放（拿到空图返回空 pixmap） */
QPixmap scaledPreview(const QImage &image)
{
    if (image.isNull())
        return QPixmap();
    return QPixmap::fromImage(image).scaled(
        kPreviewMaxWidth, kPreviewMaxHeight,
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace

OrthoViewsPreviewDialog::OrthoViewsPreviewDialog(QWidget *parent, const QImage &front,
                                                 const QImage &left, const QImage &top,
                                                 const QString &baseName)
    : QDialog(parent)
    , m_front(front)
    , m_left(left)
    , m_top(top)
    , m_baseName(baseName)
{
    setObjectName(QStringLiteral("OrthoViewsPreviewDialog"));
    setWindowTitle(tr("三视图预览"));
    setMinimumSize(860, 560);
    resize(1020, 640);

    // 统一使用主题令牌着色（与投影编辑器同一套卡片语言）
    setStyleSheet(StyleKit::resolve(QStringLiteral(
        "QDialog#OrthoViewsPreviewDialog {"
        "  background-color: @BG_BASE@;"
        "}"
        "QFrame#orthoViewCard {"
        "  background-color: @BG_CARD@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 12px;"
        "}"
        "QLabel#orthoViewTitle {"
        "  color: @TEXT_PRIMARY@;"
        "  font-size: 13px;"
        "  font-weight: 700;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#orthoMutedLabel {"
        "  color: @TEXT_TERTIARY@;"
        "  font-size: 12px;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#orthoImageLabel {"
        "  background: @BG_MUTED@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 10px;"
        "  color: @TEXT_TERTIARY@;"
        "}")));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(20, 18, 20, 16);
    outer->setSpacing(10);

    auto *intro = new QLabel(tr("按正、侧、俯三个方向生成的正交投影图，确认效果后一次保存为三张 PNG"),
                             this);
    intro->setObjectName(QStringLiteral("orthoMutedLabel"));
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(StyleKit::resolve(QStringLiteral(
        "QScrollArea{background:transparent;}")));
    auto *rowHost = new QWidget(scroll);
    rowHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *row = new QHBoxLayout(rowHost);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(14);
    row->addWidget(makeViewCard(tr("正视图"), m_front));
    row->addWidget(makeViewCard(tr("侧视图"), m_left));
    row->addWidget(makeViewCard(tr("俯视图"), m_top));
    row->addStretch(1);
    scroll->setWidget(rowHost);
    outer->addWidget(scroll, 1);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName(QStringLiteral("orthoMutedLabel"));
    m_hintLabel->setWordWrap(true);
    m_hintLabel->hide();
    outer->addWidget(m_hintLabel);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    auto *closeButton = new QPushButton(tr("关闭"), this);
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonRow->addWidget(closeButton);
    auto *saveButton = new QPushButton(tr("保存图片"), this);
    saveButton->setObjectName(QStringLiteral("defaultButton"));
    saveButton->setCursor(Qt::PointingHandCursor);
    saveButton->setDefault(true);
    connect(saveButton, &QPushButton::clicked, this, &OrthoViewsPreviewDialog::onSaveClicked);
    buttonRow->addWidget(saveButton);
    outer->addLayout(buttonRow);
}

QWidget *OrthoViewsPreviewDialog::makeViewCard(const QString &title, const QImage &image)
{
    auto *card = new QFrame(this);
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setObjectName(QStringLiteral("orthoViewCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("orthoViewTitle"));
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    auto *imageLabel = new QLabel(card);
    imageLabel->setObjectName(QStringLiteral("orthoImageLabel"));
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setMinimumSize(kPreviewMaxWidth / 2, 120);
    const QPixmap preview = scaledPreview(image);
    if (preview.isNull())
    {
        imageLabel->setText(tr("（空）"));
    }
    else
    {
        imageLabel->setPixmap(preview);
        imageLabel->setToolTip(tr("%1 × %2 像素").arg(image.width()).arg(image.height()));
    }
    layout->addWidget(imageLabel, 1);

    return card;
}

void OrthoViewsPreviewDialog::onSaveClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("选择保存文件夹"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    if (dir.isEmpty())
        return;

    const QString safeBase = m_baseName.isEmpty() ? QStringLiteral("projection") : m_baseName;
    const QList<QPair<QString, QImage>> views = {
        {tr("正视图"), m_front},
        {tr("侧视图"), m_left},
        {tr("俯视图"), m_top},
    };

    for (const auto &view : views)
    {
        QString target = QDir(dir).filePath(
            QStringLiteral("%1-%2.png").arg(safeBase, view.first));
        target.replace(QLatin1Char('"'), QLatin1Char('_'));
        if (!view.second.save(target, "PNG"))
        {
            AppMessageBox::warning(this, tr("保存失败"),
                                   tr("这张图写不进去：%1").arg(target));
            return;
        }
    }

    m_hintLabel->setText(tr("已保存 3 张图片到 %1").arg(QDir::toNativeSeparators(dir)));
    m_hintLabel->show();
    AppMessageBox::information(this, tr("保存成功"),
                               tr("三张三视图已保存到：\n%1").arg(QDir::toNativeSeparators(dir)));
}
