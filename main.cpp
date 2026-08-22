#include <QApplication>
#include <QFontDatabase>
#include <QDebug>
#include <utility>

#include "core/global_context.h"
#include "core/app_translator.h"
#include "core/settings_store.h"
#include "core/types.h"
#include "hardware/hardware_manager.h"
#include "hardware/imaging/thermal_camera.h"
#include "services/settings_service.h"
#include "ui/app.h"

extern "C" {
#include <libavutil/log.h>
}

#define FONT_PATH_ROBOTO "/usr/share/fonts/Roboto-Regular.ttf"
#define FONT_PATH_SANS   "/usr/share/fonts/SourceHanSansCN-Regular.otf"
#define FONT_PATH_ICON   "/usr/share/fonts/tabler-icons.ttf"

static void loadLocalFont(const QString &path) {
    int id = QFontDatabase::addApplicationFont(path);
    if (id < 0) {
        qWarning() << "Failed to load font from disk:" << path;
    } else {
        qInfo() << "Loaded font:" << QFontDatabase::applicationFontFamilies(id).at(0);
    }
}

int main(int argc, char *argv[])
{
    // Start the Seek backend early to reduce camera-ready latency after the UI comes up.
    ThermalCamera::StartupHandle cameraStartup = ThermalCamera::startSeekUsbEarly();

    QApplication a(argc, argv);

    av_log_set_level(AV_LOG_FATAL);

    /* Mandatory for Signal/Slot data passing */
    qRegisterMetaType<RawFrame>();
    qRegisterMetaType<VisualFrame>();
    qRegisterMetaType<BatteryStatus>();
    qRegisterMetaType<CaptureMode>();

    HardwareManager::instance().createCamera(std::move(cameraStartup));

    loadLocalFont(FONT_PATH_ROBOTO);
    loadLocalFont(FONT_PATH_SANS);
    loadLocalFont(FONT_PATH_ICON);
    // loadLocalFont(FONT_PATH_ICON_F);

    QFont mainFont("Roboto");
    mainFont.setPixelSize(14);
    mainFont.setHintingPreference(QFont::PreferFullHinting);
    a.setFont(mainFont);

    /* Settings */
    if (!SettingsStore::instance().init()) {
        qCritical() << "Fatal: Settings Store initialization failed.";
        return -1;
    }

    if (!AppTranslator::instance().initialize(a)) {
        qWarning() << "Starting with source language because translation initialization failed.";
    }

    /* Environment */
    if (!GlobalContext::instance().init()) {
        qCritical() << "Fatal: Global Context initialization failed.";
        return -1;
    }

    /* Hardware */
    if (!HardwareManager::instance().init()) {
        qCritical() << "Fatal: Hardware Management Layer failed.";
        return -1;
    }

    if (!SettingsService::instance().init()) {
        qCritical() << "Fatal: Settings Service initialization failed.";
        return -1;
    }

    /* User Interface */
    App w;
    w.showFullScreen();

    return a.exec();
}
