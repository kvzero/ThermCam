#include "camera_view.h"
#include "core/event_bus.h"
#include "core/global_context.h"
#include "core/settings_store.h"
#include "hardware/hardware_manager.h"
#include "hardware/imaging/seekcam/seekcam.h"
#include "processing/thermal_processor.h"
#include "services/capture_service.h"
#include "ui/widgets/status_bar.h"
#include "ui/widgets/capsule_button.h"
#include "ui/widgets/mode_selector.h"

#include <QPainter>
#include <QResizeEvent>
#include <QEasingCurve>
#include <QDebug>
#include <QMetaObject>
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
}

CameraView::CameraView(QWidget* parent) : BaseView(parent) {
    // Init Logic Processor (Created but not connected yet)
    m_processor = new ThermalProcessor(this);
    m_processor->setTargetSize(GlobalContext::instance().screenSize());

    // Init UI Components (CameraView owns HUD directly)
    m_statusBar = new StatusBar(this);
    m_capsuleButton = new CapsuleButton(this);
    m_modeSelector = new ModeSelector(this);

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

    /* Establish non-blocking animation engine for shutter feedback */
    m_shutterAnim = new QPropertyAnimation(this, "shutterProgress", this);
    m_shutterAnim->setDuration(m_cfg.ANIM_DURATION_MS);
    m_shutterAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_isFahrenheit = isFahrenheitFromSnapshot(SettingsStore::instance().current());
    connect(&EventBus::instance(), &EventBus::temperatureUnitChanged, this,
            [this](bool isFahrenheit) {
                if (m_isFahrenheit == isFahrenheit) return;
                m_isFahrenheit = isFahrenheit;
                update();
            });

    updateHudLayout();
    applyHudState(true);
}

CameraView::~CameraView() {
    // HardwareManager handles camera lifecycle, we just disconnect signals
}

void CameraView::onEnter() {
    qInfo() << "[CameraView] Enter: Connecting Hardware";
    connectHardware();

    // Ensure HUD is visible when entering
    setHudVisible(true, false);
}

void CameraView::onExit() {
    qInfo() << "[CameraView] Exit: Disconnecting Hardware";
    disconnectHardware();
}

void CameraView::connectHardware() {
    auto* camera = HardwareManager::instance().camera();
    if (!camera) return;

    // Link: Camera -> Processor
    connect(camera, &ThermalCamera::rawFrameReady,
            m_processor, &ThermalProcessor::processFrame, Qt::UniqueConnection);

    // Link: Processor -> View
    connect(m_processor, &ThermalProcessor::frameReady,
            this, &CameraView::updateFrame, Qt::UniqueConnection);

    // Link: Processor -> CaptureService
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

    /* visual feedback is strictly reserved for single-shot captures. */
    if (CaptureService::instance().currentMode() == CaptureMode::Photo) {
        m_shutterAnim->stop();
        m_shutterAnim->setStartValue(1.0);
        m_shutterAnim->setEndValue(0.0);
        m_shutterAnim->start();
    }

    // Delegate the actual acquisition logic to the headless service
    CaptureService::instance().handlePhysicalTrigger();
}

void CameraView::resetTransientUi() {
    if (m_modeSelector) {
        QMetaObject::invokeMethod(m_modeSelector, "collapse");
    }
}

/* --- Gesture Implementations (The logic moved from EventBus) --- */

void CameraView::onGestureStarted() {
    m_swipeAxis = SwipeAxis::None;
    stopHudAnimations();
}

void CameraView::onGestureUpdate(const QPoint& /*start*/, int dx, int dy) {
    if (m_swipeAxis == SwipeAxis::None) {
        if (std::abs(dx) > m_hudCfg.SWIPE_DEADZONE_PX || std::abs(dy) > m_hudCfg.SWIPE_DEADZONE_PX) {
            m_swipeAxis = (std::abs(dx) > std::abs(dy)) ? SwipeAxis::Horizontal : SwipeAxis::Vertical;
        }
    }
}

void CameraView::onGestureFinished(const QPoint& start, int dx, int dy, float /*vx*/, float /*vy*/) {
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

    if (m_hudVisible && downwardEnough && crossedBottomEdge) {
        setHudVisible(false, true);
        return;
    }

    if (!m_hudVisible && startedFromBottomEdge && upwardEnough) {
        setHudVisible(true, true);
    }
}

void CameraView::onLongPressDetected(const QPoint& start) {
    // PRD: Long press on blank area -> Palette Wheel
    // Logic: If hit test didn't find a widget (handled by InteractionArbiter),
    // InteractionArbiter calls this.
    qInfo() << "[CameraView] Show Palette Wheel at" << start;
    // App::instance()->showPaletteWheel(start);
}
/* --- Widget Discovery --- */

QWidget* CameraView::capsuleWidget() {
    return m_capsuleButton;
}

QWidget* CameraView::modeSelectorWidget() {
    return m_modeSelector;
}

/* --- Rendering & Layout --- */

void CameraView::updateFrame(const VisualFrame& frame) {
    m_currentFrame = frame;
    m_hotMarker.update(frame.hot_spot);
    m_coldMarker.update(frame.cold_spot);
    m_centerMarker.update(frame.center_spot);
    update(); // Schedules paintEvent
}

void CameraView::resizeEvent(QResizeEvent* event) {
    BaseView::resizeEvent(event);
    Q_UNUSED(event);
    stopHudAnimations();
    updateHudLayout();
    applyHudState(m_hudVisible);
}

void CameraView::paintEvent(QPaintEvent*) {
    QPainter p(this);

    // Layer 0: Thermal Image
    if (!m_currentFrame.image.isNull()) {
        p.drawImage(rect(), m_currentFrame.image);
    } else {
        p.fillRect(rect(), Qt::black);
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, "Waiting for Stream...");
    }

    // Layer 1: Markers (Fixed, do not move with HUD)
    const QSize s = size();
    m_hotMarker.paint(p, s, m_isFahrenheit);
    m_coldMarker.paint(p, s, m_isFahrenheit);
    m_centerMarker.paint(p, s, m_isFahrenheit);

    // Layer 2: Transient Optical Feedback (Zero-Widget Composition)
    if (m_shutterProgress > 0.01) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);

        const int borderWidth = qRound(m_cfg.MAX_STROKE_WIDTH * m_shutterProgress);
        const int edgeAlpha = qRound(m_cfg.MAX_STROKE_ALPHA * m_shutterProgress);
        const int fillAlpha = qRound(m_cfg.MAX_FILL_ALPHA * m_shutterProgress);

        for (int i = 0; i < borderWidth; ++i) {

            float t = (float)i / borderWidth;
            int currentAlpha = edgeAlpha - static_cast<int>((edgeAlpha - fillAlpha) * (t * t));

            p.setPen(QPen(QColor(255, 255, 255, currentAlpha), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(rect().adjusted(i, i, -i - 1, -i - 1));
        }

        QRect centerHole = rect().adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth);
        p.fillRect(centerHole, QColor(255, 255, 255, fillAlpha));

        p.restore();
    }
}

void CameraView::setHudVisible(bool visible, bool animated) {
    if (!m_statusBar || !m_capsuleButton || !m_modeSelector) {
        m_hudVisible = visible;
        return;
    }

    m_hudVisible = visible;
    if (!animated) {
        stopHudAnimations();
        applyHudState(visible);
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
