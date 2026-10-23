// 临时诊断：用本机真实配置（QSettings）跑一次真实翻译请求，看服务端到底回什么
#include <QCoreApplication>
#include <QEventLoop>
#include <QSettings>
#include <QTimer>
#include <QDebug>
#include "utils/translation/TranslationService.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QSettings s(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    qInfo() << "defaultModelId:" << s.value("ai/defaultModelId").toString();
    qInfo() << "settingsKeys:" << s.allKeys().size();

    TranslationConfig cfg;
    cfg.source = TranslationSource::AI;
    cfg.sourceLanguage = QStringLiteral("en");
    cfg.targetLanguage = QStringLiteral("zh");
    qInfo() << "cfg.source:" << int(cfg.source)
            << "customModel:" << cfg.customModel
            << "customApiUrl:" << cfg.customApiUrl
            << "customApiKeyLen:" << cfg.customApiKey.size()
            << "target:" << cfg.targetLanguage;

    TranslationService svc;

    QEventLoop loop;
    QMetaObject::Connection c1 = QObject::connect(&svc, &TranslationService::listTranslationFinished,
        [&](const QString &t) {
            qInfo() << "LIST OK len=" << t.size() << "head=" << t.left(120);
            loop.quit();
        });
    QMetaObject::Connection c2 = QObject::connect(&svc, &TranslationService::translationError,
        [&](const QString &e) {
            qWarning() << "LIST ERR:" << e;
            loop.quit();
        });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    svc.translateList(QStringLiteral("[#0#] Create ||| A mod about machines and factories"),
                      cfg);
    loop.exec();
    QObject::disconnect(c1);
    QObject::disconnect(c2);

    // 单段翻译（详情页走这条）
    TranslationService svc2;
    QEventLoop loop2;
    QMetaObject::Connection c3 = QObject::connect(&svc2, &TranslationService::translationFinished,
        [&](const QString &t) {
            qInfo() << "SINGLE OK len=" << t.size() << "head=" << t.left(120);
            loop2.quit();
        });
    QMetaObject::Connection c4 = QObject::connect(&svc2, &TranslationService::translationError,
        [&](const QString &e) {
            qWarning() << "SINGLE ERR:" << e;
            loop2.quit();
        });
    QTimer::singleShot(30000, &loop2, &QEventLoop::quit);
    svc2.translate(QStringLiteral("A mod about machines and factories."), cfg);
    loop2.exec();
    QObject::disconnect(c3);
    QObject::disconnect(c4);

    return 0;
}
