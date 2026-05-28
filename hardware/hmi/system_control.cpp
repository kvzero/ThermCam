#include "system_control.h"

#include <QFile>
#include <QIODevice>
#include <QByteArray>
#include <QDebug>
#include <QtGlobal>

#include <cstring>

namespace {
constexpr int kPercentMin = 0;
constexpr int kPercentMax = 100;

const char* kBacklightBrightnessPath = "/sys/class/backlight/backlight/brightness";
const char* kBacklightMaxPath = "/sys/class/backlight/backlight/max_brightness";
const char* kDacDigitalControlName = "DAC Digital";

QString alsaErrorText(int code) {
    return QString::fromLocal8Bit(snd_strerror(code));
}
} // namespace

SystemControl::SystemControl(QObject* parent)
    : QObject(parent),
      m_brightnessPath(QString::fromLatin1(kBacklightBrightnessPath)),
      m_maxBrightnessPath(QString::fromLatin1(kBacklightMaxPath)) {}

SystemControl::~SystemControl() {
    if (m_mixer) {
        snd_mixer_close(m_mixer);
        m_mixer = nullptr;
    }
}

bool SystemControl::init() {
    QString backlightError;
    QString audioError;

    m_backlightReady = initBacklight(&backlightError);
    m_audioReady = initAudioMixer(&audioError);

    if (!m_backlightReady) {
        qWarning() << "[SystemControl] Backlight init failed:" << backlightError;
    }
    if (!m_audioReady) {
        qWarning() << "[SystemControl] Audio init failed:" << audioError;
    }

    return m_backlightReady && m_audioReady;
}

bool SystemControl::screenBrightnessPercent(int* outPercent, QString* outError) const {
    if (!outPercent) {
        if (outError) *outError = "Null output pointer";
        return false;
    }

    if (!m_backlightReady || m_maxBrightness <= 0) {
        if (outError) *outError = "Backlight is not initialized";
        return false;
    }

    int rawBrightness = 0;
    if (!readIntFile(m_brightnessPath, &rawBrightness, outError)) {
        return false;
    }

    rawBrightness = qBound(0, rawBrightness, m_maxBrightness);
    const int percent = qRound((static_cast<double>(rawBrightness) * 100.0) /
                               static_cast<double>(m_maxBrightness));
    *outPercent = clampPercent(percent);
    return true;
}

bool SystemControl::setScreenBrightnessPercent(int percent, QString* outError) {
    if (!m_backlightReady || m_maxBrightness <= 0) {
        if (outError) *outError = "Backlight is not initialized";
        return false;
    }

    const int clampedPercent = clampPercent(percent);
    const int rawBrightness = qBound(
        0,
        qRound((static_cast<double>(clampedPercent) / 100.0) *
               static_cast<double>(m_maxBrightness)),
        m_maxBrightness);

    return writeIntFile(m_brightnessPath, rawBrightness, outError);
}

bool SystemControl::audioVolumePercent(int* outPercent, QString* outError) const {
    if (!outPercent) {
        if (outError) *outError = "Null output pointer";
        return false;
    }

    if (!m_audioReady || !m_dacDigitalElem || (m_volumeMax <= m_volumeMin)) {
        if (outError) *outError = "Audio mixer is not initialized";
        return false;
    }

    snd_mixer_selem_channel_id_t channel = SND_MIXER_SCHN_UNKNOWN;
    if (snd_mixer_selem_has_playback_channel(m_dacDigitalElem, SND_MIXER_SCHN_FRONT_LEFT)) {
        channel = SND_MIXER_SCHN_FRONT_LEFT;
    } else if (snd_mixer_selem_has_playback_channel(m_dacDigitalElem, SND_MIXER_SCHN_MONO)) {
        channel = SND_MIXER_SCHN_MONO;
    }

    if (channel == SND_MIXER_SCHN_UNKNOWN) {
        if (outError) *outError = "Playback channel is unavailable";
        return false;
    }

    long rawVolume = 0;
    const int rc = snd_mixer_selem_get_playback_volume(m_dacDigitalElem, channel, &rawVolume);
    if (rc < 0) {
        if (outError) {
            *outError = QString("Failed to read playback volume: %1")
                            .arg(alsaErrorText(rc));
        }
        return false;
    }

    rawVolume = qBound(m_volumeMin, rawVolume, m_volumeMax);
    const double ratio =
        static_cast<double>(rawVolume - m_volumeMin) /
        static_cast<double>(m_volumeMax - m_volumeMin);
    const int percent = qRound(ratio * 100.0);
    *outPercent = clampPercent(percent);
    return true;
}

bool SystemControl::setAudioVolumePercent(int percent, QString* outError) {
    if (!m_audioReady || !m_dacDigitalElem || (m_volumeMax <= m_volumeMin)) {
        if (outError) *outError = "Audio mixer is not initialized";
        return false;
    }

    const int clampedPercent = clampPercent(percent);
    const long range = m_volumeMax - m_volumeMin;
    const long rawVolume = qBound(
        m_volumeMin,
        m_volumeMin + qRound((static_cast<double>(clampedPercent) / 100.0) *
                             static_cast<double>(range)),
        m_volumeMax);

    const int rc = snd_mixer_selem_set_playback_volume_all(m_dacDigitalElem, rawVolume);
    if (rc < 0) {
        if (outError) {
            *outError = QString("Failed to set playback volume: %1")
                            .arg(alsaErrorText(rc));
        }
        return false;
    }

    return true;
}

