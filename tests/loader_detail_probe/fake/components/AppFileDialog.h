/**
 * 探针用 AppFileDialog 桩：仅声明 LoaderDetailPage.cpp 引用到的接口，
 * 避免拉入真实实现（3.7k 行 + LocalCategoryManager 依赖链）。
 */
#ifndef APPFILEDIALOG_H
#define APPFILEDIALOG_H

#include <QString>

class AppFileDialog
{
public:
    enum Option {
        DontResolveSymlinks = 0x00000001,
        ShowDirsOnly = 0x00000002,
        DontUseNativeDialog = 0x00000004
    };

    static QString getExistingDirectory(void * = nullptr, const QString & = QString(),
                                        const QString & = QString(),
                                        int = ShowDirsOnly | DontResolveSymlinks)
    {
        return QString();
    }
};

#endif // APPFILEDIALOG_H
