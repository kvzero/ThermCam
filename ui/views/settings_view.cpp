#include "settings_view.h"
#include "ui/widgets/settings_row.h"
#include "ui/widgets/scroll_indicator.h"
#include "core/event_bus.h"
#include "core/settings_store.h"
#include "hardware/hardware_manager.h"
#include "hardware/imaging/thermal_camera.h"
#include "hardware/storage/storage_manager.h"
#include "ui/app.h"

#include <QPainter>
#include <QEasingCurve>
#include <QDebug>
#include <QTimer>
#include <cmath>
#include <memory>

namespace {

const QVector<PrimaryItemData> kMenuBlueprint = {
    {
        QString(QChar(0xf837)), QColor(72, 104, 255), "Camera",
        {
            {SettingID::Emissivity, "Emissivity", QColor(255, 255, 255), ActionType::Value},
            {SettingID::ShutterAutoEnabled, "Auto Shutter", QColor(255, 255, 255), ActionType::Toggle},
            {SettingID::SeekVisionEnabled, "Auto SeekVision", QColor(255, 255, 255), ActionType::Toggle},
            {SettingID::LegacySharpenEnabled, "Sharpen Filter", QColor(255, 255, 255), ActionType::Toggle, SecondaryVisibility::RequiresLegacyMode},
            {SettingID::AgcMode, "AGC Mode", QColor(255, 255, 255), ActionType::Action, SecondaryVisibility::RequiresLegacyMode},
            {SettingID::LinearAgcMin, "- Linear AGC Min", QColor(255, 255, 255), ActionType::Action, SecondaryVisibility::RequiresLegacyLinearAgc},
            {SettingID::LinearAgcMax, "- Linear AGC Max", QColor(255, 255, 255), ActionType::Action, SecondaryVisibility::RequiresLegacyLinearAgc},
            {SettingID::ThermographyOffset, "Temperature Offset", QColor(255, 255, 255), ActionType::Action},
            {SettingID::TriggerFlatSceneCorrection, "Flat-Scene Correction", QColor(255, 255, 255), ActionType::Action}
        }
    },
    {
        QString(QChar(0xf02c)), QColor(28, 158, 112), "View",
        {
            {SettingID::Palette, "Palette", QColor(255, 255, 255), ActionType::Action},
            {SettingID::OSDOverlay, "Save Marker Overlay", QColor(255, 255, 255), ActionType::Toggle},
            {SettingID::HideMarkerWhenHudHidden, "Hide Marker with HUD", QColor(255, 255, 255), ActionType::Toggle},
            {SettingID::TemperatureUnit, "Temperature Unit", QColor(255, 255, 255), ActionType::Action}
        }
    },
    {
        QString(QChar(0xfaf7)), QColor(84, 132, 214), "Storage",
        {
            {SettingID::StoragePriority, "Priority", QColor(255, 255, 255), ActionType::Action, SecondaryVisibility::Always},
            {SettingID::SdCardCapacity, "SD Card", QColor(255, 255, 255), ActionType::Value, SecondaryVisibility::RequiresSdCard},
            {SettingID::SdCardSafeEject, "Eject SD Card", QColor(255, 210, 120), ActionType::Action, SecondaryVisibility::RequiresSdCard},
            {SettingID::SdCardFormat, "Format SD Card", QColor(228, 72, 72), ActionType::Action, SecondaryVisibility::RequiresSdCard},
            {SettingID::UsbDiskCapacity, "USB Disk", QColor(255, 255, 255), ActionType::Value, SecondaryVisibility::RequiresUsbDisk},
            {SettingID::UsbDiskSafeEject, "Eject USB Disk", QColor(255, 210, 120), ActionType::Action, SecondaryVisibility::RequiresUsbDisk},
            {SettingID::UsbDiskFormat, "Format USB Disk", QColor(228, 72, 72), ActionType::Action, SecondaryVisibility::RequiresUsbDisk}
        }
    },
    {
        QString(QChar(0xea03)), QColor(182, 102, 45), "System",
        {
            {SettingID::ScreenBrightness, "Screen Brightness", QColor(255, 255, 255), ActionType::Value},
            {SettingID::AudioVolume, "Audio Volume", QColor(255, 255, 255), ActionType::Value}
        }
    }
};

const QString kTopBarBackIcon = QString(QChar(0xea60));
const QString kTopBarCloseIcon = QString(QChar(0xeb55));

constexpr float kEmissivityMin = 0.01f;
constexpr float kEmissivityMax = 1.00f;
constexpr int kEmissivitySliderMin = 1;
constexpr int kEmissivitySliderMax = 100;
constexpr int kEmissivitySliderStep = 1;
constexpr int kPreviewThrottleMs = 50;
constexpr int kThermographyOffsetTenthsMin = -100;
constexpr int kThermographyOffsetTenthsMax = 100;
constexpr int kThermographyOffsetTenthsStep = 1;
constexpr float kLinearAgcCelsiusMin = -40.0f;
constexpr float kLinearAgcCelsiusMax = 600.0f;
constexpr int kLinearAgcStep = 1;
constexpr int kScreenBrightnessPercentMin = 1;
constexpr int kScreenBrightnessPercentMax = 100;
constexpr int kAudioVolumePercentMin = 0;
constexpr int kAudioVolumePercentMax = 100;
const uint kScreenBrightnessIconCodepoint[] = {0x10108};
const QString kScreenBrightnessIconGlyph = QString::fromUcs4(kScreenBrightnessIconCodepoint, 1);
const QString kAudioVolumeIconGlyph = QString(QChar(0xeb51));

float clampEmissivity(float value) {
    return qBound(kEmissivityMin, value, kEmissivityMax);
}

int emissivityToSliderValue(float value) {
    const float clamped = clampEmissivity(value);
    return qBound(kEmissivitySliderMin,
                  qRound(clamped * 100.0f),
                  kEmissivitySliderMax);
}

float sliderValueToEmissivity(int value) {
    const int clamped = qBound(kEmissivitySliderMin, value, kEmissivitySliderMax);
    return clampEmissivity(static_cast<float>(clamped) / 100.0f);
}

int thermographyOffsetToTenths(float value) {
    return qBound(kThermographyOffsetTenthsMin,
                  qRound(value * 10.0f),
                  kThermographyOffsetTenthsMax);
}

float thermographyTenthsToCelsius(int value) {
    const int clamped = qBound(kThermographyOffsetTenthsMin,
                               value,
                               kThermographyOffsetTenthsMax);
    return static_cast<float>(clamped) / 10.0f;
}

QVariant defaultValueForKey(SettingKey key) {
    for (const auto& desc : kSettingRegistry) {
        if (desc.key == key) return desc.defaultValue;
    }
    return QVariant();
}

int normalizeTemperatureUnitInt(int value) {
    const int c = static_cast<int>(TemperatureUnit::Celsius);
    const int f = static_cast<int>(TemperatureUnit::Fahrenheit);
    return (value == f) ? f : c;
}

QString formatTemperatureUnit(int unitValue) {
    const QChar degree(0x00B0);
    return QString(degree) +
           QString((normalizeTemperatureUnitInt(unitValue) ==
                    static_cast<int>(TemperatureUnit::Fahrenheit))
                       ? QLatin1Char('F')
                       : QLatin1Char('C'));
}

int normalizeStoragePriorityInt(int value) {
    const int sd = static_cast<int>(StoragePriority::SdFirst);
    const int usb = static_cast<int>(StoragePriority::UsbFirst);
    return (value == usb) ? usb : sd;
}

QString formatStoragePriority(int priorityValue) {
    return (normalizeStoragePriorityInt(priorityValue) ==
            static_cast<int>(StoragePriority::UsbFirst))
               ? QStringLiteral("USB Disk First")
               : QStringLiteral("SD Card First");
}

int clampPercentInt(int value, int minValue, int maxValue) {
    return qBound(minValue, value, maxValue);
}

QString formatPercent(int value, int minValue, int maxValue) {
    return QStringLiteral("%1%").arg(clampPercentInt(value, minValue, maxValue));
}

bool boolSettingFromSnapshot(const SettingsSnapshot& snapshot, SettingKey key, bool fallback) {
    const QVariant value = snapshot.values.value(key, QVariant(fallback));
    if (!value.canConvert<bool>()) return fallback;
    return value.toBool();
}

float floatSettingFromSnapshot(const SettingsSnapshot& snapshot, SettingKey key, float fallback) {
    bool ok = false;
    const float value = snapshot.values.value(key, QVariant(fallback)).toFloat(&ok);
    return ok ? value : fallback;
}

int intSettingFromSnapshot(const SettingsSnapshot& snapshot, SettingKey key, int fallback) {
    bool ok = false;
    const int value = snapshot.values.value(key, QVariant(fallback)).toInt(&ok);
    return ok ? value : fallback;
}

int normalizeAgcModeInt(int value) {
    const int linear = static_cast<int>(AgcMode::LinearManual);
    return (value == linear) ? linear : static_cast<int>(AgcMode::HistEqAuto);
}

bool seekVisionEnabledFromSnapshot(const SettingsSnapshot& snapshot) {
    return boolSettingFromSnapshot(snapshot, SettingKey::SeekVisionEnabled, true);
}

bool linearAgcSelectedFromSnapshot(const SettingsSnapshot& snapshot) {
    bool ok = false;
    const int parsed = snapshot.values
                           .value(SettingKey::AgcMode, defaultValueForKey(SettingKey::AgcMode))
                           .toInt(&ok);
    const int mode = normalizeAgcModeInt(ok ? parsed : static_cast<int>(AgcMode::HistEqAuto));
    return (mode == static_cast<int>(AgcMode::LinearManual));
}

bool visibilityDependsOnSettings(SecondaryVisibility visibility) {
    return visibility == SecondaryVisibility::RequiresLegacyMode ||
           visibility == SecondaryVisibility::RequiresLegacyLinearAgc;
}

bool visibilityDependsOnStorage(SecondaryVisibility visibility) {
    return visibility == SecondaryVisibility::RequiresSdCard ||
           visibility == SecondaryVisibility::RequiresUsbDisk;
}

bool primaryHasVisibilityDependency(int primaryIndex,
                                    bool includeSettingsDependency,
                                    bool includeStorageDependency) {
    if (primaryIndex < 0 || primaryIndex >= kMenuBlueprint.size()) return false;

    const auto& items = kMenuBlueprint[primaryIndex].subItems;
    for (const auto& item : items) {
        const bool settingsDependency = visibilityDependsOnSettings(item.visibility);
        const bool storageDependency = visibilityDependsOnStorage(item.visibility);
        if ((includeSettingsDependency && settingsDependency) ||
            (includeStorageDependency && storageDependency)) {
            return true;
        }
    }

    return false;
}

bool primaryVisibilityAffectedBySettingsChange(int primaryIndex,
                                               const QSet<SettingKey>& changedKeys) {
    if (!changedKeys.contains(SettingKey::SeekVisionEnabled) &&
        !changedKeys.contains(SettingKey::AgcMode)) {
        return false;
    }
    return primaryHasVisibilityDependency(primaryIndex, true, false);
}

bool primaryVisibilityAffectedByStorageState(int primaryIndex) {
    return primaryHasVisibilityDependency(primaryIndex, false, true);
}

QString formatAgcMode(int modeValue) {
    return (normalizeAgcModeInt(modeValue) == static_cast<int>(AgcMode::LinearManual))
               ? QStringLiteral("Linear")
               : QStringLiteral("Auto (HistEQ)");
}

QString formatSignedCelsius(float value, int precision) {
    const QChar degree(0x00B0);
    const QString sign = (value > 0.0001f) ? QStringLiteral("+") : QString();
    return QStringLiteral("%1%2%3C")
        .arg(sign)
        .arg(QString::number(value, 'f', precision))
        .arg(QString(degree));
}

QString formatStorageCapacityValue(quint64 mb) {
    if (mb >= 1024) {
        return QString("%1 GB").arg(QString::number(static_cast<double>(mb) / 1024.0, 'f', 1));
    }
    return QString("%1 MB").arg(mb);
}

QString formatStorageCapacity(const StorageVolumeStatus& status) {
    if (!status.ready || status.totalMB == 0) return "--";
    const quint64 usedMB =
        (status.totalMB >= status.availableMB) ? (status.totalMB - status.availableMB) : 0;
    return QString("%1 / %2")
        .arg(formatStorageCapacityValue(usedMB))
        .arg(formatStorageCapacityValue(status.totalMB));
}

std::vector<SecondaryItemData> visibleSecondaryItems(int primaryIndex,
                                                     const SettingsSnapshot& snapshot) {
    if (primaryIndex < 0 || primaryIndex >= kMenuBlueprint.size()) {
        return {};
    }

    const auto& full = kMenuBlueprint[primaryIndex].subItems;
    std::vector<SecondaryItemData> visible;
    if (full.empty()) return visible;

    auto* storage = HardwareManager::instance().storage();
    const bool sdReady = storage && storage->isSdCardReady();
    const bool usbReady = storage && storage->isUsbDiskReady();
    const bool legacyMode = !seekVisionEnabledFromSnapshot(snapshot);
    const bool linearAgcMode = linearAgcSelectedFromSnapshot(snapshot);

    for (const auto& item : full) {
        bool show = false;
        switch (item.visibility) {
        case SecondaryVisibility::Always:
            show = true;
            break;
        case SecondaryVisibility::RequiresSdCard:
            show = sdReady;
            break;
        case SecondaryVisibility::RequiresUsbDisk:
            show = usbReady;
            break;
        case SecondaryVisibility::RequiresLegacyMode:
            show = legacyMode;
            break;
        case SecondaryVisibility::RequiresLegacyLinearAgc:
            show = legacyMode && linearAgcMode;
            break;
        }

        if (show) {
            visible.push_back(item);
        }
    }

    return visible;
}

} // namespace

