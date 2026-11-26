/**
 * @file   ForumCarouselEditor.cpp
 * @brief  帖子轮播图编辑器实现（发布页 / 编辑对话框共用）
 * @author BlockBox Team
 * @date   2026-11-21
 */

#include "ForumCarouselEditor.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "NotificationManager.h"
#include <utils/ForumAccountService.h>
#include <utils/ForumImageStore.h>
#include <utils/ForumLobbyService.h>
#include <utils/StyleKit.h>

namespace {

/** 轮播缩略图的统一尺寸 */
constexpr int kThumbWidth = 64;
constexpr int kThumbHeight = 40;

/** 从用户粘贴的内容里提取 B 站 BV 号：认完整链接，也认裸 BV 号 */
QString parseBilibiliBv(const QString &input)
{
    const QString text = input.trimmed();
    if (text.isEmpty())
        return QString();
    const QRegularExpression linkRe(QStringLiteral("bilibili\\.com/video/(BV[0-9A-Za-z]+)"),
                                    QRegularExpression::CaseInsensitiveOption);
    const auto linkMatch = linkRe.match(text);
    if (linkMatch.hasMatch())
        return linkMatch.captured(1);
    const QRegularExpression bareRe(QStringLiteral("^BV[0-9A-Za-z]{8,}$"));
    const auto bareMatch = bareRe.match(text);
    if (bareMatch.hasMatch())
        return bareMatch.captured(0);
    return QString();
}

/** 缩略图底板的通用样式（占位 / 加载中都长这样，图到了直接铺上去） */
QString thumbFrameStyle()
{
    return StyleKit::resolve(QStringLiteral(
        "background:@BG_MUTED@;border:1px solid @BORDER@;border-radius:6px;"));
}

} // namespace

ForumCarouselEditor::ForumCarouselEditor(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_rowsLayout = new QVBoxLayout();
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(6);
    layout->addLayout(m_rowsLayout);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName(QStringLiteral("forumMuted"));
    m_hintLabel->setWordWrap(true);
    buttonRow->addWidget(m_hintLabel, 1);

    auto *addBilibiliButton = new QPushButton(tr("添加 B 站视频…"), this);
    addBilibiliButton->setObjectName(QStringLiteral("forumGhostButton"));
    addBilibiliButton->setCursor(Qt::PointingHandCursor);
    addBilibiliButton->setFixedHeight(26);
    addBilibiliButton->setToolTip(tr("粘贴 bilibili.com/video/BV… 格式的视频链接"));
    connect(addBilibiliButton, &QPushButton::clicked, this, &ForumCarouselEditor::addBilibiliVideo);
    buttonRow->addWidget(addBilibiliButton);

    auto *addExternalButton = new QPushButton(tr("添加外链图片…"), this);
    addExternalButton->setObjectName(QStringLiteral("forumGhostButton"));
    addExternalButton->setCursor(Qt::PointingHandCursor);
    addExternalButton->setFixedHeight(26);
    addExternalButton->setToolTip(tr("直接贴一张网图的完整地址（http:// 或 https:// 开头），不用上传"));
    connect(addExternalButton, &QPushButton::clicked, this, &ForumCarouselEditor::addExternalImage);
    buttonRow->addWidget(addExternalButton);

    auto *addImageButton = new QPushButton(tr("添加图片…"), this);
    addImageButton->setObjectName(QStringLiteral("forumGhostButton"));
    addImageButton->setCursor(Qt::PointingHandCursor);
    addImageButton->setFixedHeight(26);
    addImageButton->setToolTip(tr("可一次选多张，自动压缩上传，最多 %1 条").arg(kMaxItems));
    connect(addImageButton, &QPushButton::clicked, this, &ForumCarouselEditor::addImageFiles);
    buttonRow->addWidget(addImageButton);

    layout->addLayout(buttonRow);
    rebuildRows();
}

