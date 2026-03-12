#include <QApplication>
#include <QFontDatabase>
#include <QDebug>

#include "core/global_context.h"
#include "core/types.h"
#include "hardware/hardware_manager.h"
#include "ui/app.h"
#include "ui/ui_controller.h"

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
    QApplication a(argc, argv);

    /* F**K U FFMPEG*/
    av_log_set_level(AV_LOG_FATAL);

    /* Mandatory for Signal/Slot data passing */
    qRegisterMetaType<RawFrame>();
    qRegisterMetaType<VisualFrame>();
    qRegisterMetaType<BatteryStatus>();
    qRegisterMetaType<CaptureMode>();

    loadLocalFont(FONT_PATH_ROBOTO);
    loadLocalFont(FONT_PATH_SANS);
    loadLocalFont(FONT_PATH_ICON);
    // loadLocalFont(FONT_PATH_ICON_F);

    QFont mainFont("Roboto");
    mainFont.setPixelSize(14);
    mainFont.setHintingPreference(QFont::PreferFullHinting);
    a.setFont(mainFont);

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

    /* User Interface */
    App w;
    w.showFullScreen();
    UIController::instance().init(&w);

    return a.exec();
}
