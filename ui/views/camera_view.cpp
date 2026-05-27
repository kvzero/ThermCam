#include "camera_view.h"

#include "core/event_bus.h"
#include "core/global_context.h"
#include "core/settings_store.h"
#include "hardware/hardware_manager.h"
#include "hardware/imaging/thermal_camera.h"
#include "processing/thermal_processor.h"
#include "services/capture_service.h"
#include "services/settings_service.h"
#include "ui/widgets/capsule_button.h"
#include "ui/widgets/mode_selector.h"
#include "ui/widgets/status_bar.h"
#include "ui/overlays/palette_selector.h"

#include <QDebug>
#include <QEasingCurve>
#include <QMetaObject>
#include <QPainter>
#include <QResizeEvent>
#include <QVariant>
#include <cmath>

namespace {
bool isFahrenheitFromSnapshot(const SettingsSnapshot& snapshot) {
    bool ok = false;
    const int raw = snapshot.values
                        .value(SettingKey::TemperatureUnit,
                               QVariant::fromValue(static_cast<int>(TemperatureUnit::Celsius)))
                        .toInt(&ok);
    if (!ok) return false;
    return raw == static_cast<int>(TemperatureUnit::Fahrenheit);
}

ThermalPalette::Id paletteFromSnapshot(const SettingsSnapshot& snapshot) {
    bool ok = false;
    const int raw = snapshot.values
                        .value(SettingKey::Palette,
                               QVariant::fromValue(static_cast<int>(ThermalPalette::Id::Spectra)))
                        .toInt(&ok);
    if (!ok) return ThermalPalette::Id::Spectra;
    if (raw < 0 || raw >= static_cast<int>(ThermalPalette::Id::Count)) {
        return ThermalPalette::Id::Spectra;
    }
    return static_cast<ThermalPalette::Id>(raw);
}

bool boolSettingFromSnapshot(const SettingsSnapshot& snapshot,
                             SettingKey key,
                             bool fallback) {
    const QVariant value = snapshot.values.value(key, QVariant(fallback));
    if (!value.canConvert<bool>()) return fallback;
    return value.toBool();
}
} // namespace

