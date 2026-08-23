#include "ui/settings_catalog.h"

#include <QCoreApplication>

#include <array>

namespace {

enum class NumberFormat { Fixed2, SignedCelsius0, SignedCelsius1, Percent };
enum class PeerBound { None, BelowPeer, AbovePeer };

struct NumberSpec {
    double step = 1.0;
    NumberFormat format = NumberFormat::Fixed2;
    PeerBound peerBound = PeerBound::None;
    std::optional<SettingKey> peerKey;
    uint iconCodepoint = 0;
    int previewThrottleMs = 0;
    bool dismissOnCommit = false;

    constexpr NumberSpec(double step = 1.0,
                         NumberFormat format = NumberFormat::Fixed2,
                         PeerBound peerBound = PeerBound::None,
                         std::optional<SettingKey> peerKey = std::nullopt,
                         uint iconCodepoint = 0,
                         int previewThrottleMs = 0,
                         bool dismissOnCommit = false)
        : step(step), format(format), peerBound(peerBound), peerKey(peerKey),
          iconCodepoint(iconCodepoint), previewThrottleMs(previewThrottleMs),
          dismissOnCommit(dismissOnCommit) {}

};

struct ChoiceSpec {
    const char* id;
    const char* title;
    int value;
};

struct ItemSpec {
    SettingsSection section;
    SettingsItemRole role;
    std::optional<SettingsSection> destinationSection;
    std::optional<SettingKey> settingKey;
    std::optional<SettingsEditor> editor;
    const char* title;
    QRgb titleColor;
    SettingsItemVisibility visibility = SettingsItemVisibility::Always;
    NumberSpec number;
    const ChoiceSpec* choices = nullptr;
    int choiceCount = 0;

