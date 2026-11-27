/**
 * @file   InstancePluginExtPage.h
 * @brief  实例助手「扩展」页：把插件声明的面板嵌进来
 * @author BlockBox Team
 *
 * 插件只要在 plugin.json 里声明：
 *
 *   "permissions": ["ui:page", ...],
 *   "ui": {
 *     "placements": ["pluginCard", "instanceAssistant"],
 *     "fields":   [ ... ],
 *     "actions":  [ ... ]
 *   }
 *
 * 这个面板就会出现在实例助手窗口的「扩展」页里，用户点按钮即可执行插件动作，
 * 不需要再进插件详情页。没有插件声明时，本页不会出现在导航栏。
 */
#ifndef INSTANCEPLUGINEXTPAGE_H
#define INSTANCEPLUGINEXTPAGE_H

#include <QWidget>

class InstancePluginExtPage : public QWidget
{
    Q_OBJECT
public:
    explicit InstancePluginExtPage(QWidget *parent = nullptr);

    /// 当前有没有插件声明 `<code>ui.placements</code>` 含 instanceAssistant
    static bool hasPanels();

signals:
    void statusMessage(const QString &text);

private:
    void rebuild();
    void buildPluginCard(class QVBoxLayout *host, const struct PluginInfo &info);
    void runAction(const struct PluginInfo &info, const struct PluginUiAction &action,
                   const QMap<QString, QWidget *> &fieldWidgets, class QPlainTextEdit *output);

    QString fieldValue(QWidget *widget) const;

    class QVBoxLayout *m_host = nullptr;
    class QProcess *m_proc = nullptr;
};

#endif // INSTANCEPLUGINEXTPAGE_H