QVector<ForumCarouselItem> ForumCarouselEditor::items() const
{
    QVector<ForumCarouselItem> items;
    for (const ForumCarouselEntry &entry : m_entries)
    {
        if (!entry.ready())
            continue;
        ForumCarouselItem item;
        item.type = entry.type;
        item.uploadId = entry.uploadId;
        item.url = entry.url;
        item.thumb = entry.thumb;
        items.append(item);
    }
    return items;
}

bool ForumCarouselEditor::hasPendingUploads() const
{
    for (const ForumCarouselEntry &entry : m_entries)
    {
        if (entry.uploading)
            return true;
    }
    return false;
}

bool ForumCarouselEditor::isEmpty() const
{
    return items().isEmpty();
}

void ForumCarouselEditor::clear()
{
    m_entries.clear();
    rebuildRows();
}

void ForumCarouselEditor::setItems(const QVector<ForumCarouselItem> &items)
{
    m_entries.clear();
    for (const ForumCarouselItem &item : items)
    {
        if (!item.isValid())
            continue;
        ForumCarouselEntry entry;
        entry.key = ++m_keySeed;
        entry.type = item.type;
        entry.uploadId = item.uploadId;
        entry.url = item.url;
        entry.thumb = item.thumb;
        m_entries.append(entry);
    }
    rebuildRows();
}

void ForumCarouselEditor::addImageFiles()
{
    const int remaining = kMaxItems - m_entries.size();
    if (remaining <= 0)
    {
        NotificationManager::showInfo(this, tr("轮播最多 %1 条，先删掉几条再添加").arg(kMaxItems));
        return;
    }

    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("选择轮播图片"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.webp *.gif)"));
    if (paths.isEmpty())
        return;

    int added = 0;
    for (const QString &path : paths)
    {
        if (added >= remaining)
            break;
        if (path.trimmed().isEmpty())
            continue;
        const QFileInfo info(path);
        if (!info.exists())
            continue;

        ForumCarouselEntry entry;
        entry.key = ++m_keySeed;
        entry.type = ForumCarouselItem::Image;
        entry.filePath = path;
        entry.uploading = true;
        m_entries.append(entry);
        ++added;
    }
    if (added == 0)
        return;
    if (paths.size() > remaining)
    {
        NotificationManager::showInfo(
            this, tr("轮播最多 %1 条，多选的图片没有加进来").arg(kMaxItems));
    }

    rebuildRows();
    // 按添加顺序逐张开传（并行上传，进度各自显示在条目上）
    for (const QString &path : paths)
    {
        for (const ForumCarouselEntry &entry : m_entries)
        {
            if (entry.filePath == path && entry.uploading && entry.uploadId <= 0)
            {
                uploadEntry(entry.key);
                break;
            }
        }
    }
}

