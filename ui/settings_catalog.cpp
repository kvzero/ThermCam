#include "ui/settings_catalog.h"

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
    std::optional<SettingKey> settingKey;
    const char* title;
    QRgb titleColor;
    ActionType type;
    SettingsEditor editor;
    SecondaryVisibility visibility = SecondaryVisibility::Always;
    NumberSpec number;
    const ChoiceSpec* choices = nullptr;
    int choiceCount = 0;

    constexpr ItemSpec(SettingsSection section,
                       std::optional<SettingKey> settingKey,
                       const char* title,
                       QRgb titleColor,
                       ActionType type,
                       SettingsEditor editor,
                       SecondaryVisibility visibility = SecondaryVisibility::Always,
                       NumberSpec number = {},
                       const ChoiceSpec* choices = nullptr,
                       int choiceCount = 0)
        : section(section), settingKey(settingKey), title(title),
          titleColor(titleColor), type(type), editor(editor), visibility(visibility),
          number(number), choices(choices), choiceCount(choiceCount) {}

};

struct SectionSpec {
    uint iconCodepoint;
    QRgb iconColor;
    const char* title;
};

constexpr ChoiceSpec kAgcChoices[] = {
    {"auto_histeq", "Auto (HistEQ AGC)", static_cast<int>(AgcMode::HistEqAuto)},
    {"linear_manual", "Linear", static_cast<int>(AgcMode::LinearManual)},
};

constexpr ChoiceSpec kTemperatureUnitChoices[] = {
    {"celsius", "°C", static_cast<int>(TemperatureUnit::Celsius)},
    {"fahrenheit", "°F", static_cast<int>(TemperatureUnit::Fahrenheit)},
};

constexpr ChoiceSpec kStoragePriorityChoices[] = {
    {"sd_first", "SD Card First", static_cast<int>(StoragePriority::SdFirst)},
    {"usb_first", "USB Disk First", static_cast<int>(StoragePriority::UsbFirst)},
};

constexpr std::array<ItemSpec, static_cast<size_t>(SettingID::Count)> kItems = {{
    {SettingsSection::Camera, SettingKey::Emissivity,
     "Emissivity", 0xffffffff, ActionType::Action, SettingsEditor::Stepper,
     SecondaryVisibility::Always,
     {0.01, NumberFormat::Fixed2}},
    {SettingsSection::Camera, SettingKey::ShutterAutoEnabled,
     "Auto Shutter", 0xffffffff, ActionType::Toggle, SettingsEditor::Toggle},
    {SettingsSection::Camera, SettingKey::SeekVisionEnabled,
     "Auto SeekVision", 0xffffffff, ActionType::Toggle, SettingsEditor::Toggle},
    {SettingsSection::Camera, SettingKey::LegacySharpenEnabled,
     "Sharpen Filter", 0xffffffff, ActionType::Toggle, SettingsEditor::Toggle,
     SecondaryVisibility::RequiresLegacyMode},
    {SettingsSection::Camera, SettingKey::AgcMode,
     "AGC Mode", 0xffffffff, ActionType::Action, SettingsEditor::Choice,
     SecondaryVisibility::RequiresLegacyMode, {}, kAgcChoices, int(std::size(kAgcChoices))},
    {SettingsSection::Camera, SettingKey::LinearAgcMinCelsius,
     "- Linear AGC Min", 0xffffffff, ActionType::Action, SettingsEditor::Stepper,
     SecondaryVisibility::RequiresLegacyLinearAgc,
     {1.0, NumberFormat::SignedCelsius0, PeerBound::BelowPeer,
      SettingKey::LinearAgcMaxCelsius}},
    {SettingsSection::Camera, SettingKey::LinearAgcMaxCelsius,
     "- Linear AGC Max", 0xffffffff, ActionType::Action, SettingsEditor::Stepper,
     SecondaryVisibility::RequiresLegacyLinearAgc,
     {1.0, NumberFormat::SignedCelsius0, PeerBound::AbovePeer,
      SettingKey::LinearAgcMinCelsius}},
    {SettingsSection::Camera, SettingKey::ThermographyOffsetCelsius, "Temperature Offset", 0xffffffff,
     ActionType::Action, SettingsEditor::Stepper, SecondaryVisibility::Always,
     {0.1, NumberFormat::SignedCelsius1}},
    {SettingsSection::Camera, std::nullopt,
     "Flat-Scene Correction", 0xffffffff, ActionType::Action, SettingsEditor::Action},

    {SettingsSection::View, SettingKey::Palette,
     "Palette", 0xffffffff, ActionType::Action, SettingsEditor::Action},
    {SettingsSection::View, SettingKey::SaveMarkerInMedia,
     "Save Marker Overlay", 0xffffffff, ActionType::Toggle, SettingsEditor::Toggle},
    {SettingsSection::View, SettingKey::HideMarkerWhenHudHidden, "Hide Marker with HUD", 0xffffffff,
     ActionType::Toggle, SettingsEditor::Toggle},
    {SettingsSection::View, SettingKey::TemperatureUnit,
     "Temperature Unit", 0xffffffff, ActionType::Action, SettingsEditor::Choice,
     SecondaryVisibility::Always, {}, kTemperatureUnitChoices,
     int(std::size(kTemperatureUnitChoices))},

    {SettingsSection::Storage, std::nullopt,
     "Internal", 0xffffffff, ActionType::Value, SettingsEditor::Action},
    {SettingsSection::Storage, SettingKey::StoragePriority,
     "Priority", 0xffffffff, ActionType::Action, SettingsEditor::Choice,
     SecondaryVisibility::Always, {}, kStoragePriorityChoices,
     int(std::size(kStoragePriorityChoices))},
    {SettingsSection::Storage, std::nullopt,
     "SD Card", 0xffffffff, ActionType::Value, SettingsEditor::Action,
     SecondaryVisibility::RequiresSdCard},
    {SettingsSection::Storage, std::nullopt,
     "Eject SD Card", 0xffffd278, ActionType::Action, SettingsEditor::Action,
     SecondaryVisibility::RequiresSdCard},
    {SettingsSection::Storage, std::nullopt,
     "Format SD Card", 0xffe44848, ActionType::Action, SettingsEditor::Action,
     SecondaryVisibility::RequiresSdCard},
    {SettingsSection::Storage, std::nullopt,
     "USB Disk", 0xffffffff, ActionType::Value, SettingsEditor::Action,
     SecondaryVisibility::RequiresUsbDisk},
    {SettingsSection::Storage, std::nullopt,
     "Eject USB Disk", 0xffffd278, ActionType::Action, SettingsEditor::Action,
     SecondaryVisibility::RequiresUsbDisk},
    {SettingsSection::Storage, std::nullopt,
     "Format USB Disk", 0xffe44848, ActionType::Action, SettingsEditor::Action,
     SecondaryVisibility::RequiresUsbDisk},

    {SettingsSection::System, SettingKey::ScreenBrightnessPercent, "Screen Brightness", 0xffffffff,
     ActionType::Value, SettingsEditor::Slider, SecondaryVisibility::Always,
     {1.0, NumberFormat::Percent, PeerBound::None,
      std::nullopt, 0x10108, 50, true}},
    {SettingsSection::System, SettingKey::AudioVolumePercent,
     "Audio Volume", 0xffffffff, ActionType::Value, SettingsEditor::Slider,
     SecondaryVisibility::Always,
     {1.0, NumberFormat::Percent, PeerBound::None,
      std::nullopt, 0xeb51, 50, true}},
    {SettingsSection::System, std::nullopt,
     "Date & Time", 0xffffffff, ActionType::Action, SettingsEditor::Action},
}};

