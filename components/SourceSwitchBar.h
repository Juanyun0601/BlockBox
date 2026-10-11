#ifndef SOURCESWITCHBAR_H
#define SOURCESWITCHBAR_H

#include <QWidget>

class QPushButton;

/* ============================================================
 * SourceSwitchBar — 资源详情页「来源」分段切换控件（Modrinth / CurseForge）
 * 跨源合并卡片的详情页顶部使用；样式复用 contentViewSwitch 分段控件 QSS
 * ============================================================ */
class SourceSwitchBar : public QWidget
{
    Q_OBJECT
public:
    explicit SourceSwitchBar(QWidget *parent = nullptr);

    /* 设置当前激活来源（"modrinth" / "curseforge"），不触发 sourceSelected */
    void setActiveSource(const QString &source);
    QString activeSource() const { return m_activeSource; }

signals:
    /* 用户点击另一来源段时发出 */
    void sourceSelected(const QString &source);

private:
    void updateButtons();

    QString m_activeSource;
    QPushButton *m_modrinthBtn = nullptr;
    QPushButton *m_curseforgeBtn = nullptr;
};

#endif // SOURCESWITCHBAR_H
