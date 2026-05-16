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
    TemperatureUnit,
    OSDOverlay,
    ShutterCalibration,
    StorageFormat,
    DeviceInfo,
    FactoryReset
};

/** @brief Render/action contract for secondary row trailing affordance. */
enum class ActionType {
    Toggle,
    Value,
    Action
};

/** @brief Immutable descriptor for one secondary row item in the right panel. */
struct SecondaryItemData {
    SettingID id;
    QString title;
    ActionType type;
};

/** @brief Immutable descriptor for one primary category row and its secondary tree. */
struct PrimaryItemData {
    int navId = -1;
    QString icon;
    QColor iconBgColor;
    QString title;
    std::vector<SecondaryItemData> subItems;
};

#endif // SETTINGS_TYPES_H