// ============================================================
// SettingsTopBar
// ============================================================

SettingsTopBar::SettingsTopBar(QWidget* parent) : QWidget(parent) {
    setProperty("isInteractable", true);
    setProperty("allowSlideTrigger", false);
}

void SettingsTopBar::setTitle(const QString& title) {
    if (m_title == title) return;
    m_title = title;
    update();
}

void SettingsTopBar::setMaskOpacity(qreal v) {
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(m_maskOpacity, v)) return;
    m_maskOpacity = v;
    update();
}

SettingsTopBar::PressZone SettingsTopBar::zoneAt(const QPoint& pos) const {
    const int expand = qRound(height() * 0.38);
    const QRect backTouch = m_backRect.adjusted(-expand, -expand, expand, expand);
    const QRect closeTouch = m_closeRect.adjusted(-expand, -expand, expand, expand);

    if (backTouch.contains(pos)) return PressZone::Back;
    if (closeTouch.contains(pos)) return PressZone::Close;
    return PressZone::None;
}

void SettingsTopBar::onInteractionBegin(const InteractionEvent& event) {
    m_pressedZone = zoneAt(event.currentLocal);
    m_lastPos = event.currentLocal;
    update();
}

InteractionUpdateDecision SettingsTopBar::onInteractionUpdate(const InteractionEvent& event) {
    if (m_pressedZone == PressZone::None) return InteractionUpdateDecision::ReleaseOwner;
    m_lastPos = event.currentLocal;
    update();
    return InteractionUpdateDecision::KeepOwner;
}

void SettingsTopBar::onInteractionEnd(const InteractionEvent& /*event*/) {
    if (m_pressedZone == PressZone::Back && zoneAt(m_lastPos) == PressZone::Back) {
        emit backTriggered();
    } else if (m_pressedZone == PressZone::Close && zoneAt(m_lastPos) == PressZone::Close) {
        emit closeTriggered();
    }
    m_pressedZone = PressZone::None;
    update();
}

