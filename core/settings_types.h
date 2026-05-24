#ifndef SETTINGS_RUNTIME_TYPES_H
#define SETTINGS_RUNTIME_TYPES_H

#include <QHash>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QVariant>
#include <array>

/** --- Settings Runtime Typing --- */

/** @brief Strongly typed canonical keys for persisted settings domains. */
enum class SettingKey : quint8 {
    Palette = 0,
    Emissivity = 1,
    TemperatureUnit = 2,
    StoragePriority = 3
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

/** @brief Static metadata descriptor for one persisted setting domain entry. */
struct SettingDescriptor {
    SettingKey key;
    const char* jsonName;
    QVariant defaultValue;
};

inline const std::array<SettingDescriptor, 4> kSettingRegistry = {{
    {SettingKey::Palette, "palette", QVariant::fromValue(2)},
    {SettingKey::Emissivity, "emissivity", QVariant(0.95f)},
    {SettingKey::TemperatureUnit, "temperature_unit",
     QVariant::fromValue(static_cast<int>(TemperatureUnit::Celsius))},
    {SettingKey::StoragePriority, "storage_priority",
     QVariant::fromValue(static_cast<int>(StoragePriority::SdFirst))}
}};

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
Q_DECLARE_METATYPE(SettingsSnapshot)
Q_DECLARE_METATYPE(SettingsPatch)
Q_DECLARE_METATYPE(SettingsChangeEvent)

#endif // SETTINGS_RUNTIME_TYPES_H