CameraView::CameraView(QWidget* parent) : BaseView(parent) {
    /* --- Logic --- */
    m_processor = new ThermalProcessor(this);
    m_processor->setTargetSize(GlobalContext::instance().screenSize());

    const SettingsSnapshot snapshot = SettingsStore::instance().current();
    m_currentPalette = paletteFromSnapshot(snapshot);
    m_processor->setPalette(m_currentPalette);

    /* --- HUD Widgets --- */
    m_statusBar = new StatusBar(this);
    m_capsuleButton = new CapsuleButton(this);
    m_modeSelector = new ModeSelector(this);

    m_paletteSelector = new PaletteSelector(this);
    m_paletteSelector->setGeometry(rect());

    m_paletteOpenTimer = new QTimer(this);
    m_paletteOpenTimer->setSingleShot(true);
    connect(m_paletteOpenTimer, &QTimer::timeout, this, [this]() {
        if (!m_paletteSelector || m_paletteSelector->isPresented()) return;
        m_paletteSelector->present(m_currentPalette);
        refreshPalettePreviews();
    });

    auto setupHudAnim = [this](QPropertyAnimation** anim, QWidget* target) {
        *anim = new QPropertyAnimation(target, "pos", this);
        (*anim)->setDuration(m_hudCfg.ANIM_DURATION_MS);
        (*anim)->setEasingCurve(QEasingCurve::OutCubic);
    };
    setupHudAnim(&m_statusBarAnim, m_statusBar);
    setupHudAnim(&m_capsuleAnim, m_capsuleButton);
    setupHudAnim(&m_modeSelectorAnim, m_modeSelector);

    m_hudAnimGroup = new QParallelAnimationGroup(this);
    m_hudAnimGroup->addAnimation(m_statusBarAnim);
    m_hudAnimGroup->addAnimation(m_capsuleAnim);
    m_hudAnimGroup->addAnimation(m_modeSelectorAnim);

    /* --- Shutter Feedback --- */
    m_shutterAnim = new QPropertyAnimation(this, "shutterProgress", this);
    m_shutterAnim->setDuration(m_cfg.ANIM_DURATION_MS);
    m_shutterAnim->setEasingCurve(QEasingCurve::OutCubic);

    /* --- Runtime Event Wiring --- */
    m_isFahrenheit = isFahrenheitFromSnapshot(snapshot);
    m_hideMarkerWhenHudHidden =
        boolSettingFromSnapshot(snapshot, SettingKey::HideMarkerWhenHudHidden, false);

    connect(&SettingsService::instance(), &SettingsService::unitChanged, this,
            [this](bool isFahrenheit) {
                if (m_isFahrenheit == isFahrenheit) return;
                m_isFahrenheit = isFahrenheit;
                update();
            });

    connect(&SettingsService::instance(), &SettingsService::hudHideMarkerChanged, this,
            [this](bool enabled) {
                if (m_hideMarkerWhenHudHidden == enabled) return;
                m_hideMarkerWhenHudHidden = enabled;
                update();
            });

    connect(&SettingsService::instance(), &SettingsService::paletteChanged, this,
            [this](int paletteId) {
                if (paletteId < 0 || paletteId >= static_cast<int>(ThermalPalette::Id::Count)) return;
                applyPalette(static_cast<ThermalPalette::Id>(paletteId), false);
                if (m_paletteSelector && m_paletteSelector->isPresented()) {
                    refreshPalettePreviews();
                }
            });

    connect(&EventBus::instance(), &EventBus::paletteSelectorRequested, this,
            [this]() { openPaletteSelector(); });

    connect(m_paletteSelector, &PaletteSelector::previewSelectionChanged, this,
            [this](ThermalPalette::Id id) {
                applyPalette(id, false);
                refreshPalettePreviews();
            });

    connect(m_paletteSelector, &PaletteSelector::selectionCommitted, this,
            [this](ThermalPalette::Id id) {
                SettingsPatch patch;
                patch.values.insert(SettingKey::Palette,
                                    QVariant::fromValue(static_cast<int>(id)));
                SettingsService::instance().apply(patch);

                m_lastHapticPalette = ThermalPalette::Id::Count;
                setHudVisible(true, true);
            });

    updateHudLayout();
    applyHudState(true);
}

CameraView::~CameraView() {
    // HardwareManager handles camera lifecycle, we just disconnect signals.
}

void CameraView::onEnter() {
    qInfo() << "[CameraView] Enter: Connecting Hardware";
    connectHardware();

    if (m_paletteSelector && m_paletteSelector->isPresented()) {
        m_paletteSelector->dismiss(false);
    }
    if (m_paletteOpenTimer) {
        m_paletteOpenTimer->stop();
    }

    setHudVisible(true, false);
}

void CameraView::onExit() {
    qInfo() << "[CameraView] Exit: Disconnecting Hardware";

    if (m_paletteOpenTimer) {
        m_paletteOpenTimer->stop();
    }
    if (m_paletteSelector && m_paletteSelector->isPresented()) {
        m_paletteSelector->dismiss(false);
    }

    disconnectHardware();
}

void CameraView::applyPalette(ThermalPalette::Id palette, bool emitHaptic) {
    if (palette == ThermalPalette::Id::Count) return;
    if (static_cast<int>(palette) >= static_cast<int>(ThermalPalette::Id::Count)) {
        return;
    }

    m_currentPalette = palette;
    m_processor->setPalette(palette);

    if (emitHaptic && palette != m_lastHapticPalette) {
        emit EventBus::instance().hapticRequested(4);
        m_lastHapticPalette = palette;
    }
}

void CameraView::openPaletteSelector() {
    if (!m_paletteSelector || !m_paletteOpenTimer) return;
    if (m_paletteSelector->isPresented() || m_paletteOpenTimer->isActive()) return;

    setHudVisible(false, true);
    m_lastHapticPalette = m_currentPalette;
    m_paletteOpenTimer->start(m_hudCfg.PALETTE_SHOW_DELAY_MS);
}