void SettingsTopBar::onInteractionCancel() {
    if (m_pressedZone == PressZone::None) return;
    m_pressedZone = PressZone::None;
    update();
}

void SettingsTopBar::resizeEvent(QResizeEvent* /*event*/) {
    const int h = height();
    const int w = width();
    const int btnSize = qRound(h * 0.65);
    const int margin = qRound(h * 0.15);

    m_backRect = QRect(margin, (h - btnSize) / 2, btnSize, btnSize);
    m_closeRect = QRect(w - margin - btnSize, (h - btnSize) / 2, btnSize, btnSize);
}

void SettingsTopBar::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_maskOpacity > 0.01) {
        const QRect gradRect = rect();
        QLinearGradient grad(gradRect.topLeft(), gradRect.bottomLeft());
        grad.setColorAt(0.0, QColor(0, 0, 0, qRound(255 * m_maskOpacity)));
        grad.setColorAt(0.7, QColor(0, 0, 0, qRound(190 * m_maskOpacity)));
        grad.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(gradRect, grad);
    }

    auto drawButton = [&](const QRect& r, const QString& iconGlyph, bool active) {
        QColor bg = active ? QColor(255, 255, 255, 45) : QColor(255, 255, 255, 28);
        p.setPen(QPen(QColor(255, 255, 255, 52), 1));
        p.setBrush(bg);
        p.drawEllipse(r);

        QFont iconFont("tabler-icons");
        iconFont.setPixelSize(qRound(r.height() * 0.50)); // Match GalleryTopBar icon scale
        p.setFont(iconFont);
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, iconGlyph);
    };

    drawButton(m_backRect, kTopBarBackIcon, m_pressedZone == PressZone::Back);
    drawButton(m_closeRect, kTopBarCloseIcon, m_pressedZone == PressZone::Close);

    QFont titleFont("Roboto");
    titleFont.setPixelSize(qRound(height() * 0.33));
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(Qt::white);
    p.drawText(rect(), Qt::AlignCenter, m_title);
}

// ============================================================
// SettingsView
// ============================================================

SettingsView::SettingsView(QWidget* parent) : BaseView(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setStyleSheet("background-color: black;");

    m_topBar = new SettingsTopBar(this);
    connect(m_topBar, &SettingsTopBar::backTriggered, this, &SettingsView::onTopBarBackTriggered);
    connect(m_topBar, &SettingsTopBar::closeTriggered, this, &SettingsView::onTopBarCloseTriggered);

    m_scrollIndicator = new ScrollIndicator(this);
    connect(m_scrollIndicator, &ScrollIndicator::opacityChanged, this, QOverload<>::of(&QWidget::update));

    m_leftScrollAnim = new QPropertyAnimation(this, "leftScroll", this);
    m_leftScrollAnim->setDuration(m_cfg.SNAP_DURATION_MS);
    m_leftScrollAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_rightScrollAnim = new QPropertyAnimation(this, "rightScroll", this);
    m_rightScrollAnim->setDuration(m_cfg.SNAP_DURATION_MS);
    m_rightScrollAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_splitAnim = new QPropertyAnimation(this, "splitProgress", this);
    m_splitAnim->setDuration(m_cfg.SNAP_DURATION_MS);
    m_splitAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(&SettingsService::instance(), &SettingsService::applyCompleted,
            this, &SettingsView::onSettingsApplyCompleted);
    connect(&SettingsStore::instance(), &SettingsStore::settingsChanged, this,
            [this](const SettingsChangeEvent& change) {
                if (m_mode == PanelMode::Expanded &&
                    primaryVisibilityAffectedBySettingsChange(m_activePrimary,
                                                              change.changedKeys)) {
                    rebuildSecondaryRows(m_activePrimary);
                    m_rightScroll = qBound<qreal>(0.0, m_rightScroll, rightMaxScroll());
                    relayoutRows();
                    return;
                }
                refreshSecondaryRowsFromSnapshot(change.snapshot);
            });

    if (auto* storage = HardwareManager::instance().storage()) {
        auto refreshStorageRows = [this]() {
            if (m_mode != PanelMode::Expanded) return;
            if (!primaryVisibilityAffectedByStorageState(m_activePrimary)) return;
            rebuildSecondaryRows(m_activePrimary);
            m_rightScroll = qBound<qreal>(0.0, m_rightScroll, rightMaxScroll());
            relayoutRows();
        };

        connect(storage, &StorageManager::sdCardStateChanged, this,
                [refreshStorageRows](bool /*ready*/) { refreshStorageRows(); });
        connect(storage, &StorageManager::usbDiskStateChanged, this,
                [refreshStorageRows](bool /*ready*/) { refreshStorageRows(); });
    }

    buildPrimaryRows();
    rebuildSecondaryRows(0);
}

void SettingsView::onEnter() {
    m_mode = PanelMode::Single;
    m_activePrimary = -1;
    m_leftScroll = 0.0;
    m_rightScroll = 0.0;
    m_splitProgress = 0.0;

    m_leftScrollAnim->stop();
    m_rightScrollAnim->stop();
    m_splitAnim->stop();

    rebuildSecondaryRows(0);
    for (auto* row : m_primaryRows) row->setSelected(false);
    m_topBar->setTitle("Settings");
    relayoutRows();
    refreshSecondaryRowsFromStore();
    m_scrollIndicator->forceHide();
    update();
}

void SettingsView::onExit() {
    m_leftScrollAnim->stop();
    m_rightScrollAnim->stop();
    m_splitAnim->stop();
}

void SettingsView::onInteractionBegin(const InteractionEvent& /*event*/) {
    m_lastDx = 0;
    m_lastDy = 0;
    m_swipeAxis = SwipeAxis::None;
    m_dragStartLeft = m_leftScroll;
    m_dragStartRight = m_rightScroll;

    m_leftScrollAnim->stop();
    m_rightScrollAnim->stop();
}

InteractionUpdateDecision SettingsView::onInteractionUpdate(const InteractionEvent& event) {
    const QPoint start = event.startGlobal;
    const int dx = event.deltaFromStartGlobal.x();
    const int dy = event.deltaFromStartGlobal.y();
    m_lastDx = dx;
    m_lastDy = dy;

    if (m_swipeAxis == SwipeAxis::None) {
        if (std::abs(dx) > m_cfg.DEADZONE_PX || std::abs(dy) > m_cfg.DEADZONE_PX) {
            m_swipeAxis = (std::abs(dx) > std::abs(dy)) ? SwipeAxis::Horizontal : SwipeAxis::Vertical;
        }
    }
    if (m_swipeAxis != SwipeAxis::Vertical) return InteractionUpdateDecision::KeepOwner;

    if (m_mode == PanelMode::Expanded && m_splitProgress > 0.95) {
        m_scrollTarget = (start.x() > leftPanelWidth()) ? ScrollTarget::Right : ScrollTarget::Left;
    } else {
        m_scrollTarget = ScrollTarget::Left;
    }

    if (m_scrollTarget == ScrollTarget::Left) {
        qreal candidate = m_dragStartLeft - dy;
        setLeftScroll(applyOverscroll(candidate, leftMaxScroll()));
    } else {
        qreal candidate = m_dragStartRight - dy;
        setRightScroll(applyOverscroll(candidate, rightMaxScroll()));
    }
    return InteractionUpdateDecision::KeepOwner;
}

void SettingsView::onInteractionEnd(const InteractionEvent& event) {
    const QPoint start = event.startGlobal;
    const int dx = event.deltaFromStartGlobal.x();
    const float vy = event.velocityPxPerMs.y();
    if (m_swipeAxis == SwipeAxis::Horizontal) {
        const int edgeZone = qRound(width() * m_cfg.SWIPE_BACK_EDGE_RATIO);
        if (start.x() < edgeZone && dx > width() * m_cfg.SWIPE_BACK_DIST_RATIO) {
            if (m_mode == PanelMode::Expanded) {
                collapseToSingle();
            } else {
                triggerExitToCamera();
            }
            return;
        }
    }

    if (m_swipeAxis == SwipeAxis::Vertical) {
        settleScroll(m_scrollTarget == ScrollTarget::Left, vy);
    }
}

