#ifndef SETTINGS_TYPES_H
#define SETTINGS_TYPES_H

#include <QColor>
#include <QString>
#include <vector>

/** --- Settings Item Typing --- */

/** @brief Canonical identity for every settings entry rendered by SettingsView. */
enum class SettingID {
    Palette,
    Emissivity,
    SeekVisionEnabled,
    LegacySharpenEnabled,
    AgcMode,
    LinearAgcMin,
    LinearAgcMax,
    ShutterAutoEnabled,
    TriggerFlatSceneCorrection,
    ThermographyOffset,
    TemperatureUnit,
    OSDOverlay,
    HideMarkerWhenHudHidden,
    StoragePriority,
    SdCardCapacity,
    SdCardSafeEject,
    SdCardFormat,
    UsbDiskCapacity,
    UsbDiskSafeEject,
    UsbDiskFormat,
    ScreenBrightness,
    AudioVolume,
    Clock
};

/** @brief Render/action contract for secondary row trailing affordance. */
enum class ActionType {
    Toggle,
    Value,
    Action
};

/** @brief Declarative visibility gate for one secondary entry. */
enum class SecondaryVisibility {
    Always,
    RequiresSdCard,
    RequiresUsbDisk,
    RequiresLegacyMode,
    RequiresLegacyLinearAgc
};

/** @brief Immutable descriptor for one secondary row item in the right panel. */
struct SecondaryItemData {
    SettingID id;
    QString title;
    QColor titleColor = Qt::white;
    ActionType type;
    SecondaryVisibility visibility = SecondaryVisibility::Always;
};

/** @brief Immutable descriptor for one primary category row and its secondary tree. */
struct PrimaryItemData {
    QString icon;
    QColor iconBgColor;
    QString title;
    std::vector<SecondaryItemData> subItems;
};

#endif // SETTINGS_TYPES_H
