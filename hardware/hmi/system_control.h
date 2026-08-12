#ifndef SYSTEM_CONTROL_H
#define SYSTEM_CONTROL_H

#include <QObject>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <alsa/asoundlib.h>

/**
 * @brief Unified runtime controller for screen backlight and audio playback volume.
 *
 * Owns hardware-facing paths and ALSA mixer bindings used by SettingsService
 * runtime apply hooks. This class does not persist state.
 */
class SystemControl : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Creates one controller bound to the application lifecycle.
     */
    explicit SystemControl(QObject* parent = nullptr);

    /**
     * @brief Releases ALSA resources.
     */
    ~SystemControl() override;

    /**
     * @brief Initializes backlight and audio control endpoints.
     * @return true when both domains are ready.
     */
    bool init();

    /**
     * @brief Reads current backlight level and maps it to percent [0, 100].
     */
    bool screenBrightnessPercent(int* outPercent, QString* outError = nullptr) const;

    /**
     * @brief Sets backlight level from percent [0, 100].
     */
    bool setScreenBrightnessPercent(int percent, QString* outError = nullptr);

    /**
     * @brief Reads current playback volume and maps it to percent [0, 100].
     */
    bool audioVolumePercent(int* outPercent, QString* outError = nullptr) const;

    /**
     * @brief Sets playback volume from percent [0, 100].
     */
    bool setAudioVolumePercent(int percent, QString* outError = nullptr);

    /**
     * @brief Sets system time and writes it back to the RTC.
     */
    bool setSystemDateTime(const QDateTime& dateTime, QString* outError = nullptr);

private:
    /* ========================= Domain Initialization ========================= */
    bool initBacklight(QString* outError);
    bool initAudioMixer(QString* outError);

    /* ========================= Filesystem Helpers ========================= */
    bool readIntFile(const QString& path, int* outValue, QString* outError) const;
    bool writeIntFile(const QString& path, int value, QString* outError) const;
    bool runCommand(const QString& program,
                    const QStringList& arguments,
                    int timeoutMs,
                    QString* outError,
                    QString* outStdout = nullptr) const;

    /* ========================= ALSA Helpers ========================= */
    bool findPlaybackVolumeElementByName(const char* controlName,
                                         snd_mixer_elem_t** outElem,
                                         QString* outError) const;

    int clampPercent(int percent) const;

    QString m_brightnessPath;
    QString m_maxBrightnessPath;
    int m_maxBrightness = 0;
    bool m_backlightReady = false;

    snd_mixer_t* m_mixer = nullptr;
    snd_mixer_elem_t* m_dacDigitalElem = nullptr;
    long m_volumeMin = 0;
    long m_volumeMax = 0;
    bool m_audioReady = false;
};

#endif // SYSTEM_CONTROL_H
