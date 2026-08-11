#include "key_manager.h"

#include "core/event_bus.h"

#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QTimer>
#include <QDebug>

namespace {
QString findInputDevicePath(const QString& targetName) {
    QDir dir("/sys/class/input");
    QStringList filters;
    filters << "event*";

    const QFileInfoList list =
        dir.entryInfoList(filters, QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo& info : list) {
        QFile nameFile(info.absoluteFilePath() + "/device/name");
        if (!nameFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        const QString name = QString::fromUtf8(nameFile.readAll()).trimmed();
        if (name == targetName) {
            return QString("/dev/input/%1").arg(info.fileName());
        }
    }

    return QString();
}
} // namespace

KeyManager::KeyManager(QObject* parent)
    : QObject(parent),
      m_longPressTimer(new QTimer(this)) {
    m_longPressTimer->setSingleShot(true);
    connect(m_longPressTimer, &QTimer::timeout,
            this, &KeyManager::emitLongPress);
}

KeyManager::~KeyManager() {
    if (m_keyFd >= 0) {
        ::close(m_keyFd);
    }
}

bool KeyManager::init() {
    const QString keyPath = findInputDevicePath("adc-keys");
    if (keyPath.isEmpty()) {
        qWarning() << "[KeyManager] 'adc-keys' not found in sysfs.";
        return true;
    }

    m_keyFd = ::open(keyPath.toStdString().c_str(), O_RDONLY | O_NONBLOCK);
    if (m_keyFd < 0) {
        qCritical() << "[KeyManager] Failed to open key device:" << strerror(errno);
        return true;
    }

    qInfo() << "[KeyManager] ADC Keys bound to:" << keyPath;
    m_keyNotifier = new QSocketNotifier(m_keyFd, QSocketNotifier::Read, this);
    connect(m_keyNotifier, &QSocketNotifier::activated,
            this, &KeyManager::processKeyEvents);

    return true;
}

void KeyManager::processKeyEvents() {
    struct input_event ev;
    while (::read(m_keyFd, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type != EV_KEY) {
            continue;
        }

        if (ev.value == 1) {
            beginPress();
        } else if (ev.value == 0) {
            endPress();
        }
    }
}

void KeyManager::beginPress() {
    if (m_pressed) {
        return;
    }

    m_pressed = true;
    m_longPressEmitted = false;
    m_pressTimer.restart();
    m_longPressTimer->start(LONG_PRESS_MS);

    emit EventBus::instance().keyPressed();
}

void KeyManager::endPress() {
    if (!m_pressed) {
        return;
    }

    m_pressed = false;
    const qint64 heldMs = m_pressTimer.isValid() ? m_pressTimer.elapsed() : LONG_PRESS_MS;

    if (m_longPressTimer->isActive()) {
        m_longPressTimer->stop();
    }

    if (!m_longPressEmitted && heldMs < SHORT_PRESS_MAX_MS) {
        emit EventBus::instance().keyShortPressed();
    }
}

void KeyManager::emitLongPress() {
    if (!m_pressed || m_longPressEmitted) {
        return;
    }

    m_longPressEmitted = true;
    emit EventBus::instance().keyLongPressed();
}
