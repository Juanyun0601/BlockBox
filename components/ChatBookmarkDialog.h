/**
 * @file   ChatBookmarkDialog.h
 * @brief  聊天收藏列表：集中查看收藏过的消息，可回到原会话或删掉收藏
 * @author BlockBox Team
 * @date   2026-11-14
 *
 * 聊天页左栏顶部的星标按钮打开。列表来自 ChatBookmarkStore（本机按账号保存），
 * 点「打开」会带着会话 key 回到聊天页，由页面负责跳到对应好友 / 群聊。
 */
#pragma once

#include <QString>
#include <QWidget>

#include <functional>

#include "AppDialogBase.h"

class QLabel;
class QScrollArea;
class QVBoxLayout;

class ChatBookmarkDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 打开聊天收藏弹窗
     * @param parent 宿主页面（提示与遮罩挂它上面）
     * @param onOpen 点了某条收藏的「打开」后回调，参数是会话 key（f12 / g3）
     */
    static void show(QWidget *parent, std::function<void(const QString &)> onOpen);

    explicit ChatBookmarkDialog(QWidget *parent = nullptr);

private:
    void initUI();
    void initStyle();
    void rebuild();

    QLabel *m_emptyLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QScrollArea *m_scroll = nullptr;
    QVBoxLayout *m_listLayout = nullptr;

    /** 用户点了「打开」的会话 key；空表示没点 */
    QString m_openKey;
};