void ForumCarouselEditor::addExternalImage()
{
    if (m_entries.size() >= kMaxItems)
    {
        NotificationManager::showInfo(this, tr("轮播最多 %1 条，先删掉几条再添加").arg(kMaxItems));
        return;
    }
    const QString input = QInputDialog::getText(
        this, tr("添加外链图片"),
        tr("粘贴网图的完整地址（以 http:// 或 https:// 开头）："),
        QLineEdit::Normal, QStringLiteral("https://"));
    if (input.trimmed().isEmpty())
        return;

    const QString url = input.trimmed().left(500);
    if (!QUrl(url).isValid() || !(url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                                  || url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        || url.contains(QLatin1Char(' ')))
    {
        NotificationManager::showInfo(
            this, tr("这不是有效的图片地址，要填以 http:// 或 https:// 开头的完整网址"));
        return;
    }
    // 同一条外链重复添加没有意义
    for (const ForumCarouselEntry &entry : m_entries)
    {
        if (!entry.url.isEmpty() && entry.url.compare(url, Qt::CaseInsensitive) == 0)
        {
            NotificationManager::showInfo(this, tr("这条外链已经在轮播里了"));
            return;
        }
    }

    ForumCarouselEntry entry;
    entry.key = ++m_keySeed;
    entry.type = ForumCarouselItem::Image;
    entry.url = url;
    m_entries.append(entry);
    rebuildRows();
}

void ForumCarouselEditor::addBilibiliVideo()
{
    if (m_entries.size() >= kMaxItems)
    {
        NotificationManager::showInfo(this, tr("轮播最多 %1 条，先删掉几条再添加").arg(kMaxItems));
        return;
    }
    const QString input = QInputDialog::getText(
        this, tr("添加 B 站视频"),
        tr("粘贴视频链接（bilibili.com/video/BV…）或直接填 BV 号："),
        QLineEdit::Normal, QString());
    if (input.trimmed().isEmpty())
        return;

    const QString bv = parseBilibiliBv(input);
    if (bv.isEmpty())
    {
        NotificationManager::showInfo(
            this, tr("没认出这是 B 站视频链接，请粘贴 bilibili.com/video/BV… 格式的链接"));
        return;
    }

    ForumCarouselEntry entry;
    entry.key = ++m_keySeed;
    entry.type = ForumCarouselItem::Bilibili;
    entry.url = QStringLiteral("https://www.bilibili.com/video/%1").arg(bv);
    m_entries.append(entry);
    rebuildRows();
}

void ForumCarouselEditor::uploadEntry(int key)
{
    int index = -1;
    for (int i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].key == key)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;

    ForumCarouselEntry &entry = m_entries[index];
    if (entry.type != ForumCarouselItem::Image || entry.filePath.isEmpty())
        return;
    entry.uploading = true;
    const QString path = entry.filePath;

    ForumLobbyService::instance()->uploadImageFull(
        path,
        [this, key, path](const ForumImageUpload &upload, const QString &message) {
            for (int i = 0; i < m_entries.size(); ++i)
            {
                if (m_entries[i].key != key)
                    continue;
                if (upload.isValid())
                {
                    m_entries[i].uploading = false;
                    m_entries[i].uploadId = upload.id;
                    m_entries[i].url = upload.url;
                    m_entries[i].thumb = upload.thumb;
                }
                else
                {
                    // 传不上去的这条撤掉，用户重新点「添加图片…」就能重试
                    m_entries.removeAt(i);
                    NotificationManager::showInfo(this, message);
                }
                break;
            }
            rebuildRows();
        });
}

void ForumCarouselEditor::removeEntry(int key)
{
    for (int i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].key == key)
        {
            m_entries.removeAt(i);
            break;
        }
    }
    rebuildRows();
}

void ForumCarouselEditor::moveEntry(int key, int delta)
{
    for (int i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].key != key)
            continue;
        const int target = i + delta;
        if (target < 0 || target >= m_entries.size())
            return;
        m_entries.swapItemsAt(i, target);
        break;
    }
    rebuildRows();
}

