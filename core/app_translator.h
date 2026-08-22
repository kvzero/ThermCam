#ifndef APP_TRANSLATOR_H
#define APP_TRANSLATOR_H

#include <QTranslator>

#include "core/settings_types.h"

class QCoreApplication;

/** @brief Owns the application translator selected by the persisted UI language. */
class AppTranslator final {
public:
    static AppTranslator& instance();

    /** Installs the persisted language translator, or leaves source English active. */
    bool initialize(QCoreApplication& application);

    /** Replaces the active translator without restarting the application. */
    bool setLanguage(QCoreApplication& application, AppLanguage language);

private:
    AppTranslator() = default;

    QTranslator m_translator;
};

#endif // APP_TRANSLATOR_H
