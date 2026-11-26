/**
 * @file   ChatPrefs.h
 * @brief  聊天页的通用偏好：发送方式 / 消息字号（只存本机，换账号也不变）
 * @author BlockBox Team
 * @date   2026-11-20
 *
 * 置顶、免打扰、草稿是「按账号」分开存的（见 ChatUiStore），
 * 这里的发送键、字号属于本机使用习惯，跟登录哪个账号无关，单独存一份 JSON。
 */
#pragma once

#include <QObject>
#include <QString>

class ChatPrefs : public QObject
{
    Q_OBJECT

public:
    /** 聊天字号档位：小 / 标准 / 大 */
    enum FontSize
    {
        FontSmall = 0,
        FontNormal = 1,
        FontLarge = 2,
    };

    /** 全局单例（首次使用时创建并读盘） */
    static ChatPrefs *instance();

    /** 回车直接发送（关闭后回车换行，Ctrl+Enter 发送） */
    bool sendOnEnter() const { return m_sendOnEnter; }
    void setSendOnEnter(bool on);

    /** 私聊发文件时优先 P2P 直传（连不上自动改走服务器；关闭则始终走服务器） */
    bool p2pDirectSend() const { return m_p2pDirectSend; }
    void setP2pDirectSend(bool on);

    FontSize fontSize() const { return m_fontSize; }
    void setFontSize(FontSize size);

    /** 按当前档位换算像素值：基准 13px 时 小 12 / 标准 13 / 大 15 */
    int fontPx(int base) const;

signals:
    /** 发送方式或字号有变化（聊天页据此刷新提示文字、重绘消息） */
    void changed();

private:
    explicit ChatPrefs(QObject *parent = nullptr);

    QString filePath() const;
    void load();
    void save();

    bool m_sendOnEnter = true;
    bool m_p2pDirectSend = true;
    FontSize m_fontSize = FontNormal;
};
