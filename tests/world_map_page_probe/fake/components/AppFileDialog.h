/**
 * 探针用 AppFileDialog 桩：WorldMapPage 只用到 getSaveFileName（导出图片），
 * 真实实现会拉入 LocalCategoryManager / ModData / AppDialogBase 一整条依赖链。
 */
#ifndef APPFILEDIALOG_H
#define APPFILEDIALOG_H

#include <QString>
#include <QStringList>

class QWidget;

class AppFileDialog
{
public:
    static QString getSaveFileName(QWidget *parent, const QString &caption,
                                   const QString &dir = QString(),
                                   const QString &filter = QString(),
                                   QString *selectedFilter = nullptr, int options = 0)
    {
        Q_UNUSED(parent);
        Q_UNUSED(caption);
        Q_UNUSED(dir);
        Q_UNUSED(filter);
        Q_UNUSED(selectedFilter);
        Q_UNUSED(options);
        return QString();
    }
};

#endif // APPFILEDIALOG_H
