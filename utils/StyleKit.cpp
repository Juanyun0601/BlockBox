/**
 * @file   StyleKit.cpp
 * @brief  集中式内联样式工厂实现
 * @author BlockBox Team
 * @date   2026-08-30
 *
 * 所有原型均以 @TOKEN@ 占位符书写，经 ThemeManager::resolveTokens()
 * 在调用时解析为当前主题颜色，与全局 style.qss 共用同一套设计令牌。
 */
#include "StyleKit.h"

#include "ThemeManager.h"

#include <QColor>
#include <QStyle>
#include <QWidget>

namespace StyleKit
{
    QString resolve(const QString& raw)
    {
        return ThemeManager::instance()->resolveTokens(raw);
    }

    /* ───────────────────────────── 文本 ───────────────────────────── */

    QString pageTitle()
    {
        return resolve(QStringLiteral(
            "QLabel { font-size: 18px; font-weight: bold; color: @TEXT_PRIMARY@; }"));
    }

    QString sectionTitle()
    {
        return resolve(QStringLiteral(
            "QLabel { font-size: 14px; font-weight: bold; color: @TEXT_PRIMARY@; }"));
    }

    QString cardTitle()
    {
        return resolve(QStringLiteral(
            "QLabel { font-size: 15px; font-weight: bold; color: @TEXT_PRIMARY@;"
            " border: none; background: transparent; }"));
    }

    QString bodyLabel()
    {
        return resolve(QStringLiteral(
            "QLabel { font-size: 13px; color: @TEXT_PRIMARY@; }"));
    }

    QString secondaryLabel()
    {
        return resolve(QStringLiteral(
            "QLabel { font-size: 13px; color: @TEXT_SECONDARY@; }"));
    }

    QString mutedLabel()
    {
        return resolve(QStringLiteral(
            "QLabel { font-size: 12px; color: @TEXT_TERTIARY@;"
            " border: none; background: transparent; }"));
    }

    QString captionLabel()
    {
        return resolve(QStringLiteral(
            "QLabel { font-size: 11px; color: @TEXT_TERTIARY@;"
            " border: none; background: transparent; }"));
    }

    QString successLabel()
    {
        return resolve(QStringLiteral(
            "QLabel { font-size: 13px; color: @SUCCESS@; }"));
    }

    QString dangerLabel()
    {
        return resolve(QStringLiteral(
            "QLabel { font-size: 13px; color: @DANGER@; }"));
    }

    /* ───────────────────────────── 按钮 ───────────────────────────── */

    QString primaryButton()
    {
        return resolve(QStringLiteral(
            "QPushButton {"
            "  font-size: 14px;"
            "  font-weight: 500;"
            "  color: @TEXT_ON_PRIMARY@;"
            "  border: none;"
            "  border-radius: 8px;"
            "  padding: 8px 24px;"
            "  background-color: @PRIMARY@;"
            "}"
            "QPushButton:hover {"
            "  background-color: @PRIMARY_HOVER@;"
            "}"
            "QPushButton:pressed {"
            "  background-color: @PRIMARY_PRESSED@;"
            "}"
            "QPushButton:disabled {"
            "  background-color: @BG_DISABLED@;"
            "  color: @TEXT_DISABLED@;"
            "}"));
    }

    QString smallPrimaryButton()
    {
        return resolve(QStringLiteral(
            "QPushButton {"
            "  font-size: 13px;"
            "  font-weight: 500;"
            "  color: @TEXT_ON_PRIMARY@;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 6px 12px;"
            "  background-color: @PRIMARY@;"
            "}"
            "QPushButton:hover {"
            "  background-color: @PRIMARY_HOVER@;"
            "}"
            "QPushButton:pressed {"
            "  background-color: @PRIMARY_PRESSED@;"
            "}"
            "QPushButton:disabled {"
            "  background-color: @BG_DISABLED@;"
            "  color: @TEXT_DISABLED@;"
            "}"));
    }

    QString miniButton()
    {
        return resolve(QStringLiteral(
            "QPushButton {"
            "  font-size: 12px;"
            "  padding: 4px 12px;"
            "  border: 1px solid @BORDER_STRONG@;"
            "  border-radius: 4px;"
            "  background: transparent;"
            "  color: @TEXT_SECONDARY@;"
            "}"
            "QPushButton:hover {"
            "  border-color: @BORDER_HOVER@;"
            "  color: @TEXT_PRIMARY@;"
            "}"));
    }

    QString outlineButton()
    {
        return resolve(QStringLiteral(
            "QPushButton {"
            "  font-size: 14px;"
            "  font-weight: 500;"
            "  color: @PRIMARY@;"
            "  border: 1px solid @PRIMARY@;"
            "  border-radius: 8px;"
            "  padding: 8px 24px;"
            "  background: transparent;"
            "}"
            "QPushButton:hover {"
            "  background-color: @PRIMARY_BG@;"
            "}"
            "QPushButton:pressed {"
            "  border-color: @PRIMARY_PRESSED@;"
            "  color: @PRIMARY_PRESSED@;"
            "}"
            "QPushButton:disabled {"
            "  color: @TEXT_DISABLED@;"
            "  border-color: @BORDER_DISABLED@;"
            "}"));
    }

