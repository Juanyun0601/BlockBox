#include "LanguageManager.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QMutexLocker>

LanguageManager *LanguageManager::m_instance = nullptr;

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
    , m_translator(new QTranslator(this))
    , m_currentLanguage(Chinese)
    , m_initialized(false)
{
}

LanguageManager::~LanguageManager()
{
    QMutexLocker locker(&m_mutex);
    if (m_translator && !m_translator->isEmpty()) {
        qApp->removeTranslator(m_translator);
    }
    m_instance = nullptr;
}

LanguageManager *LanguageManager::instance()
{
    if (!m_instance) {
        static QMutex instanceMutex;
        QMutexLocker locker(&instanceMutex);
        if (!m_instance) {
            m_instance = new LanguageManager();
        }
    }
    return m_instance;
}

void LanguageManager::setLanguage(Language lang)
{
    QMutexLocker locker(&m_mutex);

    if (m_currentLanguage == lang && !m_translator->isEmpty()) {
        return;
    }

    locker.unlock();
    setLanguageInternal(lang);
}

static const char *languageFileName(LanguageManager::Language lang)
{
    switch (lang) {
    case LanguageManager::English:             return "BlockBox_en.qm";
    case LanguageManager::ChineseTraditional:   return "BlockBox_zh_Hant.qm";
    case LanguageManager::Spanish:             return "BlockBox_es.qm";
    case LanguageManager::Chinese:
    default:                                   return "BlockBox_zh.qm";
    }
}

static const char *languageCode(LanguageManager::Language lang)
{
    switch (lang) {
    case LanguageManager::English:             return "en";
    case LanguageManager::ChineseTraditional:   return "zh_Hant";
    case LanguageManager::Spanish:             return "es";
    case LanguageManager::Chinese:
    default:                                   return "zh";
    }
}

void LanguageManager::setLanguagePreference(Language lang)
{
    // 仅写 QSettings，不安装翻译器、不触发 languageChanged。
    // 下次进程启动时由 loadLanguageSync() 读取并真正应用。
    QSettings settings("BlockBox", "BlockBox");
    settings.setValue("language", QString::fromLatin1(languageCode(lang)));
    settings.sync();
}

void LanguageManager::setLanguageInternal(Language lang)
{
    QMutexLocker locker(&m_mutex);

    if (!m_translator->isEmpty()) {
        qApp->removeTranslator(m_translator);
        m_translator->deleteLater();
        m_translator = new QTranslator(this);
    }

    QString translationFileName = QString::fromLatin1(languageFileName(lang));
    locker.unlock();

    bool loaded = m_translator->load(":/translations/" + translationFileName);

    locker.relock();

    if (loaded) {
        qApp->installTranslator(m_translator);
        m_currentLanguage = lang;

        QSettings settings("BlockBox", "BlockBox");
        settings.setValue("language", QString::fromLatin1(languageCode(lang)));
        settings.sync();

        emit languageChanged();
    } else {
        qWarning() << "[LanguageManager] Failed to load:" << translationFileName;
    }
}

void LanguageManager::loadLanguageSync()
{
    QSettings settings("BlockBox", "BlockBox");
    QString langCode = settings.value("language", "zh").toString();
    Language lang;
    if (langCode == "en")
        lang = English;
    else if (langCode == "zh_Hant")
        lang = ChineseTraditional;
    else if (langCode == "es")
        lang = Spanish;
    else
        lang = Chinese;
    setLanguageInternal(lang);
    {
        QMutexLocker locker(&m_mutex);
        m_initialized = true;
    }
}

LanguageManager::Language LanguageManager::currentLanguage() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentLanguage;
}

QFuture<void> LanguageManager::initializeAsync()
{
    return QtConcurrent::run([this]() {
        QSettings settings("BlockBox", "BlockBox");
        QString langCode = settings.value("language", "zh").toString();
        Language lang;
        if (langCode == "en")
            lang = English;
        else if (langCode == "zh_Hant")
            lang = ChineseTraditional;
        else if (langCode == "es")
            lang = Spanish;
        else
            lang = Chinese;

        QMetaObject::invokeMethod(this, [this, lang]() {
            if (m_translator->isEmpty())
                setLanguageInternal(lang);
            else
                setLanguage(lang);

            {
                QMutexLocker locker(&m_mutex);
                m_initialized = true;
            }

            emit initialized();
        }, Qt::QueuedConnection);
    });
}

bool LanguageManager::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}
