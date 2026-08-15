#include "app.h"
#include "core/event_bus.h"
#include "ui/views/base_view.h"
#include "ui/views/camera_view.h"
#include "ui/views/gallery_view.h"
#include "ui/views/settings_view.h"
// #include "ui/overlays/quick_settings.h"
#include "ui/overlays/clock_modal.h"
#include "ui/overlays/modal_dialog.h"
#include "ui/overlays/toast_manager.h"
#include "ui/overlays/transition_layer.h"

#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QStackedWidget>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QDebug>
#include <cstdlib>
#include <utility>

App::App(QWidget *parent) : QWidget(parent) {
    // Embedded fullscreen setup
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setStyleSheet("background-color: black;");

    initLayer_Stack();
    initLayer_Overlays();
    connectHardwareKeys();
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
    m_settingsView = new SettingsView(this);
    m_viewStack->addWidget(m_settingsView);

    // Initialize default view
    if (m_cameraView) {
        m_viewStack->setCurrentWidget(m_cameraView);
        m_cameraView->onEnter();
    }

    auto& bus = EventBus::instance();

    connect(&bus, &EventBus::cameraRequested, this,
            [this](const QRect& anchor, TransitionMode transitionMode) {
        switchView(App::View_Camera, anchor, transitionMode);
    });

    connect(&bus, &EventBus::galleryRequested, this,
            [this](const QRect& anchor, TransitionMode transitionMode) {
        switchView(App::View_Gallery, anchor, transitionMode);
    });

    connect(&bus, &EventBus::settingsRequested, this,
            [this](const QRect& anchor, TransitionMode transitionMode) {
        switchView(App::View_Settings, anchor, transitionMode);
    });

    connect(&bus, &EventBus::settingsItemRequested, this,
            [this](SettingID item, const QRect& anchor, TransitionMode transitionMode) {
        openSettingsItem(item, anchor, transitionMode);
    });
}

void App::initLayer_Overlays() {
    // Transition Layer (Hidden by default)
    m_transitionLayer = new TransitionLayer(this);
    m_transitionLayer->hide();

    // Quick Settings (Pull-down menu)
    // m_quickSettings = new QuickSettings(this);
    // m_quickSettings->hide();

    // System Dialogs
    m_textModal = new TextModal(this);
    m_textModal->hide();

    m_clockModal = new ClockModal(this);
    m_clockModal->hide();

    // Toast Notifications
    m_toastManager = new ToastManager(this);
    m_toastManager->hide();

    m_startupMask = new QWidget(this);
    m_startupMask->setStyleSheet("background-color: black;");
    m_startupMaskOpacity = new QGraphicsOpacityEffect(m_startupMask);
    m_startupMaskOpacity->setOpacity(1.0);
    m_startupMask->setGraphicsEffect(m_startupMaskOpacity);

    m_startupMaskFade = new QPropertyAnimation(m_startupMaskOpacity, "opacity", this);
    m_startupMaskFade->setDuration(500);
    m_startupMaskFade->setStartValue(1.0);
    m_startupMaskFade->setEndValue(0.0);
    connect(m_startupMaskFade, &QPropertyAnimation::finished, this, [this]() {
        m_startupMask->hide();
    });

    m_startupMask->show();
    m_startupMask->raise();

    connect(&EventBus::instance(), &EventBus::toastRequested, this, &App::showToast);
}

void App::connectHardwareKeys() {
    auto& bus = EventBus::instance();
    connect(&bus, &EventBus::keyPressed,
            this, &App::handleHardwareKeyPressed);
    connect(&bus, &EventBus::keyShortPressed,
            this, &App::handleHardwareKeyShortPress);
    connect(&bus, &EventBus::keyLongPressed,
            this, &App::handleHardwareKeyLongPress);
}

void App::handleHardwareKeyPressed() {
    if ((m_textModal && m_textModal->isVisible()) ||
        (m_clockModal && m_clockModal->isVisible())) {
        return;
    }

    if (auto* view = activeView()) {
        view->resetTransientUi();
    }
}

void App::handleHardwareKeyShortPress() {
    if ((m_textModal && m_textModal->isVisible()) ||
        (m_clockModal && m_clockModal->isVisible())) {
        return;
    }

    if (auto* view = activeView()) {
        view->handleKeyShortPress();
    }
}

void App::handleHardwareKeyLongPress() {
    showTextModal("TURN OFF\nTHE CAMERA?", []() {
        if (std::system("poweroff") != 0) {
            qWarning() << "[System] Shutdown command failed.";
        }
        QApplication::quit();
    });
}

