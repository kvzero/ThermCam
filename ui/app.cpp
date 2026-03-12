#include "app.h"
#include "core/event_bus.h"
#include "ui/views/base_view.h"
#include "ui/views/camera_view.h"
#include "ui/views/gallery_view.h"
// #include "ui/views/settings_view.h"
// #include "ui/overlays/quick_settings.h"
#include "ui/overlays/confirm_dialog.h"
#include "ui/overlays/toast_manager.h"
#include "ui/overlays/transition_layer.h"

#include <QStackedWidget>
#include <QResizeEvent>
#include <QDebug>

App::App(QWidget *parent) : QWidget(parent) {
    // Embedded fullscreen setup
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setStyleSheet("background-color: black;");

    initLayer_Stack();
    initLayer_Overlays();
}

App::~App() {
    // Child widgets are automatically deleted by Qt's object tree
}

void App::initLayer_Stack() {
    m_viewStack = new QStackedWidget(this);

    // 1. Camera View (Default)
    m_cameraView = new CameraView(this);
    m_viewStack->addWidget(m_cameraView);

    // 2. Gallery View
    m_galleryView = new GalleryView(this);
    m_viewStack->addWidget(m_galleryView);

    // 3. Settings View
    // m_settingsView = new SettingsView(this);
    // m_viewStack->addWidget(m_settingsView);

    // Initialize default view
    if (m_cameraView) {
        m_viewStack->setCurrentWidget(m_cameraView);
        m_cameraView->onEnter();
    }

    auto& bus = EventBus::instance();

    connect(&bus, &EventBus::cameraRequested, this, [this](const QRect& anchor) {
        switchView(App::View_Camera, anchor);
    });

    connect(&bus, &EventBus::galleryRequested, this, [this](const QRect& anchor) {
        switchView(App::View_Gallery, anchor);
    });

//    connect(&bus, &EventBus::settingsRequested, this, [this](const QRect& anchor) {
//        switchView(App::View_Settings, anchor);
//    });
}

void App::initLayer_Overlays() {
    // Transition Layer (Hidden by default)
    m_transitionLayer = new TransitionLayer(this);
    m_transitionLayer->hide();

    // Quick Settings (Pull-down menu)
    // m_quickSettings = new QuickSettings(this);
    // m_quickSettings->hide();

    // System Dialogs
    m_confirmDialog = new ConfirmDialog(this);
    m_confirmDialog->hide();

    // Toast Notifications
    m_toastManager = new ToastManager(this);
    m_toastManager->hide();

    connect(&EventBus::instance(), &EventBus::toastRequested, this, &App::showToast);
}

void App::showConfirmDialog(const QString& title, std::function<void()> onConfirm) {
    if (m_confirmDialog) {
        m_confirmDialog->raise();
        m_confirmDialog->showMessage(title, onConfirm);
    }
    if (m_toastManager && m_toastManager->isVisible()) {
        m_toastManager->raise();
    }
}

void App::showToast(const QString& msg, ToastLevel level){
    if (m_toastManager) {
        m_toastManager->raise();
        m_toastManager->showToast(msg, level);
    }
}

void App::switchView(ViewType type, const QRect& sourceAnchor) {
    int index = static_cast<int>(type);
    if (!m_viewStack || index < 0 || index >= m_viewStack->count()) return;

    BaseView* oldView = activeView();
    QWidget* targetWidget = m_viewStack->widget(index);

    if (oldView == targetWidget) return;

    if (m_transitionLayer) {
        /* Define visual identity for the transition based on the destination.
           This allows the engine to be generic for Gallery, Settings, etc. */
        QString targetIcon;
        QColor targetColor = QColor(10, 10, 10); // Standard dark theme background

        if (type == View_Gallery || (type == View_Camera && oldView == m_galleryView)) {
            targetIcon = QChar(0xfa4a); // Gallery Icon
        } /*else if (type == View_Settings || (type == View_Camera && oldView == m_settingsView)) {
            targetIcon = QChar(0xf69e); // Settings Icon
        }*/

        // ========================================================
        // Case 1: Expanding (e.g., Camera -> Gallery)
        // ========================================================
        if (!sourceAnchor.isEmpty()) {
            // Expansion uses a clean slate (no snapshot) and fades into the new content.
            m_transitionLayer->startMorph(sourceAnchor, rect(), true, targetIcon,
                                          targetColor, QImage(), [=]() {
                if (oldView) oldView->onExit();
                m_viewStack->setCurrentIndex(index);
                if (activeView()) activeView()->onEnter();

                m_transitionLayer->startFadeOut([](){});
            });
            return;
        }

        // ========================================================
        // Case 2: Contracting (e.g., Gallery -> Camera)
        // ========================================================
        else {
            QRect targetAnchor = sourceAnchor;

            // Retroactively locate the physical return destination (Capsule Button)
            if (targetAnchor.isEmpty() && m_cameraView && m_cameraView->capsuleWidget()) {
                QWidget* capsule = m_cameraView->capsuleWidget();
                QPoint globalPos = capsule->mapToGlobal(QPoint(0, 0));

                // Determine which half of the capsule to target based on previous context
                int yOffset = (oldView == m_galleryView) ? (capsule->height() / 2) : 0;
                targetAnchor = QRect(globalPos.x(), globalPos.y() + yOffset,
                                     capsule->width(), capsule->height() / 2);
            }

            // 1. Capture a high-performance snapshot of the current view before it is hidden
            QImage exitSnapshot;
            if (oldView) {
                exitSnapshot = oldView->grab().toImage();
            }

            // 2. Prepare the curtain to shield the background swap
            m_transitionLayer->setGeometry(rect());
            m_transitionLayer->setLayerOpacity(1.0);
            m_transitionLayer->show();
            m_transitionLayer->raise();

            // 3. Perform the heavy context switch behind the curtain
            if (oldView) oldView->onExit();
            m_viewStack->setCurrentIndex(index);
            if (activeView()) activeView()->onEnter();

            // 4. Animate the curtain shrinking with the content snapshot
            m_transitionLayer->startMorph(rect(), targetAnchor, false, targetIcon,
                                          targetColor, exitSnapshot, [=]() {
                m_transitionLayer->hide();
            });
            return;
        }
    }

    // ========================================================
    // Fallback: Instant Switch (No animation resources available)
    // ========================================================
    if (oldView) oldView->onExit();
    m_viewStack->setCurrentIndex(index);
    if (BaseView* newView = activeView()) newView->onEnter();
}

BaseView* App::activeView() const {
    if (!m_viewStack) return nullptr;
    return qobject_cast<BaseView*>(m_viewStack->currentWidget());
}

QWidget* App::findWidgetAt(const QPoint& globalPos) {
    QPoint localPos = this->mapFromGlobal(globalPos);
    // Recursively finds the top-most child at the position.
    // Respects Z-order: Overlays -> Stack -> Views -> Widgets.
    return this->childAt(localPos);
}

/*
QuickSettings* App::quickSettings() const {
    return m_quickSettings;
}
*/

void App::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const QSize s = event->size();

    // Layer 0: Stack fills screen
    if (m_viewStack) {
        m_viewStack->setGeometry(0, 0, s.width(), s.height());
    }

    // Layer 1: Transition Layer
    if (m_transitionLayer) m_transitionLayer->resize(s);

    // Layer 2: System Overlays
    // if (m_quickSettings) m_quickSettings->resize(s.width(), m_quickSettings->height()); // Height managed internally
    if (m_confirmDialog) m_confirmDialog->resize(s);
    if (m_toastManager) m_toastManager->resize(s.width(), qRound(s.height() * 0.2));
}