void CameraView::closePaletteSelector(bool commitSelection) {
    if (m_paletteOpenTimer) {
        m_paletteOpenTimer->stop();
    }

    if (!m_paletteSelector || !m_paletteSelector->isPresented()) {
        if (!commitSelection) {
            setHudVisible(true, true);
        }
        return;
    }

    m_paletteSelector->dismiss(commitSelection);

    if (!commitSelection) {
        m_lastHapticPalette = ThermalPalette::Id::Count;
        setHudVisible(true, true);
    }
}

void CameraView::refreshPalettePreviews() {
    if (!m_paletteSelector || !m_paletteSelector->isPresented()) return;

    const QSize previewSize = m_paletteSelector->previewFrameSize();
    if (previewSize.isEmpty()) return;

    const int current = static_cast<int>(m_currentPalette);
    const int minIndex = qMax(0, current - 2);
    const int maxIndex = qMin(static_cast<int>(ThermalPalette::Id::Count) - 1, current + 2);

    for (int i = minIndex; i <= maxIndex; ++i) {
        const auto id = static_cast<ThermalPalette::Id>(i);
        const QImage frame = m_processor->renderPreview(id, previewSize);
        if (!frame.isNull()) {
            m_paletteSelector->setPreviewFrame(id, frame);
        }
    }
}

void CameraView::connectHardware() {
    auto* camera = HardwareManager::instance().camera();
    if (!camera) return;

    connect(camera, &ThermalCamera::rawFrameReady,
            m_processor, &ThermalProcessor::processFrame, Qt::UniqueConnection);

    connect(m_processor, &ThermalProcessor::frameReady,
            this, &CameraView::updateFrame, Qt::UniqueConnection);

    connect(m_processor, &ThermalProcessor::frameReady,
            &CaptureService::instance(), &CaptureService::onFrameReady, Qt::UniqueConnection);
}

void CameraView::disconnectHardware() {
    auto* camera = HardwareManager::instance().camera();
    if (camera) {
        disconnect(camera, nullptr, m_processor, nullptr);
    }
    disconnect(m_processor, nullptr, this, nullptr);
}

void CameraView::handleKeyShortPress() {
    if (m_paletteSelector && m_paletteSelector->isPresented()) {
        closePaletteSelector(true);
        return;
    }

    if (CaptureService::instance().currentMode() == CaptureMode::Photo) {
        m_shutterAnim->stop();
        m_shutterAnim->setStartValue(1.0);
        m_shutterAnim->setEndValue(0.0);
        m_shutterAnim->start();
    }

    CaptureService::instance().handlePhysicalTrigger();
}

void CameraView::resetTransientUi() {
    if (m_modeSelector) {
        QMetaObject::invokeMethod(m_modeSelector, "collapse");
    }
}

/* --- Gesture Implementations --- */

void CameraView::onInteractionBegin(const InteractionEvent& /*event*/) {
    m_swipeAxis = SwipeAxis::None;
}

InteractionUpdateDecision CameraView::onInteractionUpdate(const InteractionEvent& event) {
    if (m_paletteSelector && m_paletteSelector->isPresented()) {
        return InteractionUpdateDecision::KeepOwner;
    }

    const int dx = event.deltaFromStartGlobal.x();
    const int dy = event.deltaFromStartGlobal.y();
    if (m_swipeAxis == SwipeAxis::None) {
        if (std::abs(dx) > m_hudCfg.SWIPE_DEADZONE_PX || std::abs(dy) > m_hudCfg.SWIPE_DEADZONE_PX) {
            m_swipeAxis = (std::abs(dx) > std::abs(dy)) ? SwipeAxis::Horizontal : SwipeAxis::Vertical;
        }
    }
    return InteractionUpdateDecision::KeepOwner;
}