constexpr SectionSpec kSections[] = {
    {0xf837, 0xff4868ff, "Camera"},
    {0xf02c, 0xff1c9e70, "View"},
    {0xfaf7, 0xff5484d6, "Storage"},
    {0xea03, 0xffb6662d, "System"},
};

const ItemSpec& itemSpec(SettingID item) {
    return kItems[static_cast<size_t>(item)];
}

const SectionSpec& sectionSpec(int index) {
    return kSections[index];
}

double snapshotNumberValue(const SettingsSnapshot& snapshot, SettingKey key) {
    return snapshot.values.value(key).toDouble();
}

bool itemVisible(const ItemSpec& item,
                 const SettingsSnapshot& snapshot,
                 bool sdCardReady,
                 bool usbDiskReady) {
    switch (item.visibility) {
    case SecondaryVisibility::Always:
        return true;
    case SecondaryVisibility::RequiresSdCard:
        return sdCardReady;
    case SecondaryVisibility::RequiresUsbDisk:
        return usbDiskReady;
    case SecondaryVisibility::RequiresLegacyMode:
        return !snapshot.values.value(SettingKey::SeekVisionEnabled).toBool();
    case SecondaryVisibility::RequiresLegacyLinearAgc:
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
            QString::fromUtf8(section.title)};
}

QString SettingsCatalog::sectionTitle(int index) {
    return QString::fromUtf8(sectionSpec(index).title);
}

int SettingsCatalog::sectionIndexForItem(SettingID item) {
    return static_cast<int>(itemSpec(item).section);
}

std::vector<SecondaryItemData> SettingsCatalog::visibleItems(int sectionIndex,
                                                              const SettingsSnapshot& snapshot,
                                                              bool sdCardReady,
                                                              bool usbDiskReady) {
    std::vector<SecondaryItemData> visible;
    for (size_t index = 0; index < kItems.size(); ++index) {
        const ItemSpec& item = kItems[index];
        if (static_cast<int>(item.section) != sectionIndex ||
            !itemVisible(item, snapshot, sdCardReady, usbDiskReady)) {
            continue;
        }
        visible.push_back({static_cast<SettingID>(index), item.settingKey,
                           QString::fromUtf8(item.title),
                           QColor::fromRgba(item.titleColor), item.type, item.editor});
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
                               QString::fromUtf8(choice.title), choice.value});
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
    int sectionIndex,
    const QSet<SettingKey>& changedKeys) {
    if (!changedKeys.contains(SettingKey::SeekVisionEnabled) &&
        !changedKeys.contains(SettingKey::AgcMode)) {
        return false;
    }
    for (const ItemSpec& item : kItems) {
        if (static_cast<int>(item.section) == sectionIndex &&
            (item.visibility == SecondaryVisibility::RequiresLegacyMode ||
             item.visibility == SecondaryVisibility::RequiresLegacyLinearAgc)) {
            return true;
        }
    }
    return false;
}

bool SettingsCatalog::sectionVisibilityAffectedByStorageState(int sectionIndex) {
    for (const ItemSpec& item : kItems) {
        if (static_cast<int>(item.section) == sectionIndex &&
            (item.visibility == SecondaryVisibility::RequiresSdCard ||
             item.visibility == SecondaryVisibility::RequiresUsbDisk)) {
            return true;
        }
    }
    return false;
}