    constexpr ItemSpec(SettingsSection section,
                       SettingsItemRole role,
                       std::optional<SettingKey> settingKey,
                       std::optional<SettingsEditor> editor,
                       const char* title,
                       QRgb titleColor,
                       SettingsItemVisibility visibility = SettingsItemVisibility::Always,
                       NumberSpec number = {},
                       const ChoiceSpec* choices = nullptr,
                       int choiceCount = 0,
                       std::optional<SettingsSection> destinationSection = std::nullopt)
        : section(section), role(role), destinationSection(destinationSection),
          settingKey(settingKey), editor(editor), title(title), titleColor(titleColor),
          visibility(visibility),
          number(number), choices(choices), choiceCount(choiceCount) {}
};

struct SectionSpec {
    uint iconCodepoint;
    QRgb iconColor;
    const char* title;
};

constexpr ChoiceSpec kAgcChoices[] = {
    {"auto_histeq", QT_TRANSLATE_NOOP("SettingsCatalog", "Auto (HistEQ AGC)"),
     static_cast<int>(AgcMode::HistEqAuto)},
    {"linear_manual", QT_TRANSLATE_NOOP("SettingsCatalog", "Linear"),
     static_cast<int>(AgcMode::LinearManual)},
};

constexpr ChoiceSpec kTemperatureUnitChoices[] = {
    {"celsius", "°C", static_cast<int>(TemperatureUnit::Celsius)},
    {"fahrenheit", "°F", static_cast<int>(TemperatureUnit::Fahrenheit)},
};

constexpr ChoiceSpec kStoragePriorityChoices[] = {
    {"sd_first", QT_TRANSLATE_NOOP("SettingsCatalog", "SD Card First"),
     static_cast<int>(StoragePriority::SdFirst)},
    {"usb_first", QT_TRANSLATE_NOOP("SettingsCatalog", "USB Disk First"),
     static_cast<int>(StoragePriority::UsbFirst)},
};

constexpr ChoiceSpec kAutoShutdownChoices[] = {
    {"never", QT_TRANSLATE_NOOP("SettingsCatalog", "Never"),
     static_cast<int>(AutoShutdownTimeout::Never)},
    {"5_minutes", QT_TRANSLATE_NOOP("SettingsCatalog", "5 Minutes"),
     static_cast<int>(AutoShutdownTimeout::FiveMinutes)},
    {"15_minutes", QT_TRANSLATE_NOOP("SettingsCatalog", "15 Minutes"),
     static_cast<int>(AutoShutdownTimeout::FifteenMinutes)},
    {"30_minutes", QT_TRANSLATE_NOOP("SettingsCatalog", "30 Minutes"),
     static_cast<int>(AutoShutdownTimeout::ThirtyMinutes)},
    {"60_minutes", QT_TRANSLATE_NOOP("SettingsCatalog", "60 Minutes"),
     static_cast<int>(AutoShutdownTimeout::SixtyMinutes)},
};

constexpr ChoiceSpec kAppLanguageChoices[] = {
    {"english", QT_TRANSLATE_NOOP("SettingsCatalog", "English"),
     static_cast<int>(AppLanguage::English)},
    {"simplified_chinese", QT_TRANSLATE_NOOP("SettingsCatalog", "Simplified Chinese"),
     static_cast<int>(AppLanguage::SimplifiedChinese)},
};

constexpr std::array<ItemSpec, static_cast<size_t>(SettingID::Count)> kItems = {{
    {SettingsSection::Camera, SettingsItemRole::Setting, SettingKey::Emissivity,
     SettingsEditor::Stepper, QT_TRANSLATE_NOOP("SettingsCatalog", "Emissivity"), 0xffffffff,
     SettingsItemVisibility::Always,
     {0.01, NumberFormat::Fixed2}},
    {SettingsSection::Camera, SettingsItemRole::Setting, SettingKey::ShutterAutoEnabled,
     SettingsEditor::Toggle, QT_TRANSLATE_NOOP("SettingsCatalog", "Auto Shutter"), 0xffffffff},
    {SettingsSection::Camera, SettingsItemRole::Setting, SettingKey::SeekVisionEnabled,
     SettingsEditor::Toggle, QT_TRANSLATE_NOOP("SettingsCatalog", "Auto SeekVision"), 0xffffffff},
    {SettingsSection::Camera, SettingsItemRole::Setting, SettingKey::LegacySharpenEnabled,
     SettingsEditor::Toggle, QT_TRANSLATE_NOOP("SettingsCatalog", "Sharpen Filter"), 0xffffffff,
     SettingsItemVisibility::RequiresLegacyMode},
    {SettingsSection::Camera, SettingsItemRole::Setting, SettingKey::AgcMode,
     SettingsEditor::Choice, QT_TRANSLATE_NOOP("SettingsCatalog", "AGC Mode"), 0xffffffff,
     SettingsItemVisibility::RequiresLegacyMode, {}, kAgcChoices, int(std::size(kAgcChoices))},
    {SettingsSection::Camera, SettingsItemRole::Setting, SettingKey::LinearAgcMinCelsius,
     SettingsEditor::Stepper, QT_TRANSLATE_NOOP("SettingsCatalog", "- Linear AGC Min"), 0xffffffff,
     SettingsItemVisibility::RequiresLegacyLinearAgc,
     {1.0, NumberFormat::SignedCelsius0, PeerBound::BelowPeer,
      SettingKey::LinearAgcMaxCelsius}},
    {SettingsSection::Camera, SettingsItemRole::Setting, SettingKey::LinearAgcMaxCelsius,
     SettingsEditor::Stepper, QT_TRANSLATE_NOOP("SettingsCatalog", "- Linear AGC Max"), 0xffffffff,
     SettingsItemVisibility::RequiresLegacyLinearAgc,
     {1.0, NumberFormat::SignedCelsius0, PeerBound::AbovePeer,
      SettingKey::LinearAgcMinCelsius}},
    {SettingsSection::Camera, SettingsItemRole::Setting, SettingKey::ThermographyOffsetCelsius,
     SettingsEditor::Stepper, QT_TRANSLATE_NOOP("SettingsCatalog", "Temperature Offset"),
     0xffffffff, SettingsItemVisibility::Always,
     {0.1, NumberFormat::SignedCelsius1}},
    {SettingsSection::Camera, SettingsItemRole::Command, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "Flat-Scene Correction"), 0xffe44848},

    {SettingsSection::View, SettingsItemRole::Setting, SettingKey::Palette,
     SettingsEditor::Palette, QT_TRANSLATE_NOOP("SettingsCatalog", "Palette"), 0xffffffff},
    {SettingsSection::View, SettingsItemRole::Setting, SettingKey::SaveMarkerInMedia,
     SettingsEditor::Toggle, QT_TRANSLATE_NOOP("SettingsCatalog", "Save Marker Overlay"),
     0xffffffff},
    {SettingsSection::View, SettingsItemRole::Setting, SettingKey::HideMarkerWhenHudHidden,
     SettingsEditor::Toggle, QT_TRANSLATE_NOOP("SettingsCatalog", "Hide Marker with HUD"),
     0xffffffff},
    {SettingsSection::View, SettingsItemRole::Setting, SettingKey::TemperatureUnit,
     SettingsEditor::Choice, QT_TRANSLATE_NOOP("SettingsCatalog", "Temperature Unit"), 0xffffffff,
     SettingsItemVisibility::Always, {}, kTemperatureUnitChoices,
     int(std::size(kTemperatureUnitChoices))},

    {SettingsSection::Storage, SettingsItemRole::Setting, SettingKey::StoragePriority,
     SettingsEditor::Choice, QT_TRANSLATE_NOOP("SettingsCatalog", "Priority"), 0xffffffff,
     SettingsItemVisibility::Always, {}, kStoragePriorityChoices,
     int(std::size(kStoragePriorityChoices))},
    {SettingsSection::Storage, SettingsItemRole::Status, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "Internal"), 0xffffffff},
    {SettingsSection::Storage, SettingsItemRole::Status, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "SD Card"), 0xffffffff,
     SettingsItemVisibility::RequiresSdCard},
    {SettingsSection::Storage, SettingsItemRole::Command, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "Eject SD Card"), 0xffffd278,
     SettingsItemVisibility::RequiresSdCard},
    {SettingsSection::Storage, SettingsItemRole::Command, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "Format SD Card"), 0xffe44848,
     SettingsItemVisibility::RequiresSdCard},
    {SettingsSection::Storage, SettingsItemRole::Status, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "USB Disk"), 0xffffffff,
     SettingsItemVisibility::RequiresUsbDisk},
    {SettingsSection::Storage, SettingsItemRole::Command, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "Eject USB Disk"), 0xffffd278,
     SettingsItemVisibility::RequiresUsbDisk},
    {SettingsSection::Storage, SettingsItemRole::Command, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "Format USB Disk"), 0xffe44848,
     SettingsItemVisibility::RequiresUsbDisk},

    {SettingsSection::System, SettingsItemRole::Setting, SettingKey::AutoShutdownTimeout,
     SettingsEditor::Choice, QT_TRANSLATE_NOOP("SettingsCatalog", "Auto Shutdown"), 0xffffffff,
     SettingsItemVisibility::Always, {}, kAutoShutdownChoices,
     int(std::size(kAutoShutdownChoices))},
    {SettingsSection::System, SettingsItemRole::Setting, SettingKey::ScreenBrightnessPercent,
     SettingsEditor::Slider, QT_TRANSLATE_NOOP("SettingsCatalog", "Screen Brightness"),
     0xffffffff, SettingsItemVisibility::Always,
     {1.0, NumberFormat::Percent, PeerBound::None,
      std::nullopt, 0x10108, 50, true}},
    {SettingsSection::System, SettingsItemRole::Setting, SettingKey::AudioVolumePercent,
     SettingsEditor::Slider, QT_TRANSLATE_NOOP("SettingsCatalog", "Audio Volume"), 0xffffffff,
     SettingsItemVisibility::Always,
     {1.0, NumberFormat::Percent, PeerBound::None,
      std::nullopt, 0xeb51, 50, true}},
    {SettingsSection::System, SettingsItemRole::Setting, SettingKey::AppLanguage,
     SettingsEditor::Choice, QT_TRANSLATE_NOOP("SettingsCatalog", "Language"), 0xffffffff,
     SettingsItemVisibility::Always, {},
     kAppLanguageChoices, int(std::size(kAppLanguageChoices))},
    {SettingsSection::System, SettingsItemRole::Command, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "Date & Time"), 0xffffffff},
    {SettingsSection::System, SettingsItemRole::Navigation, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "System Tools"), 0xffffd278,
     SettingsItemVisibility::Always,
     {}, nullptr, 0, SettingsSection::SystemTools},
    {SettingsSection::System, SettingsItemRole::Command, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "About"), 0xffffffff},
    {SettingsSection::SystemTools, SettingsItemRole::Command, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "Initialize Userdata"), 0xffe44848,
     SettingsItemVisibility::RequiresUnattachedUserdata},
    {SettingsSection::SystemTools, SettingsItemRole::Command, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "Haptic Motor Calibration"), 0xffffffff},
    {SettingsSection::SystemTools, SettingsItemRole::Command, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "Software Update"), 0xffffffff},
    {SettingsSection::SystemTools, SettingsItemRole::Command, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "Reboot to Loader"), 0xffffffff},
    {SettingsSection::SystemTools, SettingsItemRole::Command, std::nullopt, std::nullopt,
     QT_TRANSLATE_NOOP("SettingsCatalog", "Restore Defaults"), 0xffe44848},
}};