bool SystemControl::initBacklight(QString* outError) {
    int maxBrightness = 0;
    if (!readIntFile(m_maxBrightnessPath, &maxBrightness, outError)) {
        return false;
    }

    if (maxBrightness <= 0) {
        if (outError) {
            *outError = QString("Invalid max brightness value: %1")
                            .arg(maxBrightness);
        }
        return false;
    }

    m_maxBrightness = maxBrightness;
    return true;
}

bool SystemControl::initAudioMixer(QString* outError) {
    if (m_mixer) {
        snd_mixer_close(m_mixer);
        m_mixer = nullptr;
    }
    m_dacDigitalElem = nullptr;
    m_volumeMin = 0;
    m_volumeMax = 0;

    int rc = snd_mixer_open(&m_mixer, 0);
    if (rc < 0) {
        if (outError) {
            *outError = QString("snd_mixer_open failed: %1").arg(alsaErrorText(rc));
        }
        return false;
    }

    rc = snd_mixer_attach(m_mixer, "default");
    if (rc < 0) {
        if (outError) {
            *outError = QString("snd_mixer_attach failed: %1").arg(alsaErrorText(rc));
        }
        snd_mixer_close(m_mixer);
        m_mixer = nullptr;
        return false;
    }

    rc = snd_mixer_selem_register(m_mixer, nullptr, nullptr);
    if (rc < 0) {
        if (outError) {
            *outError = QString("snd_mixer_selem_register failed: %1")
                            .arg(alsaErrorText(rc));
        }
        snd_mixer_close(m_mixer);
        m_mixer = nullptr;
        return false;
    }

    rc = snd_mixer_load(m_mixer);
    if (rc < 0) {
        if (outError) {
            *outError = QString("snd_mixer_load failed: %1").arg(alsaErrorText(rc));
        }
        snd_mixer_close(m_mixer);
        m_mixer = nullptr;
        return false;
    }

    if (!findPlaybackVolumeElementByName(kDacDigitalControlName, &m_dacDigitalElem, outError)) {
        snd_mixer_close(m_mixer);
        m_mixer = nullptr;
        return false;
    }

    snd_mixer_selem_get_playback_volume_range(m_dacDigitalElem, &m_volumeMin, &m_volumeMax);
    if (m_volumeMax <= m_volumeMin) {
        if (outError) {
            *outError = QString("Invalid playback range: %1..%2")
                            .arg(m_volumeMin)
                            .arg(m_volumeMax);
        }
        snd_mixer_close(m_mixer);
        m_mixer = nullptr;
        m_dacDigitalElem = nullptr;
        return false;
    }

    return true;
}

bool SystemControl::readIntFile(const QString& path, int* outValue, QString* outError) const {
    if (!outValue) {
        if (outError) *outError = "Null output pointer";
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (outError) {
            *outError = QString("Failed to open %1 for read").arg(path);
        }
        return false;
    }

    bool ok = false;
    const int value = QString::fromUtf8(file.readAll()).trimmed().toInt(&ok);
    if (!ok) {
        if (outError) {
            *outError = QString("Failed to parse integer from %1").arg(path);
        }
        return false;
    }

    *outValue = value;
    return true;
}

bool SystemControl::writeIntFile(const QString& path, int value, QString* outError) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (outError) {
            *outError = QString("Failed to open %1 for write").arg(path);
        }
        return false;
    }

    const QByteArray payload = QByteArray::number(value);
    const qint64 written = file.write(payload);
    if (written != payload.size()) {
        if (outError) {
            *outError = QString("Failed to write integer to %1").arg(path);
        }
        return false;
    }

    return true;
}

bool SystemControl::findPlaybackVolumeElementByName(const char* controlName,
                                                    snd_mixer_elem_t** outElem,
                                                    QString* outError) const {
    if (!outElem) {
        if (outError) *outError = "Null mixer element output pointer";
        return false;
    }

    for (snd_mixer_elem_t* elem = snd_mixer_first_elem(m_mixer);
         elem;
         elem = snd_mixer_elem_next(elem)) {
        if (!snd_mixer_selem_is_active(elem)) continue;
        if (!snd_mixer_selem_has_playback_volume(elem)) continue;

        const char* name = snd_mixer_selem_get_name(elem);
        if (name && (std::strcmp(name, controlName) == 0)) {
            *outElem = elem;
            return true;
        }
    }

    if (outError) {
        *outError = QString("Playback control not found: %1")
                        .arg(QString::fromLatin1(controlName));
    }
    return false;
}

int SystemControl::clampPercent(int percent) const {
    return qBound(kPercentMin, percent, kPercentMax);
}
