/**
 * 探针：确认默认 QSettings() 与 QSettings("BlockBox","BlockBox") 是否同一存储，
 * 以及 aiConfiguredModels 能读到几条。
 */
#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTextStream>
#include "utils/AiProviderPresets.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QFile out(QStringLiteral("ai_model_list_probe_out.txt"));
    out.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QTextStream ts(&out);

    QSettings def;
    ts << "default fileName: " << def.fileName() << "\n";
    ts << "default organizationName: [" << QCoreApplication::organizationName() << "]\n";
    ts << "default applicationName: [" << QCoreApplication::applicationName() << "]\n";
    ts << "default ai/customModels size: " << def.value("ai/customModels").toString().size() << "\n";
    ts << "default ai/provider_openrouter/key: "
       << def.value("ai/provider_openrouter/key").toString().left(8) << "\n";

    QSettings named(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    ts << "named fileName: " << named.fileName() << "\n";
    ts << "named ai/customModels size: " << named.value("ai/customModels").toString().size() << "\n";
    ts << "named ai/provider_openrouter/key: "
       << named.value("ai/provider_openrouter/key").toString().left(8) << "\n";
    ts << "named ai/provider_openrouter/selectedModel: "
       << named.value("ai/provider_openrouter/selectedModel").toString() << "\n";
    ts << "named ai/defaultModelId: [" << named.value("ai/defaultModelId").toString() << "]\n";
    ts.flush();

    const auto models = aiConfiguredModels();
    ts << "aiConfiguredModels count: " << models.size() << "\n";
    for (const auto &m : models) {
        ts << " - " << m.id << " | " << m.displayName << "\n";
    }
    ts.flush();
    out.close();
    return 0;
}