void SettingsView::onInteractionCancel() {
    m_swipeAxis = SwipeAxis::None;
}

void SettingsView::setLeftScroll(qreal v) {
    if (qFuzzyCompare(m_leftScroll, v)) return;
    m_leftScroll = v;
    relayoutRows();
    refreshTopMask();

    if (m_mode == PanelMode::Single || m_splitProgress < 0.99) {
        m_scrollIndicator->updateState(m_leftScroll, leftMaxScroll());
    }
}

void SettingsView::setRightScroll(qreal v) {
    if (qFuzzyCompare(m_rightScroll, v)) return;
    m_rightScroll = v;
    relayoutRows();
    refreshTopMask();

    if (m_mode == PanelMode::Expanded && m_splitProgress > 0.99) {
        m_scrollIndicator->updateState(m_rightScroll, rightMaxScroll());
    }
}

void SettingsView::setSplitProgress(qreal v) {
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(m_splitProgress, v)) return;
    m_splitProgress = v;
    relayoutRows();
    refreshTopMask();
    update();
}

void SettingsView::resizeEvent(QResizeEvent* /*event*/) {
    relayoutRows();
}

void SettingsView::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0));
    const int topH = topBarHeight();

    if (m_splitProgress > 0.01) {
        const int leftWTarget = leftPanelWidth();
        const int leftW = qRound(width() - (width() - leftWTarget) * m_splitProgress);
        const int dividerW = qMax(1, qRound(width() * m_cfg.DIVIDER_WIDTH_RATIO));
        const int dividerX = leftW;

        QColor divider(255, 255, 255, qRound(70 * m_splitProgress));
        p.fillRect(QRect(dividerX, topH, dividerW, height() - topH), divider);
    }

    if (m_mode == PanelMode::Expanded && m_splitProgress > 0.99) {
        const int leftWTarget = leftPanelWidth();
        const int dividerW = qMax(1, qRound(width() * m_cfg.DIVIDER_WIDTH_RATIO));
        const int rightX = leftWTarget + dividerW;
        QRect rightRect(rightX, topH, width() - rightX, height() - topH);
        m_scrollIndicator->paint(p, rightRect);
    } else {
        QRect singleRect(0, topH, width(), height() - topH);
        m_scrollIndicator->paint(p, singleRect);
    }
}

void SettingsView::onPrimaryRowActivated() {
    auto* row = qobject_cast<SettingsPrimaryRow*>(sender());
    const int index = m_primaryRows.indexOf(row);
    if (index < 0) return;

    if (m_mode == PanelMode::Expanded && m_activePrimary == index) {
        collapseToSingle();
    } else {
        expandPrimary(index);
    }
}