constexpr bool itemContractsAreValid() {
    for (const ItemSpec& item : kItems) {
        const bool hasSettingContract = item.settingKey.has_value() && item.editor.has_value();
        const bool hasDestination = item.destinationSection.has_value();
        switch (item.role) {
        case SettingsItemRole::Setting:
            if (!hasSettingContract || hasDestination) return false;
            break;
        case SettingsItemRole::Status:
        case SettingsItemRole::Command:
            if (item.settingKey.has_value() || item.editor.has_value() || hasDestination) {
                return false;
            }
            break;
        case SettingsItemRole::Navigation:
            if (item.settingKey.has_value() || item.editor.has_value() || !hasDestination) {
                return false;
            }
            break;
        }
    }
    return true;
}

static_assert(itemContractsAreValid(), "Settings catalog contains an invalid item contract");

constexpr SectionSpec kSections[] = {
    {0xf837, 0xff4868ff, QT_TRANSLATE_NOOP("SettingsCatalog", "Camera")},
    {0xf02c, 0xff1c9e70, QT_TRANSLATE_NOOP("SettingsCatalog", "View")},
    {0xfaf7, 0xff5484d6, QT_TRANSLATE_NOOP("SettingsCatalog", "Storage")},
    {0xea03, 0xffb6662d, QT_TRANSLATE_NOOP("SettingsCatalog", "System")},
};

