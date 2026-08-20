#include "poweroff_overlay.h"

#include "core/event_bus.h"
#include "hardware/hmi/system_control.h"

#include <QApplication>
#include <QDebug>
#include <QLinearGradient>
#include <QPainter>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QTimer>
#include <QVariantAnimation>

#include <cmath>
#include <cstdlib>

namespace {
constexpr int kSpinnerSegments = 12;
constexpr qreal kTwoPi = 6.28318530717958647692;
}

PoweroffOverlay::PoweroffOverlay(SystemControl* systemControl, QWidget* parent)
    : QWidget(parent),
      m_spinnerAnimation(new QPropertyAnimation(this, "spinnerPhase", this)),
      m_backlightFadeTimer(new QTimer(this)),
      m_backlightFade(new QVariantAnimation(this)),
      m_systemControl(systemControl) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    hide();

    m_spinnerAnimation->setDuration(kSpinnerCycleMs);
    m_spinnerAnimation->setStartValue(0.0);
    m_spinnerAnimation->setEndValue(1.0);
    m_spinnerAnimation->setLoopCount(-1);
    m_spinnerAnimation->setEasingCurve(QEasingCurve::Linear);

    m_backlightFadeTimer->setSingleShot(true);
    m_backlightFadeTimer->setInterval(kBacklightFadeStartMs);
    connect(m_backlightFadeTimer, &QTimer::timeout, this, [this]() {
        int currentBrightness = 0;
        QString brightnessError;
        if (m_systemControl &&
            m_systemControl->screenBrightnessPercent(&currentBrightness, &brightnessError)) {
            m_originalBrightnessPercent = currentBrightness;
        } else if (m_systemControl) {
            qWarning() << "[Poweroff] Cannot read current backlight:" << brightnessError;
        }

        m_backlightFade->stop();
        m_backlightFade->setStartValue(m_originalBrightnessPercent);
        m_backlightFade->setEndValue(0);
        m_backlightFade->start();
    });

    m_backlightFade->setDuration(kBacklightFadeDurationMs);
    m_backlightFade->setEasingCurve(QEasingCurve::Linear);
    connect(m_backlightFade, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        if (!m_systemControl) return;

        QString error;
        if (!m_systemControl->setScreenBrightnessPercent(value.toInt(), &error)) {
            qWarning() << "[Poweroff] Backlight fade failed:" << error;
        }
    });
    connect(m_backlightFade, &QVariantAnimation::finished, this, [this]() {
        if (std::system("poweroff") == 0) {
            QApplication::quit();
            return;
        }

        qWarning() << "[Poweroff] Shutdown command failed.";
        if (m_systemControl) {
            QString error;
            if (!m_systemControl->setScreenBrightnessPercent(m_originalBrightnessPercent, &error)) {
                qWarning() << "[Poweroff] Failed to restore backlight:" << error;
            }
        }
        m_spinnerAnimation->stop();
        hide();
        emit EventBus::instance().toastRequested("SHUTDOWN FAILED", ToastLevel::Error);
    });
}

void PoweroffOverlay::start(Reason reason) {
    if (isVisible()) return;

    setReason(reason);
    m_spinnerPhase = 0.0;
    m_spinnerAnimation->start();
    m_backlightFadeTimer->start();

    raise();
    show();
    update();
}

void PoweroffOverlay::setSpinnerPhase(qreal phase) {
    m_spinnerPhase = phase;
    update();
}

void PoweroffOverlay::setReason(Reason reason) {
    switch (reason) {
    case Reason::BatteryDepleted:
        m_message = QStringLiteral("Battery depleted. Shutting down...");
        return;
    case Reason::UserRequested:
    default:
        m_message = QStringLiteral("Shutting down...");
        return;
    }
}

void PoweroffOverlay::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QLinearGradient background(0, 0, 0, height());
    background.setColorAt(0.0, QColor("#121820"));
    background.setColorAt(0.52, QColor("#080B10"));
    background.setColorAt(1.0, QColor("#020304"));
    painter.fillRect(rect(), background);

    const qreal shortestSide = qMin(width(), height());
    const QPointF spinnerCenter(width() / 2.0, height() * 0.44);
    const qreal spinnerRadius = shortestSide * 0.098;
    const qreal dotRadius = qMax<qreal>(2.0, shortestSide * 0.010);

    QRadialGradient halo(spinnerCenter, spinnerRadius * 1.8);
    halo.setColorAt(0.0, QColor(104, 192, 255, 28));
    halo.setColorAt(0.55, QColor(51, 120, 185, 12));
    halo.setColorAt(1.0, Qt::transparent);
    painter.fillRect(rect(), halo);

    for (int index = 0; index < kSpinnerSegments; ++index) {
        const qreal progress = static_cast<qreal>(index) / kSpinnerSegments;
        const qreal angle = -kTwoPi / 4.0 + kTwoPi * (m_spinnerPhase - progress);
        const qreal strength = 1.0 - progress;
        const QColor dotColor(156,
                              216,
                              255,
                              qRound(35.0 + 220.0 * strength * strength));
        const QPointF dotCenter(spinnerCenter.x() + std::cos(angle) * spinnerRadius,
                                spinnerCenter.y() + std::sin(angle) * spinnerRadius);
        painter.setPen(Qt::NoPen);
        painter.setBrush(dotColor);
        painter.drawEllipse(dotCenter, dotRadius, dotRadius);
    }

    const QRect textRect(qRound(width() * 0.10),
                         qRound(height() * 0.60),
                         qRound(width() * 0.80),
                         qRound(height() * 0.10));
    QFont messageFont("Roboto");
    messageFont.setPixelSize(qRound(shortestSide * 0.047));
    messageFont.setWeight(QFont::DemiBold);
    painter.setFont(messageFont);
    painter.setPen(QColor(242, 247, 252, 230));
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_message);
}