void SettingsView::onSecondaryRowActivated() {
    auto* row = qobject_cast<SettingsSecondaryRow*>(sender());
    if (!row) return;
    const SecondaryItemData item = row->data();

    auto buildAnchor = [this, row]() {
        BubbleAnchorContext anchor;
        anchor.pressPosGlobal = row->mapToGlobal(row->rect().center());
        anchor.triggerRectGlobal = QRect(row->mapToGlobal(QPoint(0, 0)), row->size());

        const QRect rowLocal = row->geometry();
        const QRect submenuLocal(rowLocal.x(),
                                 topBarHeight(),
                                 rowLocal.width(),
                                 qMax(1, height() - topBarHeight()));
        anchor.submenuRectGlobal = QRect(mapToGlobal(submenuLocal.topLeft()), submenuLocal.size());
        anchor.submenuContentTopGlobalY = anchor.submenuRectGlobal.top();
        anchor.submenuContentBottomGlobalY = anchor.submenuRectGlobal.bottom();
        anchor.referenceRowHeightPx = rowHeight();
        return anchor;
    };

    auto* app = qobject_cast<App*>(window());
    if (!app) return;

    switch (item.id) {
    case SettingID::Emissivity: {
        const SettingsSnapshot snapshot = SettingsStore::instance().current();
        bool ok = false;
        const float emissivity = clampEmissivity(
            snapshot.values.value(SettingKey::Emissivity, defaultValueForKey(SettingKey::Emissivity))
                .toFloat(&ok));
        const float current = ok ? emissivity : 0.95f;

        SliderBubble::Spec spec;
        spec.minValue = kEmissivitySliderMin;
        spec.maxValue = kEmissivitySliderMax;
        spec.step = kEmissivitySliderStep;
        spec.value = emissivityToSliderValue(current);
        spec.dismissOnCommit = true;
        auto previewTimer = std::make_shared<QTimer>();
        previewTimer->setSingleShot(true);
        previewTimer->setInterval(kPreviewThrottleMs);

        auto latestPreviewSliderValue = std::make_shared<int>(spec.value);
        auto previewDirty = std::make_shared<bool>(false);

        auto flushPreview = [latestPreviewSliderValue]() {
            SettingsPatch previewPatch;
            previewPatch.values.insert(SettingKey::Emissivity,
                                       QVariant(sliderValueToEmissivity(*latestPreviewSliderValue)));
            SettingsService::instance().preview(previewPatch);
        };

        connect(previewTimer.get(), &QTimer::timeout, this,
                [previewTimer, previewDirty, flushPreview]() {
                    if (!*previewDirty) return;
                    flushPreview();
                    *previewDirty = false;
                    previewTimer->start();
                });

        spec.onValueChanging = [row, previewTimer, latestPreviewSliderValue, previewDirty, flushPreview](
                                   int sliderValue) {
            *latestPreviewSliderValue = sliderValue;
            *previewDirty = true;
            row->setValueText(QString::number(sliderValueToEmissivity(sliderValue), 'f', 2));

            if (!previewTimer->isActive()) {
                flushPreview();
                *previewDirty = false;
                previewTimer->start();
            }
        };

        spec.onValueCommitted = [this, previewTimer, previewDirty](int sliderValue) {
            previewTimer->stop();
            *previewDirty = false;

            SettingsPatch patch;
            patch.values.insert(SettingKey::Emissivity, QVariant(sliderValueToEmissivity(sliderValue)));
            applyPatchFromUi(patch);
        };
        spec.onDismissed = [previewTimer]() { previewTimer->stop(); };

        app->showSliderBubble(spec, buildAnchor());
        return;
    }
    case SettingID::SeekVisionEnabled: {
        const SettingsSnapshot snapshot = SettingsStore::instance().current();
        const bool current = boolSettingFromSnapshot(snapshot, SettingKey::SeekVisionEnabled, true);
        SettingsPatch patch;
        patch.values.insert(SettingKey::SeekVisionEnabled, QVariant(!current));
        applyPatchFromUi(patch);
        return;
    }
    case SettingID::LegacySharpenEnabled: {
        const SettingsSnapshot snapshot = SettingsStore::instance().current();
        const bool current =
            boolSettingFromSnapshot(snapshot, SettingKey::LegacySharpenEnabled, false);
        SettingsPatch patch;
        patch.values.insert(SettingKey::LegacySharpenEnabled, QVariant(!current));
        applyPatchFromUi(patch);
        return;
    }
    case SettingID::AgcMode: {
        const SettingsSnapshot snapshot = SettingsStore::instance().current();
        bool ok = false;
        const int parsed = snapshot.values
                               .value(SettingKey::AgcMode, defaultValueForKey(SettingKey::AgcMode))
                               .toInt(&ok);
        const int mode = normalizeAgcModeInt(ok ? parsed : static_cast<int>(AgcMode::HistEqAuto));

        RadioListBubble::Spec spec;
        spec.items = {
            {"auto_histeq", "Auto (HistEQ AGC)"},
            {"linear_manual", "Linear"}
        };
        spec.selectedIndex = (mode == static_cast<int>(AgcMode::LinearManual)) ? 1 : 0;
        spec.dismissOnSelection = true;
        spec.onSelected = [this](int selectedIndex, const QString& /*id*/) {
            const int modeValue = (selectedIndex == 1)
                                      ? static_cast<int>(AgcMode::LinearManual)
                                      : static_cast<int>(AgcMode::HistEqAuto);
            SettingsPatch patch;
            patch.values.insert(SettingKey::AgcMode, QVariant(modeValue));
            applyPatchFromUi(patch);
        };

        app->showRadioListBubble(spec, buildAnchor());
        return;
    }
    case SettingID::LinearAgcMin: {
        const SettingsSnapshot snapshot = SettingsStore::instance().current();
        const float currentMin = qBound(
            kLinearAgcCelsiusMin,
            floatSettingFromSnapshot(snapshot, SettingKey::LinearAgcMinCelsius, 20.0f),
            kLinearAgcCelsiusMax);
        const float currentMax = qBound(
            kLinearAgcCelsiusMin,
            floatSettingFromSnapshot(snapshot, SettingKey::LinearAgcMaxCelsius, 80.0f),
            kLinearAgcCelsiusMax);

        StepperBubble::Spec spec;
        spec.minValue = static_cast<int>(kLinearAgcCelsiusMin);
        spec.maxValue =
            qMax(spec.minValue, qRound(currentMax) - kLinearAgcStep);
        spec.step = kLinearAgcStep;
        spec.value = qBound(spec.minValue, qRound(currentMin), spec.maxValue);
        spec.dismissOnCommit = false;
        spec.valueTextFormatter = [](int value) {
            return formatSignedCelsius(static_cast<float>(value), 0);
        };
        spec.onValueChanging = [row](int value) {
            row->setValueText(formatSignedCelsius(static_cast<float>(value), 0));
            SettingsPatch previewPatch;
            previewPatch.values.insert(SettingKey::LinearAgcMinCelsius,
                                       QVariant(static_cast<float>(value)));
            SettingsService::instance().preview(previewPatch);
        };
        spec.onValueCommitted = [this](int value) {
            SettingsPatch patch;
            patch.values.insert(SettingKey::LinearAgcMinCelsius,
                                QVariant(static_cast<float>(value)));
            applyPatchFromUi(patch);
        };

        app->showStepperBubble(spec, buildAnchor());
        return;
    }
    case SettingID::LinearAgcMax: {
        const SettingsSnapshot snapshot = SettingsStore::instance().current();
        const float currentMin = qBound(
            kLinearAgcCelsiusMin,
            floatSettingFromSnapshot(snapshot, SettingKey::LinearAgcMinCelsius, 20.0f),
            kLinearAgcCelsiusMax);
        const float currentMax = qBound(
            kLinearAgcCelsiusMin,
            floatSettingFromSnapshot(snapshot, SettingKey::LinearAgcMaxCelsius, 80.0f),
            kLinearAgcCelsiusMax);

        StepperBubble::Spec spec;
        spec.minValue =
            qMin(static_cast<int>(kLinearAgcCelsiusMax), qRound(currentMin) + kLinearAgcStep);
        spec.maxValue = static_cast<int>(kLinearAgcCelsiusMax);
        spec.step = kLinearAgcStep;
        spec.value = qBound(spec.minValue, qRound(currentMax), spec.maxValue);
        spec.dismissOnCommit = false;
        spec.valueTextFormatter = [](int value) {
            return formatSignedCelsius(static_cast<float>(value), 0);
        };
        spec.onValueChanging = [row](int value) {
            row->setValueText(formatSignedCelsius(static_cast<float>(value), 0));
            SettingsPatch previewPatch;
            previewPatch.values.insert(SettingKey::LinearAgcMaxCelsius,
                                       QVariant(static_cast<float>(value)));
            SettingsService::instance().preview(previewPatch);
        };
        spec.onValueCommitted = [this](int value) {
            SettingsPatch patch;
            patch.values.insert(SettingKey::LinearAgcMaxCelsius,
                                QVariant(static_cast<float>(value)));
            applyPatchFromUi(patch);
        };

        app->showStepperBubble(spec, buildAnchor());
        return;
    }
    case SettingID::ShutterAutoEnabled: {
        const SettingsSnapshot snapshot = SettingsStore::instance().current();
        const bool current = boolSettingFromSnapshot(snapshot, SettingKey::ShutterAutoEnabled, true);
        SettingsPatch patch;
        patch.values.insert(SettingKey::ShutterAutoEnabled, QVariant(!current));
        applyPatchFromUi(patch);
        return;
    }
    case SettingID::TriggerFlatSceneCorrection: {
        app->showTextModal(
            QStringLiteral("WARNING: Risk of image ghosting!\n"
                        "Fully cover lens with a flat object before continuing."),
            [app]() {
                QString error;
                if (!SettingsService::instance().triggerFlatSceneCorrection(&error)) {
                    qWarning() << "[Settings] Flat-scene correction failed:" << error;
                    app->showToast("FLAT-SCENE CORRECTION FAILED", ToastLevel::Warning);
                    return;
                }
                app->showToast("FLAT-SCENE CORRECTION TRIGGERED", ToastLevel::Info);
            },
            ModalLevel::Critical);
        return;
    }
    case SettingID::ThermographyOffset: {
        const SettingsSnapshot snapshot = SettingsStore::instance().current();
        const float currentOffset = floatSettingFromSnapshot(
            snapshot, SettingKey::ThermographyOffsetCelsius, 0.0f);

        StepperBubble::Spec spec;
        spec.minValue = kThermographyOffsetTenthsMin;
        spec.maxValue = kThermographyOffsetTenthsMax;
        spec.step = kThermographyOffsetTenthsStep;
        spec.value = thermographyOffsetToTenths(currentOffset);
        spec.dismissOnCommit = false;
        spec.valueTextFormatter = [](int value) {
            return formatSignedCelsius(thermographyTenthsToCelsius(value), 1);
        };
        spec.onValueChanging = [row](int value) {
            const float offset = thermographyTenthsToCelsius(value);
            row->setValueText(formatSignedCelsius(offset, 1));
            SettingsPatch previewPatch;
            previewPatch.values.insert(SettingKey::ThermographyOffsetCelsius, QVariant(offset));
            SettingsService::instance().preview(previewPatch);
        };
        spec.onValueCommitted = [this](int value) {
            SettingsPatch patch;
            patch.values.insert(SettingKey::ThermographyOffsetCelsius,
                                QVariant(thermographyTenthsToCelsius(value)));
            applyPatchFromUi(patch);
        };

        app->showStepperBubble(spec, buildAnchor());
        return;
    }
    case SettingID::TemperatureUnit: {
        const SettingsSnapshot snapshot = SettingsStore::instance().current();
        bool ok = false;
        const int parsed = snapshot.values
                               .value(SettingKey::TemperatureUnit,
                                      defaultValueForKey(SettingKey::TemperatureUnit))
                               .toInt(&ok);
        const int unitValue = normalizeTemperatureUnitInt(ok ? parsed
                                                             : static_cast<int>(TemperatureUnit::Celsius));

        RadioListBubble::Spec spec;
        spec.items = {
            {"celsius", formatTemperatureUnit(static_cast<int>(TemperatureUnit::Celsius))},
            {"fahrenheit", formatTemperatureUnit(static_cast<int>(TemperatureUnit::Fahrenheit))}
        };
        spec.selectedIndex = (unitValue == static_cast<int>(TemperatureUnit::Fahrenheit)) ? 1 : 0;
        spec.dismissOnSelection = true;
        spec.onSelected = [this](int selectedIndex, const QString& /*id*/) {
            const int unit = (selectedIndex == 1)
                                 ? static_cast<int>(TemperatureUnit::Fahrenheit)
                                 : static_cast<int>(TemperatureUnit::Celsius);
            SettingsPatch patch;
            patch.values.insert(SettingKey::TemperatureUnit, QVariant(unit));
            applyPatchFromUi(patch);
        };

        app->showRadioListBubble(spec, buildAnchor());
        return;
    }
    case SettingID::StoragePriority: {
        const SettingsSnapshot snapshot = SettingsStore::instance().current();
        bool ok = false;
        const int parsed = snapshot.values
                               .value(SettingKey::StoragePriority,
                                      defaultValueForKey(SettingKey::StoragePriority))
                               .toInt(&ok);
        const int priorityValue = normalizeStoragePriorityInt(
            ok ? parsed : static_cast<int>(StoragePriority::SdFirst));

        RadioListBubble::Spec spec;
        spec.items = {
            {"sd_first", "SD Card First"},
            {"usb_first", "USB Disk First"}
        };
        spec.selectedIndex =
            (priorityValue == static_cast<int>(StoragePriority::UsbFirst)) ? 1 : 0;
        spec.dismissOnSelection = true;
        spec.onSelected = [this](int selectedIndex, const QString& /*id*/) {
            const int priority = (selectedIndex == 1)
                                     ? static_cast<int>(StoragePriority::UsbFirst)
                                     : static_cast<int>(StoragePriority::SdFirst);
            SettingsPatch patch;
            patch.values.insert(SettingKey::StoragePriority, QVariant(priority));
            applyPatchFromUi(patch);
        };

        app->showRadioListBubble(spec, buildAnchor());
        return;
    }
    case SettingID::OSDOverlay: {
        const SettingsSnapshot snapshot = SettingsStore::instance().current();
        const bool current = boolSettingFromSnapshot(snapshot, SettingKey::SaveMarkerInMedia, true);
        SettingsPatch patch;
        patch.values.insert(SettingKey::SaveMarkerInMedia, QVariant(!current));
        applyPatchFromUi(patch);
        return;
    }
    case SettingID::HideMarkerWhenHudHidden: {
        const SettingsSnapshot snapshot = SettingsStore::instance().current();
        const bool current =
            boolSettingFromSnapshot(snapshot, SettingKey::HideMarkerWhenHudHidden, false);
        SettingsPatch patch;
        patch.values.insert(SettingKey::HideMarkerWhenHudHidden, QVariant(!current));
        applyPatchFromUi(patch);
        return;
    }
    case SettingID::ScreenBrightness: {
        const SettingsSnapshot snapshot = SettingsStore::instance().current();
        const int current = clampPercentInt(
            intSettingFromSnapshot(snapshot, SettingKey::ScreenBrightnessPercent, 80),
            kScreenBrightnessPercentMin,
            kScreenBrightnessPercentMax);

        SliderBubble::Spec spec;
        spec.iconGlyph = kScreenBrightnessIconGlyph;
        spec.minValue = kScreenBrightnessPercentMin;
        spec.maxValue = kScreenBrightnessPercentMax;
        spec.step = 1;
        spec.value = current;
        spec.dismissOnCommit = true;

        auto previewTimer = std::make_shared<QTimer>();
        previewTimer->setSingleShot(true);
        previewTimer->setInterval(kPreviewThrottleMs);

        auto latestPreviewPercent = std::make_shared<int>(spec.value);
        auto previewDirty = std::make_shared<bool>(false);

        auto flushPreview = [latestPreviewPercent]() {
            SettingsPatch previewPatch;
            previewPatch.values.insert(SettingKey::ScreenBrightnessPercent,
                                       QVariant(*latestPreviewPercent));
            SettingsService::instance().preview(previewPatch);
        };

        connect(previewTimer.get(),
                &QTimer::timeout,
                this,
                [previewTimer, previewDirty, flushPreview]() {
                    if (!*previewDirty) return;
                    flushPreview();
                    *previewDirty = false;
                    previewTimer->start();
                });

        spec.onValueChanging =
            [row, previewTimer, latestPreviewPercent, previewDirty, flushPreview](int value) {
                *latestPreviewPercent = value;
                *previewDirty = true;
                row->setValueText(formatPercent(value,
                                                kScreenBrightnessPercentMin,
                                                kScreenBrightnessPercentMax));

                if (!previewTimer->isActive()) {
                    flushPreview();
                    *previewDirty = false;
                    previewTimer->start();
                }
            };

        spec.onValueCommitted = [this, previewTimer, previewDirty](int value) {
            previewTimer->stop();
            *previewDirty = false;

            SettingsPatch patch;
            patch.values.insert(SettingKey::ScreenBrightnessPercent, QVariant(value));
            applyPatchFromUi(patch);
        };
        spec.onDismissed = [previewTimer]() { previewTimer->stop(); };

        app->showSliderBubble(spec, buildAnchor());
        return;
    }
    case SettingID::AudioVolume: {
        const SettingsSnapshot snapshot = SettingsStore::instance().current();
        const int current = clampPercentInt(
            intSettingFromSnapshot(snapshot, SettingKey::AudioVolumePercent, 50),
            kAudioVolumePercentMin,
            kAudioVolumePercentMax);

        SliderBubble::Spec spec;
        spec.iconGlyph = kAudioVolumeIconGlyph;
        spec.minValue = kAudioVolumePercentMin;
        spec.maxValue = kAudioVolumePercentMax;
        spec.step = 1;
        spec.value = current;
        spec.dismissOnCommit = true;

        auto previewTimer = std::make_shared<QTimer>();
        previewTimer->setSingleShot(true);
        previewTimer->setInterval(kPreviewThrottleMs);

        auto latestPreviewPercent = std::make_shared<int>(spec.value);
        auto previewDirty = std::make_shared<bool>(false);

        auto flushPreview = [latestPreviewPercent]() {
            SettingsPatch previewPatch;
            previewPatch.values.insert(SettingKey::AudioVolumePercent,
                                       QVariant(*latestPreviewPercent));
            SettingsService::instance().preview(previewPatch);
        };

        connect(previewTimer.get(),
                &QTimer::timeout,
                this,
                [previewTimer, previewDirty, flushPreview]() {
                    if (!*previewDirty) return;
                    flushPreview();
                    *previewDirty = false;
                    previewTimer->start();
                });

        spec.onValueChanging =
            [row, previewTimer, latestPreviewPercent, previewDirty, flushPreview](int value) {
                *latestPreviewPercent = value;
                *previewDirty = true;
                row->setValueText(formatPercent(value,
                                                kAudioVolumePercentMin,
                                                kAudioVolumePercentMax));

                if (!previewTimer->isActive()) {
                    flushPreview();
                    *previewDirty = false;
                    previewTimer->start();
                }
            };

        spec.onValueCommitted = [this, previewTimer, previewDirty](int value) {
            previewTimer->stop();
            *previewDirty = false;

            SettingsPatch patch;
            patch.values.insert(SettingKey::AudioVolumePercent, QVariant(value));
            applyPatchFromUi(patch);
        };
        spec.onDismissed = [previewTimer]() { previewTimer->stop(); };

        app->showSliderBubble(spec, buildAnchor());
        return;
    }
    case SettingID::Palette:
        emit EventBus::instance().cameraRequested(QRect(), TransitionMode::Instant);
        emit EventBus::instance().paletteSelectorRequested();
        return;
    case SettingID::SdCardSafeEject:
    case SettingID::UsbDiskSafeEject: {
        const bool sdCard = (item.id == SettingID::SdCardSafeEject);
        const QString targetName = sdCard ? QStringLiteral("SD Card")
                                          : QStringLiteral("USB Disk");
        app->showTextModal(
            QString("Safely eject %1?\nWait for completion before unplugging.").arg(targetName),
            [this, app, targetName, sdCard]() {
                auto* storage = HardwareManager::instance().storage();
                if (!storage) {
                    app->showToast("STORAGE UNAVAILABLE", ToastLevel::Warning);
                    return;
                }

                QString error;
                const StorageVolume targetVolume = sdCard
                                                       ? StorageVolume::SdCard
                                                       : StorageVolume::UsbDisk;
                const bool ok = storage->safeEjectVolume(targetVolume, &error);

                if (!ok) {
                    qWarning() << "[Settings] Safe eject failed:" << targetName
                               << "reason:" << error;
                    app->showToast(targetName + " eject failed", ToastLevel::Warning);
                } else {
                    app->showToast(targetName + " can be removed", ToastLevel::Info);
                }

                if (m_mode == PanelMode::Expanded &&
                    primaryVisibilityAffectedByStorageState(m_activePrimary)) {
                    rebuildSecondaryRows(m_activePrimary);
                    m_rightScroll = qBound<qreal>(0.0, m_rightScroll, rightMaxScroll());
                    relayoutRows();
                }
            },
            ModalLevel::Normal);
        return;
    }
    case SettingID::SdCardFormat:
    case SettingID::UsbDiskFormat: {
        const bool sdCard = (item.id == SettingID::SdCardFormat);
        const QString targetName = sdCard ? QStringLiteral("SD Card")
                                          : QStringLiteral("USB Disk");
        app->showTextModal(QString("Format %1?\nAll files will be erased.").arg(targetName),
                           [this, app, targetName, sdCard]() {
                               auto* storage = HardwareManager::instance().storage();
                               if (!storage) {
                                   app->showToast("STORAGE UNAVAILABLE", ToastLevel::Warning);
                                   return;
                               }

                               QString error;
                               const StorageVolume targetVolume = sdCard
                                                                      ? StorageVolume::SdCard
                                                                      : StorageVolume::UsbDisk;
                               const bool ok = storage->formatVolume(targetVolume, &error);

                               if (!ok) {
                                   qWarning() << "[Settings] Format failed:" << targetName
                                              << "reason:" << error;
                                   app->showToast(targetName + " format failed", ToastLevel::Warning);
                               } else {
                                   app->showToast(targetName + " formatted", ToastLevel::Info);
                               }

                               if (m_mode == PanelMode::Expanded &&
                                   primaryVisibilityAffectedByStorageState(m_activePrimary)) {
                                   rebuildSecondaryRows(m_activePrimary);
                                   m_rightScroll = qBound<qreal>(0.0, m_rightScroll, rightMaxScroll());
                                   relayoutRows();
                               }
                           },
                           ModalLevel::Critical);
        return;
    }
    case SettingID::SdCardCapacity:
    case SettingID::UsbDiskCapacity:
        return;
    }
}

