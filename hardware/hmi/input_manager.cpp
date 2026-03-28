#include "input_manager.h"
#include "core/event_bus.h"
#include "core/global_context.h"
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QDebug>

namespace {
/**
 * @brief Dynamically locates the input event device path by its sysfs name.
 *
 * Bypasses udev symlinks to enable ultra-fast initialization during early boot.
 *
 * @param targetName The sysfs name of the device (e.g., "adc-keys").
 * @return Absolute path to the device node, or an empty string if not found.
 */
QString findInputDevicePath(const QString& targetName) {
    QDir dir("/sys/class/input");
    QStringList filters;
    filters << "event*";

    QFileInfoList list = dir.entryInfoList(filters, QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo& info : list) {
        QFile nameFile(info.absoluteFilePath() + "/device/name");
        if (nameFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString name = QString::fromUtf8(nameFile.readAll()).trimmed();
            nameFile.close();

            if (name == targetName) {
                return QString("/dev/input/%1").arg(info.fileName());
            }
        }
    }
    return QString();
}
} // anonymous namespace


InputManager::InputManager(QObject* parent) : QObject(parent) {
    m_keyFd = -1;
    m_touchFd = -1;
}

InputManager::~InputManager() {
    if (m_keyFd >= 0) ::close(m_keyFd);
    if (m_touchFd >= 0) ::close(m_touchFd);
}

bool InputManager::init() {
    parseEnv();

    /* Step 1: Probe and open the physical key device */
    QString keyPath = findInputDevicePath("adc-keys");
    if (!keyPath.isEmpty()) {
        m_keyFd = ::open(keyPath.toStdString().c_str(), O_RDONLY | O_NONBLOCK);
        if (m_keyFd >= 0) {
            qInfo() << "[InputManager] ADC Keys bound to:" << keyPath;
        } else {
            qCritical() << "[InputManager] Failed to open key device:" << strerror(errno);
        }
    } else {
        qWarning() << "[InputManager] 'adc-keys' not found in sysfs.";
    }

    /* Step 2: Probe and open the touch screen device */
    QString touchPath = findInputDevicePath("fts_ts");
    if (!touchPath.isEmpty()) {
        m_touchFd = ::open(touchPath.toStdString().c_str(), O_RDONLY | O_NONBLOCK);
        if (m_touchFd >= 0) {
            qInfo() << "[InputManager] Touch screen bound to:" << touchPath;
        } else {
            qCritical() << "[InputManager] Failed to open touch device:" << strerror(errno);
        }
    } else {
        qWarning() << "[InputManager] 'fts_ts' not found in sysfs.";
    }

    /* Step 3: Configure touch device exclusivity and limits */
    if (m_touchFd >= 0) {
        if (::ioctl(m_touchFd, EVIOCGRAB, (void*)1) < 0) {
            qWarning() << "[InputManager] Failed to grab touch device exclusively:" << strerror(errno);
        } else {
            qInfo() << "[InputManager] Touch device grabbed exclusively.";
        }

        if (getAbsLimits()) {
            m_touchNotifier = new QSocketNotifier(m_touchFd, QSocketNotifier::Read, this);
            connect(m_touchNotifier, &QSocketNotifier::activated, this, &InputManager::processTouchEvents);
        } else {
            qWarning() << "[InputManager] Failed to read absolute limits from touch device.";
        }
    }

    /* Step 4: Setup key event notifier */
    if (m_keyFd >= 0) {
        m_keyNotifier = new QSocketNotifier(m_keyFd, QSocketNotifier::Read, this);
        connect(m_keyNotifier, &QSocketNotifier::activated, this, &InputManager::processKeyEvents);
    }

    /* Return true regardless of input availability to allow UI rendering to proceed */
    return true;
}


bool InputManager::getAbsLimits() {
    struct input_absinfo absInfo;
    if (ioctl(m_touchFd, EVIOCGABS(ABS_MT_POSITION_X), &absInfo) < 0) return false;
    m_absX = {absInfo.minimum, absInfo.maximum};

    if (ioctl(m_touchFd, EVIOCGABS(ABS_MT_POSITION_Y), &absInfo) < 0) return false;
    m_absY = {absInfo.minimum, absInfo.maximum};

    return true;
}

void InputManager::processKeyEvents() {
    struct input_event ev;
    while (::read(m_keyFd, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type == EV_KEY) {
            // High-level decision making is handled by InteractionArbiter
            emit EventBus::instance().rawKeySignal(ev.value != 0);
        }
    }
}

void InputManager::processTouchEvents() {
    struct input_event ev;
    bool syncTriggered = false;

    while (::read(m_touchFd, &ev, sizeof(ev)) == sizeof(ev)) {
        switch (ev.type) {
            case EV_ABS:
                if (ev.code == ABS_MT_SLOT) m_currentSlot = ev.value;
                else if (ev.code == ABS_MT_POSITION_X) m_slots[m_currentSlot].x = ev.value;
                else if (ev.code == ABS_MT_POSITION_Y) m_slots[m_currentSlot].y = ev.value;
                else if (ev.code == ABS_MT_TRACKING_ID) {
                    if (ev.value == -1) m_slots[m_currentSlot].active = false;
                    else {
                        m_slots[m_currentSlot].active = true;
                        m_slots[m_currentSlot].id = ev.value;
                    }
                }
                break;

            case EV_SYN:
                if (ev.code == SYN_REPORT) syncTriggered = true;
                break;
        }
    }

    if (syncTriggered) {
        // Emit the current state of all touch slots as a frame
        QList<RawTouchPoint> frame;

        auto i = m_slots.constBegin();
        while (i != m_slots.constEnd()) {
            if (i.value().active) {
                RawTouchPoint p = i.value();
                QPoint screenPos = mapToScreen(p.x, p.y);
                p.x = screenPos.x();
                p.y = screenPos.y();
                frame.append(p);
            }
            ++i;
        }

        emit EventBus::instance().rawTouchSignal(frame);
    }
}

void InputManager::parseEnv() {
    // For export QT_QPA_PLATFORM=linuxfb:rotation=90
    QByteArray env = qgetenv("QT_QPA_PLATFORM");
    QStringList params = QString::fromLatin1(env).split(':');
    for (const QString& param : params) {
        if (param.startsWith("rotation=")) {
            m_rotationAngle = param.mid(9).toInt();
            qInfo() << "InputManager: Rotation set to" << m_rotationAngle;
        }
    }
}

QPoint InputManager::mapToScreen(int rawX, int rawY) {
    auto s = GlobalContext::instance().screenSize();
    if (s.isEmpty()) return QPoint(rawX, rawY);

    float percentX = (float)(rawX - m_absX.min) / (m_absX.max - m_absX.min);
    float percentY = (float)(rawY - m_absY.min) / (m_absY.max - m_absY.min);

    int sx = 0, sy = 0;

    switch(m_rotationAngle) {
        case 270:
            sx = s.width() * (1.0 - percentY);
            sy = s.height() * percentX;
            break;
        case 180:
            sx = s.width() * (1.0 - percentX);
            sy = s.height() * (1.0 - percentY);
            break;
        case 90:
            sx = s.width() * percentY;
            sy = s.height() * (1.0 - percentX);
            break;
        default: // 0
            sx = s.width() * percentX;
            sy = s.height() * percentY;
            break;
    }
    return QPoint(sx, sy);
}