const ItemSpec& itemSpec(SettingID item) {
    return kItems[static_cast<size_t>(item)];
}

const SectionSpec& sectionSpec(int index) {
    return kSections[index];
}

QString catalogText(const char* sourceText) {
    return QCoreApplication::translate("SettingsCatalog", sourceText);
}

double snapshotNumberValue(const SettingsSnapshot& snapshot, SettingKey key) {
    return snapshot.values.value(key).toDouble();
}

bool itemVisible(const ItemSpec& item,
                 const SettingsSnapshot& snapshot,
                 bool sdCardReady,
                 bool usbDiskReady,
                 bool userdataUbiAttached) {
    switch (item.visibility) {
    case SettingsItemVisibility::Always:
        return true;
    case SettingsItemVisibility::RequiresSdCard:
        return sdCardReady;
    case SettingsItemVisibility::RequiresUsbDisk:
        return usbDiskReady;
    case SettingsItemVisibility::RequiresUnattachedUserdata:
        return !userdataUbiAttached;
    case SettingsItemVisibility::RequiresLegacyMode:
        return !snapshot.values.value(SettingKey::SeekVisionEnabled).toBool();
    case SettingsItemVisibility::RequiresLegacyLinearAgc:
        return !snapshot.values.value(SettingKey::SeekVisionEnabled).toBool() &&
               snapshot.values.value(SettingKey::AgcMode).toInt() ==
                   static_cast<int>(AgcMode::LinearManual);
    }
    return false;
}

QString numberText(const NumberSpec& number, double value) {
    switch (number.format) {
    case NumberFormat::Fixed2:
        return QString::number(value, 'f', 2);
    case NumberFormat::SignedCelsius0:
        return QStringLiteral("%1%2°C")
            .arg(value > 0.0001 ? QStringLiteral("+") : QString())
            .arg(QString::number(value, 'f', 0));
    case NumberFormat::SignedCelsius1:
        return QStringLiteral("%1%2°C")
            .arg(value > 0.0001 ? QStringLiteral("+") : QString())
            .arg(QString::number(value, 'f', 1));
    case NumberFormat::Percent:
        return QStringLiteral("%1%").arg(qRound(value));
    }
    return QString();
}

} // namespace

int SettingsCatalog::sectionCount() {
    return int(std::size(kSections));
}

PrimaryItemData SettingsCatalog::sectionAt(int index) {
    const SectionSpec& section = sectionSpec(index);
    return {QString(QChar(section.iconCodepoint)), QColor::fromRgba(section.iconColor),
            catalogText(section.title)};
}

QString SettingsCatalog::sectionTitle(int index) {
    return catalogText(sectionSpec(index).title);
}

int SettingsCatalog::sectionIndexForItem(SettingID item) {
    return static_cast<int>(itemSpec(item).section);
}

namespace {

SettingsItemData displayData(SettingID id, const ItemSpec& item) {
    return {id, item.role, item.settingKey, item.editor, catalogText(item.title),
            QColor::fromRgba(item.titleColor), item.destinationSection};
}

} // namespace

