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
    AutoShutdown,
    ScreenBrightness,
    AudioVolume,
    Language,
    Clock,
    SystemTools,
    About,
    InitializeUserdata,
    CalibrateHapticMotor,
    SoftwareUpdate,
    RebootToLoader,
    RestoreDefaults,
    Count
};

/**
 * @brief Identity for one settings-item group.
 *
 * The primary menu exposes Camera through System; SystemTools is reached from
 * its System entry and rendered by the same item-list mechanism full screen.
 */
enum class SettingsSection { Camera, View, Storage, System, SystemTools };

/** @brief Business role governing activation for one settings item. */
enum class SettingsItemRole { Setting, Status, Command, Navigation };

/** @brief Presentation contract for an editable persisted setting. */
enum class SettingsEditor { Toggle, Stepper, Slider, Choice, Palette };

/** @brief Declarative visibility gate for one settings item. */
enum class SettingsItemVisibility {
    Always,
    RequiresSdCard,
    RequiresUsbDisk,
    RequiresLegacyMode,
    RequiresLegacyLinearAgc
};

/** @brief Immutable descriptor for one settings item, independent of its page layout. */
struct SettingsItemData {
    SettingID id;
    SettingsItemRole role;
    std::optional<SettingKey> settingKey;
    std::optional<SettingsEditor> editor;
    QString title;
    QColor titleColor = Qt::white;
    std::optional<SettingsSection> destinationSection;
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

    static std::vector<SettingsItemData> visibleItems(SettingsSection section,
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
        SettingsSection section,
        const QSet<SettingKey>& changedKeys);
    static bool sectionVisibilityAffectedByStorageState(SettingsSection section);
};

#endif // SETTINGS_CATALOG_H
