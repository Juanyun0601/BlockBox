/**
 * 探针：直接构造投影方块编辑器对话框，检查「当前方块」下拉框的中文显示与解析。
 * （链接启动器全部目标文件，仅替换 main；结果写入 UTF-8 文本）
 */

#include "components/ProjectionBlockEditorDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QStringList log;

    ProjectionBlockEditorDialog dlg(nullptr);
    auto *combo = dlg.findChild<QComboBox *>(QStringLiteral("projBlockCombo"));

    auto dump = [&log, combo](const QString &title) {
        log << QStringLiteral("---- %1 ----").arg(title);
        log << QStringLiteral("项数=%1 当前项=%2 当前文字=[%3] 当前数据=[%4] 提示=[%5]")
                   .arg(combo->count()).arg(combo->currentIndex())
                   .arg(combo->currentText(), combo->currentData().toString(), combo->toolTip());
        for (int i = 0; i < combo->count(); ++i)
            log << QStringLiteral("  item[%1] 文字=[%2] 数据=[%3] 项提示=[%4]")
                       .arg(i).arg(combo->itemText(i), combo->itemData(i).toString(),
                                   combo->itemData(i, Qt::ToolTipRole).toString());
    };

    if (!combo)
    {
        log << QStringLiteral("未找到对象名为 projBlockCombo 的下拉框");
    }
    else
    {
        dump(QStringLiteral("构造完成（未绑定实例，应走内置中文名）"));

        // 手输中文名：提示应变为解析出的英文 ID
        combo->setCurrentText(QStringLiteral("橡木活板门"));
        log << QStringLiteral("手输中文名 -> 解析=[%1]").arg(combo->toolTip());

        // 手输英文短 ID（带状态）
        combo->setCurrentText(QStringLiteral("oak_trapdoor[facing=south]"));
        log << QStringLiteral("手输英文短ID -> 解析=[%1]").arg(combo->toolTip());

        // 手输调色板风格文案
        combo->setCurrentText(QStringLiteral("石头 (stone)"));
        log << QStringLiteral("手输调色板文案 -> 解析=[%1]").arg(combo->toolTip());

        // 手动输入无法识别的内容应原样保留
        combo->setCurrentText(QStringLiteral("这不是方块"));
        log << QStringLiteral("手输无效内容 -> 解析=[%1]").arg(combo->toolTip());

        // 选中一个预设项，再触发「无实例」刷新：应保留所选方块且仍显示中文
        combo->setCurrentIndex(1);
        log << QStringLiteral("选中预设项 -> 数据=[%1]").arg(combo->currentData().toString());
        dlg.setTextureSource(QString());
        dump(QStringLiteral("setTextureSource(\"\") 之后（应仍选中同一方块）"));

        // 切到中文名解析出的方块，再刷新一次
        combo->setCurrentText(QStringLiteral("铁活板门"));
        log << QStringLiteral("手输铁活板门 -> 解析=[%1]").arg(combo->toolTip());
    }

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/.syntax_check/probe_pbed_combo_out.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        ts << log.join(QLatin1Char('\n')) << Qt::endl;
        ts.flush();
    }
    return 0;
}