void SettingsView::onTopBarBackTriggered() {
    if (m_mode == PanelMode::Expanded) {
        collapseToSingle();
    } else {
        triggerExitToCamera();
    }
}

void SettingsView::onTopBarCloseTriggered() {
    triggerExitToCamera();
}

void SettingsView::buildPrimaryRows() {
    for (auto* row : m_primaryRows) delete row;
    m_primaryRows.clear();

    for (const auto& item : kMenuBlueprint) {
        auto* row = new SettingsPrimaryRow(this);
        row->setData(item);
        connect(row, &SettingsPrimaryRow::activated, this, &SettingsView::onPrimaryRowActivated);
        m_primaryRows.append(row);
    }
}

void SettingsView::rebuildSecondaryRows(int primaryIndex) {
    for (auto* row : m_secondaryRows) delete row;
    m_secondaryRows.clear();

    if (primaryIndex < 0 || primaryIndex >= kMenuBlueprint.size()) return;

    const auto items = visibleSecondaryItems(primaryIndex, SettingsStore::instance().current());
    for (const auto& item : items) {
        auto* row = new SettingsSecondaryRow(this);
        row->setData(item);
        connect(row, &SettingsSecondaryRow::activated, this, &SettingsView::onSecondaryRowActivated);
        m_secondaryRows.append(row);
    }

    refreshSecondaryRowsFromStore();
}