void CameraView::onInteractionEnd(const InteractionEvent& event) {
    if (m_paletteSelector && m_paletteSelector->isPresented()) {
        return;
    }

    const QPoint start = event.startGlobal;
    const int dx = event.deltaFromStartGlobal.x();
    const int dy = event.deltaFromStartGlobal.y();
    if (m_swipeAxis == SwipeAxis::None &&
        (std::abs(dx) > m_hudCfg.SWIPE_DEADZONE_PX || std::abs(dy) > m_hudCfg.SWIPE_DEADZONE_PX)) {
        m_swipeAxis = (std::abs(dx) > std::abs(dy)) ? SwipeAxis::Horizontal : SwipeAxis::Vertical;
    }

    if (m_swipeAxis != SwipeAxis::Vertical || height() <= 0) {
        return;
    }

    const int screenH = height();
    const int finalY = start.y() + dy;
    const int bottomZone = qRound(screenH * m_hudCfg.BOTTOM_TRIGGER_ZONE_RATIO);
    const bool startedFromBottomEdge = start.y() >= (screenH - bottomZone);
    const bool downwardEnough = dy >= m_hudCfg.SWIPE_HIDE_THRESHOLD_PX;
    const bool upwardEnough = (-dy) >= m_hudCfg.SWIPE_SHOW_THRESHOLD_PX;
    const bool crossedBottomEdge = finalY >= (screenH - m_hudCfg.EDGE_CONFIRM_MARGIN_PX);
    const bool upwardForPalette = (-dy) >= m_hudCfg.PALETTE_SHOW_THRESHOLD_PX;

    if (m_hudVisible && startedFromBottomEdge && upwardForPalette) {
        openPaletteSelector();
        return;
    }

    if (m_hudVisible && downwardEnough && crossedBottomEdge) {
        setHudVisible(false, true);
        return;
    }

    if (!m_hudVisible && startedFromBottomEdge && upwardEnough) {
        setHudVisible(true, true);
    }
}

void CameraView::onInteractionCancel() {
    m_swipeAxis = SwipeAxis::None;
}

void CameraView::onInteractionTap(const InteractionEvent& /*event*/) {
    if (m_paletteSelector && m_paletteSelector->isPresented()) {
        closePaletteSelector(true);
    }
}

void CameraView::onInteractionDoubleTap(const InteractionEvent& /*event*/) {
    if (m_paletteSelector && m_paletteSelector->isPresented()) {
        return;
    }

    auto* camera = HardwareManager::instance().camera();
    if (!camera) return;
    camera->triggerShutter();
}

void CameraView::onInteractionLongPress(const InteractionEvent& /*event*/) {
    // Reserved for future in-view interactions.
}

/* --- Transition Anchor --- */

QWidget* CameraView::capsuleWidget() {
    return m_capsuleButton;
}

/* --- Rendering & Layout --- */

void CameraView::updateFrame(const VisualFrame& frame) {
    m_currentFrame = frame;
    m_hotMarker.update(frame.hot_spot);
    m_coldMarker.update(frame.cold_spot);
    m_centerMarker.update(frame.center_spot);

    if (m_paletteSelector && m_paletteSelector->isPresented()) {
        refreshPalettePreviews();
    }

    update();
}

void CameraView::resizeEvent(QResizeEvent* event) {
    BaseView::resizeEvent(event);
    Q_UNUSED(event);

    stopHudAnimations();
    updateHudLayout();
    applyHudState(m_hudVisible);

    if (m_paletteSelector) {
        m_paletteSelector->setGeometry(rect());
    }
}

void CameraView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const bool hasFrame = !m_currentFrame.image.isNull();

    if (hasFrame) {
        p.drawImage(rect(), m_currentFrame.image);
    } else {
        p.fillRect(rect(), Qt::black);
        QFont iconFont("tabler-icons");
        iconFont.setPixelSize(qRound(qMin(width(), height()) * 0.22));
        iconFont.setWeight(QFont::DemiBold);
        p.setFont(iconFont);
        p.setPen(QColor(170, 170, 170, 150));
        p.drawText(rect(), Qt::AlignCenter, QString(QChar(0xf83f)));
    }

    const bool shouldDrawMarkers = hasFrame && (!m_hideMarkerWhenHudHidden || m_hudVisible);
    if (shouldDrawMarkers) {
        const QSize s = size();
        m_hotMarker.paint(p, s, m_isFahrenheit);
        m_coldMarker.paint(p, s, m_isFahrenheit);
        m_centerMarker.paint(p, s, m_isFahrenheit);
    }

    if (m_shutterProgress > 0.01) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);

        const int borderWidth = qRound(m_cfg.MAX_STROKE_WIDTH * m_shutterProgress);
        const int edgeAlpha = qRound(m_cfg.MAX_STROKE_ALPHA * m_shutterProgress);
        const int fillAlpha = qRound(m_cfg.MAX_FILL_ALPHA * m_shutterProgress);

        for (int i = 0; i < borderWidth; ++i) {
            const float t = static_cast<float>(i) / borderWidth;
            const int currentAlpha = edgeAlpha - static_cast<int>((edgeAlpha - fillAlpha) * (t * t));

            p.setPen(QPen(QColor(255, 255, 255, currentAlpha), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(rect().adjusted(i, i, -i - 1, -i - 1));
        }

        const QRect centerHole = rect().adjusted(borderWidth, borderWidth,
                                                 -borderWidth, -borderWidth);
        p.fillRect(centerHole, QColor(255, 255, 255, fillAlpha));

        p.restore();
    }
}

