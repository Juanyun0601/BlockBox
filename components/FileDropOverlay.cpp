/**
 * @file   FileDropOverlay.cpp
 * @brief  全屏拖拽覆盖层实现
 * @author BlockBox Team
 * @date   2026-08-24
 */
#include "FileDropOverlay.h"

#include <QPainter>
#include <QPainterPath>
#include "utils/ThemeManager.h"

FileDropOverlay::FileDropOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    hide();

    m_layout = new QVBoxLayout(this);
    m_layout->setAlignment(Qt::AlignCenter);
    m_layout->setSpacing(12);

    // 图标
    m_iconLabel = new QLabel(this);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedSize(80, 80);
    m_iconLabel->setStyleSheet(
        "background: rgba(255, 255, 255, 0.15);"
        "border-radius: 40px;"
        "border: 3px dashed rgba(255, 255, 255, 0.6);"
    );
    m_layout->addWidget(m_iconLabel, 0, Qt::AlignCenter);

    // 提示文字
    m_hintLabel = new QLabel(tr("释放以导入文件"), this);
    m_hintLabel->setAlignment(Qt::AlignCenter);
    m_hintLabel->setStyleSheet(
        "color: rgba(255, 255, 255, 0.95);"
        "font-size: 18px;"
        "font-weight: bold;"
        "background: transparent;"
        "border: none;"
    );
    m_layout->addWidget(m_hintLabel, 0, Qt::AlignCenter);

    // 文件数量
    m_countLabel = new QLabel(this);
    m_countLabel->setAlignment(Qt::AlignCenter);
    m_countLabel->setStyleSheet(
        "color: rgba(255, 255, 255, 0.7);"
        "font-size: 14px;"
        "background: transparent;"
        "border: none;"
    );
    m_layout->addWidget(m_countLabel, 0, Qt::AlignCenter);
}

void FileDropOverlay::activate(int fileCount)
{
    m_active = true;
    m_countLabel->setText(tr("已选择 %1 个文件").arg(fileCount));
    show();
    raise();
    update();
}

void FileDropOverlay::updatePreview(const QString &typeSummary)
{
    if (!typeSummary.isEmpty())
        m_countLabel->setText(typeSummary);
}

void FileDropOverlay::deactivate()
{
    m_active = false;
    hide();
}

void FileDropOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 半透明深色遮罩
    painter.fillRect(rect(), QColor(0, 0, 0, 140));

    // 居中高亮边框区域
    int margin = 60;
    QRect cardRect = rect().adjusted(margin, margin, -margin, -margin);

    QPainterPath path;
    path.addRoundedRect(cardRect, 16, 16);

    // 内部微弱填充
    painter.fillPath(path, QColor(255, 255, 255, 15));

    // 虚线边框
    QPen pen(QColor(255, 255, 255, 80), 2, Qt::DashLine);
    painter.setPen(pen);
    painter.drawPath(path);
}