void SettingsView::relayoutRows() {
    const int topH = topBarHeight();
    const int rowH = rowHeight();

    m_topBar->setGeometry(0, 0, width(), topH);

    const int leftWTarget = leftPanelWidth();
    const int leftW = qRound(width() - (width() - leftWTarget) * m_splitProgress);
    const int dividerW = qMax(1, qRound(width() * m_cfg.DIVIDER_WIDTH_RATIO));
    const int rightW = qMax(0, width() - leftW - dividerW);
    const int rightX = qRound(width() * (1.0 - m_splitProgress) + (leftW + dividerW) * m_splitProgress);

    for (int i = 0; i < m_primaryRows.size(); ++i) {
        auto* row = m_primaryRows[i];
        row->setTargetLayoutWidth(leftWTarget);
        row->setSplitProgress(m_splitProgress);
        row->setSelected(i == m_activePrimary);
        const bool isLast = (i == m_primaryRows.size() - 1);
        const bool isSelected = (i == m_activePrimary);
        const bool isAboveSelected = (m_activePrimary >= 0 && i == m_activePrimary - 1);
        row->setBottomDividerVisible(!(isLast || isSelected || isAboveSelected));

        const int y = qRound(topH + i * rowH - m_leftScroll);
        row->setGeometry(0, y, leftW, rowH);
        row->setVisible(y < height() && (y + rowH) > topH - rowH);
    }

    for (int i = 0; i < m_secondaryRows.size(); ++i) {
        auto* row = m_secondaryRows[i];
        row->setBottomDividerVisible(i != m_secondaryRows.size() - 1);
        const int y = qRound(topH + i * rowH - m_rightScroll);
        row->setGeometry(rightX, y, rightW, rowH);
        const bool visible = (m_splitProgress > 0.01) && (y < height()) && ((y + rowH) > topH - rowH);
        row->setVisible(visible);
    }

    m_topBar->raise();
    update();
}

void SettingsView::refreshTopMask() {
    const bool leftUnderTop = m_leftScroll > 0.5;
    const bool rightUnderTop = (m_splitProgress > 0.95) && (m_rightScroll > 0.5);
    m_topBar->setMaskOpacity((leftUnderTop || rightUnderTop) ? 1.0 : 0.0);
}

int SettingsView::topBarHeight() const {
    return qMax(44, qRound(height() * m_cfg.TOPBAR_H_RATIO));
}

int SettingsView::rowHeight() const {
    return qMax(40, qRound(height() * m_cfg.ROW_H_RATIO));
}

int SettingsView::leftPanelWidth() const {
    return qRound(width() * m_cfg.LEFT_PANEL_RATIO);
}

qreal SettingsView::leftMaxScroll() const {
    const int contentH = m_primaryRows.size() * rowHeight();
    const int viewH = qMax(0, height() - topBarHeight());
    return qMax(0, contentH - viewH);
}

qreal SettingsView::rightMaxScroll() const {
    const int contentH = m_secondaryRows.size() * rowHeight();
    const int viewH = qMax(0, height() - topBarHeight());
    return qMax(0, contentH - viewH);
}

qreal SettingsView::applyOverscroll(qreal candidate, qreal maxScroll) const {
    if (candidate < 0.0) return candidate * m_cfg.OVERSCROLL_FRICTION;
    if (candidate > maxScroll) return maxScroll + (candidate - maxScroll) * m_cfg.OVERSCROLL_FRICTION;
    return candidate;
}

void SettingsView::settleScroll(bool leftPanel, float velocity) {
    const qreal current = leftPanel ? m_leftScroll : m_rightScroll;
    const qreal maxScroll = leftPanel ? leftMaxScroll() : rightMaxScroll();
    qreal target = current;

    if (current < 0.0) {
        target = 0.0;
    } else if (current > maxScroll) {
        target = maxScroll;
    } else {
        target = qBound(0.0, current - (velocity * 140.0f), maxScroll);
    }

    auto* anim = leftPanel ? m_leftScrollAnim : m_rightScrollAnim;
    anim->stop();
    anim->setStartValue(current);
    anim->setEndValue(target);
    anim->start();
}