void CameraView::setHudVisible(bool visible, bool animated) {
    if (!m_statusBar || !m_capsuleButton || !m_modeSelector) {
        m_hudVisible = visible;
        update();
        return;
    }

    m_hudVisible = visible;
    if (!animated) {
        stopHudAnimations();
        applyHudState(visible);
        update();
        return;
    }

    stopHudAnimations();
    m_statusBarAnim->setStartValue(m_statusBar->pos());
    m_statusBarAnim->setEndValue(visible ? m_statusBarVisiblePos : m_statusBarHiddenPos);

    m_capsuleAnim->setStartValue(m_capsuleButton->pos());
    m_capsuleAnim->setEndValue(visible ? m_capsuleVisiblePos : m_capsuleHiddenPos);

    m_modeSelectorAnim->setStartValue(m_modeSelector->pos());
    m_modeSelectorAnim->setEndValue(visible ? m_modeSelectorVisiblePos : m_modeSelectorHiddenPos);
    m_hudAnimGroup->start();
    update();
}

void CameraView::updateHudLayout() {
    if (!m_statusBar || !m_capsuleButton || !m_modeSelector) return;
    if (width() <= 0 || height() <= 0) return;

    const int w = width();
    const int h = height();
    const int barH = qRound(h * m_hudCfg.STATUS_BAR_H_RATIO);
    const int margin = qRound(w * m_hudCfg.CAPSULE_MARGIN_RATIO);

    const int capW = qRound(w * m_hudCfg.CAPSULE_W_RATIO);
    const int capH = qRound(h * m_hudCfg.CAPSULE_H_RATIO);
    const int capX = margin;
    const int capY = h - capH - margin;

    const int modeW = qRound(w * m_hudCfg.MODE_W_RATIO);
    const int modeH = qRound(h * m_hudCfg.MODE_H_RATIO);
    const int modeX = w - modeW - margin;
    const int modeY = h - modeH - margin;

    m_statusBar->setGeometry(0, 0, w, barH);
    m_capsuleButton->setGeometry(capX, capY, capW, capH);
    m_modeSelector->setGeometry(modeX, modeY, modeW, modeH);

    m_statusBarVisiblePos = QPoint(0, 0);
    m_statusBarHiddenPos = QPoint(0, -barH);

    m_capsuleVisiblePos = QPoint(capX, capY);
    m_modeSelectorVisiblePos = QPoint(modeX, modeY);
    const int hiddenBottomY = h + m_hudCfg.EDGE_CONFIRM_MARGIN_PX;
    m_capsuleHiddenPos = QPoint(capX, hiddenBottomY);
    m_modeSelectorHiddenPos = QPoint(modeX, hiddenBottomY);
}

void CameraView::applyHudState(bool visible) {
    if (!m_statusBar || !m_capsuleButton || !m_modeSelector) return;
    m_statusBar->move(visible ? m_statusBarVisiblePos : m_statusBarHiddenPos);
    m_capsuleButton->move(visible ? m_capsuleVisiblePos : m_capsuleHiddenPos);
    m_modeSelector->move(visible ? m_modeSelectorVisiblePos : m_modeSelectorHiddenPos);
}

void CameraView::stopHudAnimations() {
    if (m_hudAnimGroup && m_hudAnimGroup->state() == QAbstractAnimation::Running) {
        m_hudAnimGroup->stop();
    }
}
