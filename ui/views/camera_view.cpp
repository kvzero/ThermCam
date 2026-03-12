#include "camera_view.h"
#include "core/global_context.h"
#include "hardware/hardware_manager.h"
#include "hardware/imaging/seekcam/seekcam.h"
#include "processing/thermal_processor.h"
#include "services/capture_service.h"
#include "ui/widgets/hud_container.h"

#include <QPainter>
#include <QResizeEvent>
#include <QEasingCurve>
#include <QDebug>

CameraView::CameraView(QWidget* parent) : BaseView(parent) {
    // Init Logic Processor (Created but not connected yet)
    m_processor = new ThermalProcessor(this);
    m_processor->setTargetSize(GlobalContext::instance().screenSize());

    // Init UI Components
    m_hudContainer = new HudContainer(this);
    m_hudContainer->raise(); // HUD sits on top of the painted thermal image

    /* Establish non-blocking animation engine for shutter feedback */
    m_shutterAnim = new QPropertyAnimation(this, "shutterProgress", this);
    m_shutterAnim->setDuration(m_cfg.ANIM_DURATION_MS);
    m_shutterAnim->setEasingCurve(QEasingCurve::OutCubic);
}

CameraView::~CameraView() {
    // HardwareManager handles camera lifecycle, we just disconnect signals
}

void CameraView::onEnter() {
    qInfo() << "[CameraView] Enter: Connecting Hardware";
    connectHardware();

    // Ensure HUD is visible when entering
    if (m_hudContainer) {
        m_hudContainer->resetPosition();
    }
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
    if (m_hudContainer && m_hudContainer->modeButton()) {
        QMetaObject::invokeMethod(m_hudContainer->modeButton(), "collapse");
    }
}

/* --- Gesture Implementations (The logic moved from EventBus) --- */

void CameraView::onGestureStarted() {

    m_swipeAxis = SwipeAxis::None;

    if (m_hudContainer) {
        m_hudContainer->stopAnimations();
    }
}

void CameraView::onGestureUpdate(const QPoint& /*start*/, int dx, int dy) {

    if (m_swipeAxis == SwipeAxis::None) {
        if (std::abs(dx) > 10 || std::abs(dy) > 10) {
            m_swipeAxis = (std::abs(dx) > std::abs(dy)) ? SwipeAxis::Horizontal : SwipeAxis::Vertical;
        }
    }

    if (m_swipeAxis == SwipeAxis::None || m_swipeAxis == SwipeAxis::Horizontal) {
        if (m_hudContainer) m_hudContainer->followGesture(dx);
    }
}

void CameraView::onGestureFinished(const QPoint& /*start*/, int dx, int /*dy*/, float vx, float /*vy*/) {
    // Forward the horizontal velocity to the HUD's physics engine for settlement
    if (m_hudContainer) {
        if (m_swipeAxis == SwipeAxis::Horizontal) {
            m_hudContainer->finalizeGesture(dx, vx);
        } else {
            m_hudContainer->finalizeGesture(0, 0);
        }
    }
}

void CameraView::onLongPressDetected(const QPoint& start) {
    // PRD: Long press on blank area -> Palette Wheel
    // Logic: If hit test didn't find a widget (handled by UIController),
    // UIController calls this.
    qInfo() << "[CameraView] Show Palette Wheel at" << start;
    // App::instance()->showPaletteWheel(start);
}
/* --- Widget Discovery --- */

QWidget* CameraView::capsuleWidget() {
    // Needed for UIController to identify the "Capsule" button
    // Assumes HudContainer exposes its internal buttons
    return m_hudContainer ? m_hudContainer->capsuleButton() : nullptr;
}

QWidget* CameraView::modeSelectorWidget() {
    return m_hudContainer ? m_hudContainer->modeButton() : nullptr;
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
    if (m_hudContainer) {
        m_hudContainer->resize(event->size());
    }
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
    m_hotMarker.paint(p, s);
    m_coldMarker.paint(p, s);
    m_centerMarker.paint(p, s);

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