void App::showTextModal(const QString& title,
                        std::function<void()> onPrimaryAction,
                        ModalLevel level,
                        TextModalSize size) {
    if (m_textModal) {
        m_textModal->raise();
        ModalSpec spec;
        spec.level = level;
        spec.primaryText = "CONFIRM";
        spec.secondaryText = "CANCEL";
        spec.dismissOnMaskTap = true;
        spec.onPrimaryAction = std::move(onPrimaryAction);

        m_textModal->setMessage(title);
        m_textModal->setSize(size);
        m_textModal->present(spec);
    }
    if (m_toastManager && m_toastManager->isVisible()) {
        m_toastManager->raise();
    }
}

void App::showClockModal(std::function<bool(const QDateTime&, QString*)> onCommit) {
    if (m_clockModal) {
        m_clockModal->raise();

        ModalSpec spec;
        spec.level = ModalLevel::Normal;
        spec.primaryText = "CONFIRM";
        spec.secondaryText = "CANCEL";
        spec.dismissOnMaskTap = true;

        m_clockModal->setDateTime(QDateTime::currentDateTime());
        m_clockModal->setCommitHandler(std::move(onCommit));
        m_clockModal->present(spec);
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

void App::openSettingsItem(SettingID item,
                           const QRect& sourceAnchor,
                           TransitionMode transitionMode) {
    ++m_settingsItemRequestSerial;

    if (activeView() == m_settingsView) {
        m_settingsView->openItem(item);
        return;
    }

    m_pendingSettingsItem = item;
    m_deferPendingSettingsItem = (transitionMode == TransitionMode::Instant);
    switchView(View_Settings, sourceAnchor, transitionMode);
}

void App::activateView(ViewType type, BaseView* previousView) {
    if (previousView) previousView->onExit();
    m_viewStack->setCurrentIndex(static_cast<int>(type));
    activeView()->onEnter();

    if (type == View_Settings && m_pendingSettingsItem.has_value()) {
        const SettingID item = *m_pendingSettingsItem;
        m_pendingSettingsItem.reset();
        const bool deferOpen = m_deferPendingSettingsItem;
        m_deferPendingSettingsItem = false;
        const quint64 requestSerial = m_settingsItemRequestSerial;

        if (deferOpen) {
            QTimer::singleShot(150, this, [this, item, requestSerial]() {
                if (requestSerial == m_settingsItemRequestSerial &&
                    activeView() == m_settingsView) {
                    m_settingsView->openItem(item);
                }
            });
        } else {
            m_settingsView->openItem(item);
        }
    }
}

void App::switchView(ViewType type,
                     const QRect& sourceAnchor,
                     TransitionMode transitionMode) {
    int index = static_cast<int>(type);
    if (!m_viewStack || index < 0 || index >= m_viewStack->count()) return;

    BaseView* oldView = activeView();
    QWidget* targetWidget = m_viewStack->widget(index);

    if (oldView == targetWidget) return;

    if (m_transitionLayer && transitionMode != TransitionMode::Instant) {
        /* Define visual identity for the transition based on the destination.
           This allows the engine to be generic for Gallery, Settings, etc. */
        QString targetIcon;
        QColor targetColor = QColor(10, 10, 10); // Standard dark theme background

        if (type == View_Gallery || (type == View_Camera && oldView == m_galleryView)) {
            targetIcon = QChar(0xfa4a); // Gallery Icon
        } else if (type == View_Settings || (type == View_Camera && oldView == m_settingsView)) {
            targetIcon = QChar(0xf69e); // Settings Icon
        }

        // ========================================================
        // Case 1: Expanding (e.g., Camera -> Gallery)
        // ========================================================
        if (!sourceAnchor.isEmpty()) {
            // Expansion uses a clean slate (no snapshot) and fades into the new content.
            m_transitionLayer->startMorph(sourceAnchor, rect(), true, targetIcon,
                                          targetColor, QImage(), [=]() {
                activateView(type, oldView);

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
            activateView(type, oldView);

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
    activateView(type, oldView);
}

BaseView* App::activeView() const {
    if (!m_viewStack) return nullptr;
    return qobject_cast<BaseView*>(m_viewStack->currentWidget());
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
    if (m_textModal) m_textModal->resize(s);
    if (m_clockModal) m_clockModal->resize(s);
    if (m_toastManager) m_toastManager->resize(s);
    if (m_startupMask) m_startupMask->resize(s);
}

void App::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);

    if (m_startupMaskFadeStarted || !m_startupMask || !m_startupMaskFade) return;
    m_startupMaskFadeStarted = true;

    m_startupMask->setGeometry(rect());
    m_startupMask->raise();

    // Let the black frame reach the display before beginning the fade.
    QTimer::singleShot(0, this, [this]() {
        m_startupMaskFade->start();
    });
}
