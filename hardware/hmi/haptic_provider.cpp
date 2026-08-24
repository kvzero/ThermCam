#include "haptic_provider.h"
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>
#include <QDir>
#include <QFileInfo>
#include <QFile>

namespace {
const QString kCalibratePath =
    QStringLiteral("/sys/bus/i2c/devices/1-005a/calibrate");
const QString kCalibrationPath =
    QStringLiteral("/sys/bus/i2c/devices/1-005a/calibration");
const QString kSavedCalibrationPath =
    QStringLiteral("/userdata/thermal_qt/haptic_calibration");

bool writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write(data) == data.size();
}

bool performCalibration() {
    if (!writeFile(kCalibratePath, QByteArrayLiteral("1"))) return false;

    QFile calibration(kCalibrationPath);
    if (!calibration.open(QIODevice::ReadOnly)) return false;
    const QByteArray data = calibration.readAll();
    if (data.isEmpty()) return false;

    QSaveFile saved(kSavedCalibrationPath);
    if (!saved.open(QIODevice::WriteOnly)) return false;
    if (saved.write(data) != data.size()) return false;
    return saved.commit();
}

/**
 * @brief Dynamically locates the input event device path by its sysfs name.
 *
 * This bypasses the need for udev symlinks, allowing for faster initialization
 * during early boot stages before the udev daemon is fully operational.
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


HapticProvider& HapticProvider::instance() {
    static HapticProvider inst;
    return inst;
}

HapticProvider::HapticProvider(QObject* parent) : QObject(parent) {}

HapticProvider::~HapticProvider() {
    if (m_calibrationFuture.isRunning()) {
        m_calibrationFuture.waitForFinished();
    }
    if (m_fd >= 0) ::close(m_fd);
}

bool HapticProvider::init() {
    QString hapticPath = findInputDevicePath("drv260x:haptics");

    if (hapticPath.isEmpty()) {
        qCritical() << "[HapticProvider] Failed to locate 'drv260x:haptics' in sysfs.";
        return false;
    }

    m_fd = ::open(hapticPath.toStdString().c_str(), O_RDWR | O_NONBLOCK);
    if (m_fd < 0) {
        qCritical() << "[HapticProvider] Failed to open device node:" << hapticPath
                    << "-" << strerror(errno);
        return false;
    }

    if (!restoreSavedCalibration()) {
        qWarning() << "[HapticProvider] Failed to restore saved calibration.";
    }

    qInfo() << "[HapticProvider] Successfully bound to device:" << hapticPath;
    return true;
}

bool HapticProvider::startCalibration() {
    if (m_fd < 0 || m_calibrationRunning || m_calibrationFuture.isRunning()) return false;

    m_calibrationRunning = true;
    const QPointer<HapticProvider> self(this);
    m_calibrationFuture = QtConcurrent::run([self]() {
        const bool success = performCalibration();
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, success]() {
            if (!self) return;
            self->m_calibrationRunning = false;
            emit self->calibrationFinished(success);
        }, Qt::QueuedConnection);
    });
    return true;
}

void HapticProvider::playEffect(int id) {
    playSequence({id});
}

void HapticProvider::playSequence(const std::vector<int>& ids) {
    if (m_fd < 0 || m_calibrationRunning || ids.empty()) return;

    struct ff_effect effect;
    std::memset(&effect, 0, sizeof(effect));

    effect.type = FF_RUMBLE;
    effect.id = m_effectId;      /* Use previously allocated ID if exists, otherwise -1 */
    effect.replay.length = 500;  /* Nominal duration to prevent kernel timer interference */
    effect.replay.delay = 0;

    /**
     * INTERNAL KERNEL LOGIC EXPLANATION:
     * The custom kernel driver's 'drv260x_haptics_play' function intercepts
     * the rumble magnitudes and extracts 8-bit IDs instead of motor intensity.
     */
    effect.u.rumble.strong_magnitude = 0;
    effect.u.rumble.weak_magnitude = 0;

    /* Pack IDs into 16-bit fields for the kernel driver to unpack */
    if (ids.size() > 0) effect.u.rumble.strong_magnitude |= (ids[0] & 0xFF);
    if (ids.size() > 1) effect.u.rumble.strong_magnitude |= ((ids[1] & 0xFF) << 8);
    if (ids.size() > 2) effect.u.rumble.weak_magnitude   |= (ids[2] & 0xFF);
    if (ids.size() > 3) effect.u.rumble.weak_magnitude   |= ((ids[3] & 0xFF) << 8);

    /* Step 1: Upload the "ID-packed" effect to kernel memory */
    if (::ioctl(m_fd, EVIOCSFF, &effect) < 0) {
        qWarning() << "HapticProvider: Effect upload failed (ioctl EVIOCSFF)";
        return;
    }

    /* Sync local effect ID with the one assigned by the kernel */
    m_effectId = effect.id;

    /* Step 2: Trigger playback via a standard Force Feedback event */
    struct input_event play;
    std::memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = m_effectId;
    play.value = 1; /* Start flag */

    if (::write(m_fd, &play, sizeof(play)) < 0) {
        qWarning() << "HapticProvider: Trigger failed (write EV_FF)";
    }
}

bool HapticProvider::restoreSavedCalibration() {
    QFile saved(kSavedCalibrationPath);
    if (!saved.exists()) return true;
    if (!saved.open(QIODevice::ReadOnly)) return false;

    const QByteArray data = saved.readAll();
    return !data.isEmpty() && writeFile(kCalibrationPath, data);
}