void SettingsView::collapseToSingle() {
    if (m_mode == PanelMode::Single && m_splitProgress < 0.01) return;

    m_mode = PanelMode::Single;
    m_activePrimary = -1;
    m_topBar->setTitle("Settings");
    for (auto* row : m_primaryRows) row->setSelected(false);

    m_splitAnim->stop();
    m_splitAnim->setStartValue(m_splitProgress);
    m_splitAnim->setEndValue(0.0);
    m_splitAnim->start();
    m_scrollIndicator->forceHide();
}

void SettingsView::expandPrimary(int primaryIndex) {
    if (primaryIndex < 0 || primaryIndex >= kMenuBlueprint.size()) return;

    m_activePrimary = primaryIndex;
    for (int i = 0; i < m_primaryRows.size(); ++i) {
        m_primaryRows[i]->setSelected(i == m_activePrimary);
    }

    rebuildSecondaryRows(primaryIndex);
    m_rightScroll = 0.0;
    refreshTopMask();
    m_topBar->setTitle(kMenuBlueprint[primaryIndex].title);

    if (m_mode == PanelMode::Single) {
        m_mode = PanelMode::Expanded;
        m_splitAnim->stop();
        m_splitAnim->setStartValue(m_splitProgress);
        m_splitAnim->setEndValue(1.0);
        m_splitAnim->start();
    } else {
        m_mode = PanelMode::Expanded;
        setSplitProgress(1.0);
    }

    relayoutRows();
    m_scrollIndicator->forceHide();
}

void SettingsView::triggerExitToCamera() {
    emit EventBus::instance().cameraRequested(QRect(), TransitionMode::Auto);
}

void SettingsView::refreshSecondaryRowsFromSnapshot(const SettingsSnapshot& snapshot) {
    StorageVolumeStatus sdStatus;
    StorageVolumeStatus usbStatus;
    if (auto* storage = HardwareManager::instance().storage()) {
        sdStatus = storage->volumeStatus(StorageVolume::SdCard);
        usbStatus = storage->volumeStatus(StorageVolume::UsbDisk);
    }

    for (auto* row : m_secondaryRows) {
        const SecondaryItemData item = row->data();
        row->setValueText(QString());
        row->setToggleOn(false);

        switch (item.id) {
        case SettingID::Emissivity: {
            bool ok = false;
            const float parsed =
                snapshot.values.value(SettingKey::Emissivity, defaultValueForKey(SettingKey::Emissivity))
                    .toFloat(&ok);
            const float value = clampEmissivity(ok ? parsed : 0.95f);
            row->setValueText(QString::number(value, 'f', 2));
            break;
        }
        case SettingID::SeekVisionEnabled: {
            const bool enabled = boolSettingFromSnapshot(snapshot, SettingKey::SeekVisionEnabled, true);
            row->setToggleOn(enabled);
            break;
        }
        case SettingID::LegacySharpenEnabled: {
            const bool enabled =
                boolSettingFromSnapshot(snapshot, SettingKey::LegacySharpenEnabled, false);
            row->setToggleOn(enabled);
            break;
        }
        case SettingID::AgcMode: {
            bool ok = false;
            const int parsed = snapshot.values
                                   .value(SettingKey::AgcMode, defaultValueForKey(SettingKey::AgcMode))
                                   .toInt(&ok);
            const int mode = normalizeAgcModeInt(
                ok ? parsed : static_cast<int>(AgcMode::HistEqAuto));
            row->setValueText(formatAgcMode(mode));
            break;
        }
        case SettingID::LinearAgcMin: {
            const float value =
                floatSettingFromSnapshot(snapshot, SettingKey::LinearAgcMinCelsius, 20.0f);
            row->setValueText(formatSignedCelsius(value, 0));
            break;
        }
        case SettingID::LinearAgcMax: {
            const float value =
                floatSettingFromSnapshot(snapshot, SettingKey::LinearAgcMaxCelsius, 80.0f);
            row->setValueText(formatSignedCelsius(value, 0));
            break;
        }
        case SettingID::ShutterAutoEnabled: {
            const bool enabled = boolSettingFromSnapshot(snapshot, SettingKey::ShutterAutoEnabled, true);
            row->setToggleOn(enabled);
            break;
        }
        case SettingID::ThermographyOffset: {
            const float value =
                floatSettingFromSnapshot(snapshot, SettingKey::ThermographyOffsetCelsius, 0.0f);
            row->setValueText(formatSignedCelsius(value, 1));
            break;
        }
        case SettingID::TemperatureUnit: {
            bool ok = false;
            const int parsed = snapshot.values
                                   .value(SettingKey::TemperatureUnit,
                                          defaultValueForKey(SettingKey::TemperatureUnit))
                                   .toInt(&ok);
            const int unit = normalizeTemperatureUnitInt(
                ok ? parsed : static_cast<int>(TemperatureUnit::Celsius));
            row->setValueText(formatTemperatureUnit(unit));
            break;
        }
        case SettingID::StoragePriority: {
            bool ok = false;
            const int parsed = snapshot.values
                                   .value(SettingKey::StoragePriority,
                                          defaultValueForKey(SettingKey::StoragePriority))
                                   .toInt(&ok);
            const int priority = normalizeStoragePriorityInt(
                ok ? parsed : static_cast<int>(StoragePriority::SdFirst));
            row->setValueText(formatStoragePriority(priority));
            break;
        }
        case SettingID::SdCardCapacity:
            row->setValueText(formatStorageCapacity(sdStatus));
            break;
        case SettingID::SdCardSafeEject:
            break;
        case SettingID::UsbDiskCapacity:
            row->setValueText(formatStorageCapacity(usbStatus));
            break;
        case SettingID::UsbDiskSafeEject:
            break;
        case SettingID::OSDOverlay: {
            const bool enabled = boolSettingFromSnapshot(snapshot, SettingKey::SaveMarkerInMedia, true);
            row->setToggleOn(enabled);
            break;
        }
        case SettingID::HideMarkerWhenHudHidden: {
            const bool enabled =
                boolSettingFromSnapshot(snapshot, SettingKey::HideMarkerWhenHudHidden, false);
            row->setToggleOn(enabled);
            break;
        }
        case SettingID::ScreenBrightness: {
            const int value = clampPercentInt(
                intSettingFromSnapshot(snapshot, SettingKey::ScreenBrightnessPercent, 80),
                kScreenBrightnessPercentMin,
                kScreenBrightnessPercentMax);
            row->setValueText(formatPercent(value,
                                            kScreenBrightnessPercentMin,
                                            kScreenBrightnessPercentMax));
            break;
        }
        case SettingID::AudioVolume: {
            const int value = clampPercentInt(
                intSettingFromSnapshot(snapshot, SettingKey::AudioVolumePercent, 50),
                kAudioVolumePercentMin,
                kAudioVolumePercentMax);
            row->setValueText(formatPercent(value,
                                            kAudioVolumePercentMin,
                                            kAudioVolumePercentMax));
            break;
        }
        case SettingID::TriggerFlatSceneCorrection:
        case SettingID::SdCardFormat:
        case SettingID::UsbDiskFormat:
        case SettingID::Palette:
            break;
        }
    }
}

void SettingsView::refreshSecondaryRowsFromStore() {
    refreshSecondaryRowsFromSnapshot(SettingsStore::instance().current());
}

void SettingsView::applyPatchFromUi(const SettingsPatch& patch) {
    if (patch.isEmpty()) return;

    m_applyInFlight = true;
    SettingsService::instance().apply(patch);
}

void SettingsView::onSettingsApplyCompleted(const SettingsService::ApplyResult& result) {
    if (!m_applyInFlight) return;
    m_applyInFlight = false;

    if (result.code == SettingsService::ApplyCode::Ok ||
        result.code == SettingsService::ApplyCode::NoChange) {
        return;
    }

    auto* app = qobject_cast<App*>(window());
    if (!app) return;

    if (result.code == SettingsService::ApplyCode::RuntimeApplyFailed && result.persisted) {
        app->showToast("APPLY DEFERRED", ToastLevel::Warning);
        return;
    }

    app->showToast("SET FAILED", ToastLevel::Warning);
}