void ForumCarouselEditor::applyThumbnail(QLabel *label, const ForumCarouselEntry &entry)
{
    label->setFixedSize(kThumbWidth, kThumbHeight);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(thumbFrameStyle());

    if (entry.type == ForumCarouselItem::Bilibili)
    {
        label->setText(tr("▶ B站"));
        label->setStyleSheet(StyleKit::resolve(QStringLiteral(
            "background:@BG_MUTED@;border:1px solid @BORDER@;border-radius:6px;"
            "color:@TEXT_SECONDARY@;font-size:11px;")));
        return;
    }

    // 本地文件还在就直接读（上传前后都适用）；编辑旧帖时只有站内地址，走图片缓存
    if (!entry.filePath.isEmpty() && QFileInfo::exists(entry.filePath))
    {
        QImageReader reader(entry.filePath);
        reader.setAutoTransform(true);
        const QPixmap pixmap = QPixmap::fromImageReader(&reader);
        if (!pixmap.isNull())
        {
            label->setPixmap(pixmap.scaled(
                kThumbWidth, kThumbHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        return;
    }

    const QString source = !entry.thumb.isEmpty() ? entry.thumb : entry.url;
    if (source.isEmpty())
        return;
    QPointer<QLabel> guard(label);
    ForumImageStore::instance()->requestImage(
        ForumAccountService::instance()->absoluteUrl(source),
        [guard](const QImage &image) {
            if (guard.isNull() || image.isNull())
                return;
            guard->setPixmap(QPixmap::fromImage(image).scaled(
                kThumbWidth, kThumbHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        });
}

void ForumCarouselEditor::rebuildRows()
{
    if (m_rowsLayout)
    {
        while (m_rowsLayout->count() > 0)
        {
            QLayoutItem *item = m_rowsLayout->takeAt(0);
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
    }

    for (int i = 0; i < m_entries.size(); ++i)
    {
        const ForumCarouselEntry &entry = m_entries.at(i);
        auto *row = new QWidget(this);
        row->setAttribute(Qt::WA_StyledBackground, false);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto *thumb = new QLabel(row);
        applyThumbnail(thumb, entry);
        rowLayout->addWidget(thumb);

        auto *desc = new QLabel(row);
        desc->setObjectName(QStringLiteral("forumStatus"));
        QString text;
        if (entry.type == ForumCarouselItem::Bilibili)
        {
            text = tr("B 站视频 · %1").arg(entry.url.mid(entry.url.lastIndexOf(QLatin1String("BV"))));
        }
        else if (entry.uploading)
        {
            text = tr("%1 · 正在上传…").arg(QFileInfo(entry.filePath).fileName());
        }
        else if (entry.filePath.isEmpty() && entry.uploadId <= 0 && !entry.url.isEmpty()
                 && !entry.url.startsWith(QLatin1Char('/')))
        {
            // 外链图片：从地址里截个文件名给用户看（截不出来就显示域名）
            const QUrl parsed(entry.url);
            const QString name = parsed.fileName();
            text = tr("外链图片 · %1").arg(name.isEmpty() ? parsed.host() : name);
        }
        else
        {
            text = tr("图片 · %1").arg(
                entry.filePath.isEmpty()
                    ? entry.url.mid(entry.url.lastIndexOf(QLatin1Char('/')) + 1)
                    : QFileInfo(entry.filePath).fileName());
        }
        desc->setText(text);
        rowLayout->addWidget(desc, 1);

        auto *leftButton = new QPushButton(tr("←"), row);
        auto *rightButton = new QPushButton(tr("→"), row);
        auto *removeButton = new QPushButton(tr("✕"), row);
        for (QPushButton *button : { leftButton, rightButton, removeButton })
        {
            button->setObjectName(QStringLiteral("forumGhostButton"));
            button->setCursor(Qt::PointingHandCursor);
            button->setFixedHeight(24);
            button->setFixedWidth(30);
        }
        leftButton->setEnabled(i > 0);
        rightButton->setEnabled(i < m_entries.size() - 1);
        connect(leftButton, &QPushButton::clicked, this,
                [this, key = entry.key]() { moveEntry(key, -1); });
        connect(rightButton, &QPushButton::clicked, this,
                [this, key = entry.key]() { moveEntry(key, 1); });
        connect(removeButton, &QPushButton::clicked, this,
                [this, key = entry.key]() { removeEntry(key); });
        rowLayout->addWidget(leftButton);
        rowLayout->addWidget(rightButton);
        rowLayout->addWidget(removeButton);

        m_rowsLayout->addWidget(row);
    }

    if (m_hintLabel)
    {
        const int pending = std::count_if(m_entries.cbegin(), m_entries.cend(),
                                          [](const ForumCarouselEntry &entry) {
                                              return entry.uploading;
                                          });
        if (pending > 0)
            m_hintLabel->setText(tr("还有 %1 张图片在上传，发布前等它传完").arg(pending));
        else if (m_entries.isEmpty())
            m_hintLabel->setText(
                tr("轮播（选填）：图片会显示在帖子顶部，B 站视频点击跳转观看，最多 %1 条").arg(kMaxItems));
        else
            m_hintLabel->setText(tr("共 %1 条，用 ← → 调整展示顺序").arg(m_entries.size()));
    }
    emit changed();
}