    QString dangerButton()
    {
        const QString danger = resolve(QStringLiteral("@DANGER@"));
        const QString hover = QColor(danger).darker(110).name();
        return resolve(QStringLiteral(
            "QPushButton {"
            "  font-size: 14px;"
            "  font-weight: 500;"
            "  color: @TEXT_ON_PRIMARY@;"
            "  border: none;"
            "  border-radius: 8px;"
            "  padding: 8px 24px;"
            "  background-color: @DANGER@;"
            "}"
            "QPushButton:hover {"
            "  background-color: %1;"
            "}").arg(hover));
    }

    QString ghostButton()
    {
        return resolve(QStringLiteral(
            "QPushButton {"
            "  font-size: 13px;"
            "  color: @TEXT_SECONDARY@;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 4px 8px;"
            "  background: transparent;"
            "}"
            "QPushButton:hover {"
            "  color: @TEXT_PRIMARY@;"
            "  background-color: @BG_HOVER@;"
            "}"));
    }

    /* ───────────────────────────── 输入 ───────────────────────────── */

    QString lineEdit()
    {
        return resolve(QStringLiteral(
            "QLineEdit {"
            "  font-size: 13px;"
            "  padding: 8px 12px;"
            "  border: 1px solid @BORDER_STRONG@;"
            "  border-radius: 6px;"
            "}"
            "QLineEdit:focus {"
            "  border: 1px solid @PRIMARY@;"
            "}"));
    }

    QString textEdit()
    {
        return resolve(QStringLiteral(
            "QTextEdit {"
            "  font-size: 13px;"
            "  padding: 8px 12px;"
            "  border: 1px solid @BORDER_STRONG@;"
            "  border-radius: 6px;"
            "}"
            "QTextEdit:focus {"
            "  border: 1px solid @PRIMARY@;"
            "}"));
    }

    QString checkBox()
    {
        return resolve(QStringLiteral(
            "QCheckBox { font-size: 13px; }"));
    }

    /* ───────────────────────────── 容器 ───────────────────────────── */

    QString card()
    {
        return resolve(QStringLiteral(
            "background-color: @BG_CARD@;"
            " border: 1px solid @BORDER@;"
            " border-radius: 12px;"));
    }

    QString flatCard()
    {
        return resolve(QStringLiteral(
            "background-color: @BG_CARD@;"
            " border: none;"
            " border-radius: 12px;"));
    }

    QString transparent()
    {
        return resolve(QStringLiteral(
            "background: transparent; border: none;"));
    }

    QString chip(const QString& bg, const QString& fg, const QString& border)
    {
        QString style = resolve(QStringLiteral(
            "background-color: %1; color: %2;"
            " border-radius: 10px; padding: 3px 10px; font-size: 11px;")
            .arg(bg, fg));
        if (!border.isEmpty())
            style += resolve(QStringLiteral(" border: 1px solid %1;").arg(border));
        return style;
    }

    QString colorSwatch(const QString& color)
    {
        return resolve(QStringLiteral(
            "background-color: %1;"
            " border: 1px solid @BORDER_STRONG@;"
            " border-radius: 6px;").arg(color));
    }

    /* ─────────────────────────── 工具函数 ─────────────────────────── */

    void setClass(QWidget* w, const QString& value)
    {
        if (!w)
            return;
        w->setProperty("class", value);
        if (QStyle* style = w->style()) {
            style->unpolish(w);
            style->polish(w);
        }
        w->update();
    }

    QString highlightKeyword(const QString& text, const QString& keyword)
    {
        const QString kw = keyword.trimmed();
        if (kw.isEmpty() || !text.contains(kw, Qt::CaseInsensitive))
            return text.toHtmlEscaped();

        const QString open = resolve(QStringLiteral(
            "<span style=\"background-color:@SUCCESS_BG@;color:@SUCCESS@;\">"));
        const QString close = QStringLiteral("</span>");

        // 逐段切分：未匹配段与匹配段分别转义，避免先转义后索引错位
        QString out;
        int pos = 0;
        while (true)
        {
            const int idx = text.indexOf(kw, pos, Qt::CaseInsensitive);
            if (idx < 0)
            {
                out += text.mid(pos).toHtmlEscaped();
                break;
            }
            out += text.mid(pos, idx - pos).toHtmlEscaped();
            out += open;
            out += text.mid(idx, kw.length()).toHtmlEscaped();
            out += close;
            pos = idx + kw.length();
        }
        return out;
    }
}
