/**
 * @file   AutoHeightTextBrowser.h
 * @brief  随内容自动撑高的富文本浏览框
 * @author BlockBox Team
 * @date   2026-10-22
 *
 * 资源详情页的「详细信息」等长文本区域使用本控件：内部不出现滚动条，
 * 高度始终等于渲染后的完整内容高度，正文顺着页面一路排下去，
 * 由外层页面滚动区统一滚动，避免长描述被固定高度截断。
 */
#ifndef AUTOHEIGHTTEXTBROWSER_H
#define AUTOHEIGHTTEXTBROWSER_H

#include <QTextBrowser>

/**
 * @brief 高度跟随内容自动增长的 QTextBrowser
 *
 * 关闭内部滚动条，按当前可用宽度重新排版后把高度设为完整文本高度。
 * 宽度变化（窗口缩放）或内容变化（setHtml / 翻译切换）后会自动重新计算。
 */
class AutoHeightTextBrowser : public QTextBrowser
{
    Q_OBJECT

public:
    explicit AutoHeightTextBrowser(QWidget *parent = nullptr);

    /** 立即按当前宽度重新计算高度（一般无需手动调用） */
    void fitToContents();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void scheduleFit();

    bool m_fitting;      // 正在调整高度，避免重入
    bool m_fitPending;   // 已排队一次延后重排
    bool m_sized;        // 是否已经按内容设置过一次高度
    int m_fitPasses;     // 连续校准次数，防止尺寸来回抖死循环
};

#endif // AUTOHEIGHTTEXTBROWSER_H