std::vector<SettingsItemData> SettingsCatalog::visibleItems(
    SettingsSection section,
    const SettingsSnapshot& snapshot,
    bool sdCardReady,
    bool usbDiskReady,
    bool userdataUbiAttached) {
    std::vector<SettingsItemData> visible;
    for (size_t index = 0; index < kItems.size(); ++index) {
        const ItemSpec& item = kItems[index];
        if (item.section != section ||
            !itemVisible(item, snapshot, sdCardReady, usbDiskReady, userdataUbiAttached)) {
            continue;
        }
        visible.push_back(displayData(static_cast<SettingID>(index), item));
    }
    return visible;
}

SettingsNumberEditor SettingsCatalog::numberEditor(SettingID item,
                                                    const SettingsSnapshot& snapshot) {
    const ItemSpec& spec = itemSpec(item);
    const NumberSpec& number = spec.number;
    const SettingDescriptor& descriptor = *settingDescriptorForKey(*spec.settingKey);
    double minimum = descriptor.minimum;
    double maximum = descriptor.maximum;
    if (number.peerBound == PeerBound::BelowPeer) {
        maximum = qMax(minimum,
                       double(qRound(snapshotNumberValue(snapshot, *number.peerKey))) -
                           number.step);
    }
    if (number.peerBound == PeerBound::AbovePeer) {
        minimum = qMin(maximum,
                       double(qRound(snapshotNumberValue(snapshot, *number.peerKey))) +
                           number.step);
    }
    const uint iconCodepoint[] = {number.iconCodepoint};
    return {minimum, maximum, number.step,
            qBound(minimum, snapshotNumberValue(snapshot, *spec.settingKey), maximum),
            number.previewThrottleMs, number.dismissOnCommit,
            number.iconCodepoint ? QString::fromUcs4(iconCodepoint, 1) : QString()};
}

SettingsChoiceEditor SettingsCatalog::choiceEditor(SettingID item,
                                                    const SettingsSnapshot& snapshot) {
    const ItemSpec& spec = itemSpec(item);
    const int current = snapshot.values.value(*spec.settingKey).toInt();
    SettingsChoiceEditor editor;
    for (int index = 0; index < spec.choiceCount; ++index) {
        const ChoiceSpec& choice = spec.choices[index];
        editor.options.append({QString::fromLatin1(choice.id),
                               catalogText(choice.title), choice.value});
        if (choice.value == current) editor.selectedIndex = index;
    }
    return editor;
}

QVariant SettingsCatalog::numberValue(SettingID item, double value) {
    const SettingDescriptor& descriptor =
        *settingDescriptorForKey(*itemSpec(item).settingKey);
    return descriptor.valueType == SettingValueType::Integer ? QVariant(qRound(value))
                                                              : QVariant(float(value));
}

QVariant SettingsCatalog::choiceValue(SettingID item, int optionIndex) {
    return itemSpec(item).choices[optionIndex].value;
}

QString SettingsCatalog::editedValueText(SettingID item, double value) {
    return numberText(itemSpec(item).number, value);
}

QString SettingsCatalog::valueText(SettingID item, const SettingsSnapshot& snapshot) {
    const ItemSpec& spec = itemSpec(item);
    if (spec.editor == SettingsEditor::Stepper || spec.editor == SettingsEditor::Slider) {
        return numberText(spec.number, snapshotNumberValue(snapshot, *spec.settingKey));
    }
    if (spec.editor == SettingsEditor::Choice) {
        const SettingsChoiceEditor editor = choiceEditor(item, snapshot);
        return editor.options[editor.selectedIndex].title;
    }
    return QString();
}

bool SettingsCatalog::sectionVisibilityAffectedBySettingsChange(
    SettingsSection section,
    const QSet<SettingKey>& changedKeys) {
    if (!changedKeys.contains(SettingKey::SeekVisionEnabled) &&
        !changedKeys.contains(SettingKey::AgcMode)) {
        return false;
    }
    for (const ItemSpec& item : kItems) {
        if (item.section == section &&
            (item.visibility == SettingsItemVisibility::RequiresLegacyMode ||
             item.visibility == SettingsItemVisibility::RequiresLegacyLinearAgc)) {
            return true;
        }
    }
    return false;
}

bool SettingsCatalog::sectionVisibilityAffectedByStorageState(SettingsSection section) {
    for (const ItemSpec& item : kItems) {
        if (item.section == section &&
            (item.visibility == SettingsItemVisibility::RequiresSdCard ||
             item.visibility == SettingsItemVisibility::RequiresUsbDisk)) {
            return true;
        }
    }
    return false;
}
