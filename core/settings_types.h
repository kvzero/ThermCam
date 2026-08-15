#ifndef SETTINGS_RUNTIME_TYPES_H
#define SETTINGS_RUNTIME_TYPES_H

#include <QHash>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QVariant>
#include <array>
#include <cstddef>

/** --- Settings Runtime Typing --- */

/** @brief Strongly typed canonical keys for persisted settings domains. */
enum class SettingKey : quint8 {
    Palette,
    Emissivity,
    TemperatureUnit,
    StoragePriority,
    SaveMarkerInMedia,
    HideMarkerWhenHudHidden,
    ShutterAutoEnabled,
    ThermographyOffsetCelsius,
    SeekVisionEnabled,
    LegacySharpenEnabled,
    AgcMode,
    LinearAgcMinCelsius,
    LinearAgcMaxCelsius,
    ScreenBrightnessPercent,
    AudioVolumePercent,
    Count
};

/** @brief Typed unit selector used by service-level normalization paths. */
enum class TemperatureUnit : quint8 {
    Celsius = 0,
    Fahrenheit = 1
};

/** @brief Persisted removable-media write order used by StorageManager routing policy. */
enum class StoragePriority : quint8 {
    SdFirst = 0,
    UsbFirst = 1
};

/** @brief Runtime AGC control mode for camera-side contrast mapping. */
enum class AgcMode : quint8 {
    HistEqAuto = 0,
    LinearManual = 1
};

/** @brief Persisted payload category used for generic normalization. */
enum class SettingValueType : quint8 {
    Boolean,
    Integer,
    Float
};

/** @brief How a constrained integer setting handles an out-of-range input. */
enum class SettingIntegerRangePolicy : quint8 {
    Clamp,
    Reject
};

/** @brief Static metadata descriptor for one persisted setting domain entry. */
struct SettingDescriptor {
    SettingKey key;
    const char* jsonName;
    QVariant defaultValue;
    SettingValueType valueType;
    double minimum = 0.0;
    double maximum = 0.0;
    SettingIntegerRangePolicy integerRangePolicy = SettingIntegerRangePolicy::Clamp;
};

inline const std::array<SettingDescriptor, static_cast<size_t>(SettingKey::Count)> kSettingRegistry = {{
    {SettingKey::Palette, "palette", QVariant::fromValue(2), SettingValueType::Integer},
    {SettingKey::Emissivity, "emissivity", QVariant(0.95f), SettingValueType::Float,
     0.01, 1.0},
    {SettingKey::TemperatureUnit, "temperature_unit",
     QVariant::fromValue(static_cast<int>(TemperatureUnit::Celsius)),
     SettingValueType::Integer, 0, 1, SettingIntegerRangePolicy::Reject},
    {SettingKey::StoragePriority, "storage_priority",
     QVariant::fromValue(static_cast<int>(StoragePriority::SdFirst)),
     SettingValueType::Integer, 0, 1, SettingIntegerRangePolicy::Reject},
    {SettingKey::SaveMarkerInMedia, "save_marker_in_media", QVariant(true),
     SettingValueType::Boolean},
    {SettingKey::HideMarkerWhenHudHidden, "hide_marker_when_hud_hidden", QVariant(false),
     SettingValueType::Boolean},
    {SettingKey::ShutterAutoEnabled, "shutter_auto_enabled", QVariant(true),
     SettingValueType::Boolean},
    {SettingKey::ThermographyOffsetCelsius, "thermography_offset_celsius", QVariant(0.0f),
     SettingValueType::Float, -10.0, 10.0},
    {SettingKey::SeekVisionEnabled, "seekvision_enabled", QVariant(true),
     SettingValueType::Boolean},
    {SettingKey::LegacySharpenEnabled, "legacy_sharpen_enabled", QVariant(false),
     SettingValueType::Boolean},
    {SettingKey::AgcMode, "agc_mode",
     QVariant::fromValue(static_cast<int>(AgcMode::HistEqAuto)),
     SettingValueType::Integer, 0, 1, SettingIntegerRangePolicy::Reject},
    {SettingKey::LinearAgcMinCelsius, "linear_agc_min_celsius", QVariant(20.0f),
     SettingValueType::Float, -40.0, 600.0},
    {SettingKey::LinearAgcMaxCelsius, "linear_agc_max_celsius", QVariant(80.0f),
     SettingValueType::Float, -40.0, 600.0},
    {SettingKey::ScreenBrightnessPercent, "screen_brightness_percent", QVariant(80),
     SettingValueType::Integer, 1, 100},
    {SettingKey::AudioVolumePercent, "audio_volume_percent", QVariant(50),
     SettingValueType::Integer, 0, 100}
}};

inline const SettingDescriptor* settingDescriptorForKey(SettingKey key) {
    const size_t index = static_cast<size_t>(key);
    if (index >= kSettingRegistry.size()) return nullptr;
    return &kSettingRegistry[index];
}

/** @brief Immutable committed truth snapshot from SettingsStore. */
struct SettingsSnapshot {
    QHash<SettingKey, QVariant> values;
    quint64 revision = 0;
};

/** @brief Sparse write intent payload routed through SettingsService. */
struct SettingsPatch {
    QHash<SettingKey, QVariant> values;

    bool isEmpty() const { return values.isEmpty(); }
};

/** @brief Delta event broadcast after a successful persisted commit. */
struct SettingsChangeEvent {
    SettingsSnapshot snapshot;
    QSet<SettingKey> changedKeys;
};

inline uint qHash(SettingKey key, uint seed = 0) {
    return ::qHash(static_cast<quint8>(key), seed);
}

Q_DECLARE_METATYPE(SettingKey)
Q_DECLARE_METATYPE(TemperatureUnit)
Q_DECLARE_METATYPE(StoragePriority)
Q_DECLARE_METATYPE(AgcMode)
Q_DECLARE_METATYPE(SettingsSnapshot)
Q_DECLARE_METATYPE(SettingsPatch)
Q_DECLARE_METATYPE(SettingsChangeEvent)

#endif // SETTINGS_RUNTIME_TYPES_H
