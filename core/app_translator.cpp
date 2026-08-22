#include "core/app_translator.h"

#include "core/settings_store.h"

#include <QCoreApplication>
#include <QDebug>

namespace {
const char* kSimplifiedChineseTranslationResource = ":/i18n/thermal_zh_CN.qm";
}

AppTranslator& AppTranslator::instance() {
    static AppTranslator translator;
    return translator;
}

bool AppTranslator::initialize(QCoreApplication& application) {
    const SettingsSnapshot settings = SettingsStore::instance().current();
    const AppLanguage language = appLanguageFromValue(
        settings.values.value(SettingKey::AppLanguage).toInt());

    return setLanguage(application, language);
}

bool AppTranslator::setLanguage(QCoreApplication& application, AppLanguage language) {
    if (language == AppLanguage::SimplifiedChinese) {
        if (!m_translator.load(QString::fromLatin1(kSimplifiedChineseTranslationResource))) {
            qWarning() << "Failed to load Simplified Chinese translation resource";
            return false;
        }
    }

    application.removeTranslator(&m_translator);
    if (language == AppLanguage::SimplifiedChinese) {
        application.installTranslator(&m_translator);
    }

    return true;
}
