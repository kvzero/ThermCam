#ifndef SETTINGS_CATALOG_H
#define SETTINGS_CATALOG_H

#include "core/settings_types.h"

#include <QColor>
#include <QSet>
#include <QString>
#include <QVector>
#include <optional>
#include <vector>

/** @brief Canonical identity for every settings entry rendered by SettingsView. */
enum class SettingID {
    Emissivity,
    ShutterAutoEnabled,
    SeekVisionEnabled,
    LegacySharpenEnabled,
    AgcMode,
    LinearAgcMin,
    LinearAgcMax,
    ThermographyOffset,
    TriggerFlatSceneCorrection,
    Palette,
    TemperatureUnit,
    OSDOverlay,
    HideMarkerWhenHudHidden,
    StoragePriority,
    InternalStorageCapacity,
    SdCardCapacity,
    SdCardSafeEject,
    SdCardFormat,
    UsbDiskCapacity,
    UsbDiskSafeEject,
    UsbDiskFormat,
    ScreenBrightness,
    AudioVolume,
    Clock,
    RestoreDefaults,
    Count
};

/** @brief Settings page identity used by the menu catalog. */
enum class SettingsSection { Camera, View, Storage, System };

/** @brief Render/action contract for secondary row trailing affordance. */
enum class ActionType { Toggle, Value, Action };

/** @brief Editor contract selected by SettingsView for one item. */
enum class SettingsEditor { Toggle, Stepper, Slider, Choice, Action };

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
    std::optional<SettingKey> settingKey;
    QString title;
    QColor titleColor = Qt::white;
    ActionType type;
    SettingsEditor editor;
};

/** @brief Immutable descriptor for one primary category row. */
struct PrimaryItemData {
    QString icon;
    QColor iconBgColor;
    QString title;
};

/** @brief Numeric editor parameters resolved from one settings catalog item. */
struct SettingsNumberEditor {
    double minimum = 0.0;
    double maximum = 0.0;
    double step = 1.0;
    double value = 0.0;
    int previewThrottleMs = 0;
    bool dismissOnCommit = false;
    QString iconGlyph;
};

/** @brief One stable value option for a choice editor. */
struct SettingsChoiceOption {
    QString id;
    QString title;
    int value = 0;
};

/** @brief Choice editor parameters resolved from one settings catalog item. */
struct SettingsChoiceEditor {
    QVector<SettingsChoiceOption> options;
    int selectedIndex = 0;
};

/**
 * @brief Owns the stable settings-menu catalog and resolves its display data.
 *
 * The catalog is the only owner of section membership, display order, labels,
 * editor configuration, and row visibility.
 */
class SettingsCatalog final {
public:
    static int sectionCount();
    static PrimaryItemData sectionAt(int index);
    static QString sectionTitle(int index);
    static int sectionIndexForItem(SettingID item);

    static std::vector<SecondaryItemData> visibleItems(int sectionIndex,
                                                        const SettingsSnapshot& snapshot,
                                                        bool sdCardReady,
                                                        bool usbDiskReady);

    static SettingsNumberEditor numberEditor(SettingID item,
                                              const SettingsSnapshot& snapshot);
    static SettingsChoiceEditor choiceEditor(SettingID item,
                                              const SettingsSnapshot& snapshot);
    static QVariant numberValue(SettingID item, double value);
    static QVariant choiceValue(SettingID item, int optionIndex);
    static QString editedValueText(SettingID item, double value);
    static QString valueText(SettingID item, const SettingsSnapshot& snapshot);

    static bool sectionVisibilityAffectedBySettingsChange(
        int sectionIndex,
        const QSet<SettingKey>& changedKeys);
    static bool sectionVisibilityAffectedByStorageState(int sectionIndex);
};

#endif // SETTINGS_CATALOG_H
