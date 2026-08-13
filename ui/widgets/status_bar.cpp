#include "status_bar.h"
#include "hardware/hardware_manager.h"
#include "hardware/sensor/battery_monitor.h"
#include "hardware/imaging/thermal_camera.h"
#include "hardware/storage/storage_manager.h"
#include "core/event_bus.h"

#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <QTimer>
#include <QFontMetrics>
#include <QDir>
#include <QFile>
#include <QSocketNotifier>

#include <fcntl.h>
#include <unistd.h>

StatusBar::StatusBar(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);

    // 1. Setup Event Subscriptions (Push Mode)
    connect(&EventBus::instance(), &EventBus::powerStatusChanged,
            this, &StatusBar::onPowerStatusChanged);

    connect(&EventBus::instance(), &EventBus::emissivityChanged,
            this, &StatusBar::onEmissivityChanged);

    if (auto* storage = HardwareManager::instance().storage()) {
        connect(storage, &StorageManager::sdCardStateChanged,
                this, &StatusBar::onSdCardStateChanged);
        connect(storage, &StorageManager::usbDiskStateChanged,
                this, &StatusBar::onUsbDiskStateChanged);
        m_sdCardReady = storage->isSdCardReady();
        m_usbDiskReady = storage->isUsbDiskReady();
    }

    // 2. Initial State Synchronization (Pull Mode on Startup)
    if (auto* bm = HardwareManager::instance().battery()) {
        m_batteryStatus = bm->getBatteryInfo().status;
    }

    if (auto* cam = HardwareManager::instance().camera()) {
        m_emissivity = cam->getEmissivity();
    }

    // 3. Periodic Clock Heartbeat (1Hz)
    QTimer* clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, &StatusBar::onSecondTick);
    clockTimer->start(1000);

    // Immediate time populate
    onSecondTick();

    initUdcMonitoring();
}

StatusBar::~StatusBar() {
    for (QSocketNotifier* notifier : m_udcStateNotifiers) {
        const int fd = notifier->socket();
        delete notifier;
        if (fd >= 0) {
            ::close(fd);
        }
    }
}

void StatusBar::onPowerStatusChanged(const BatteryStatus& status) {
    m_batteryStatus = status;
    update(); // Redraw due to power event
}

void StatusBar::onEmissivityChanged(float value) {
    m_emissivity = value;
    update(); // Redraw due to emissivity change
}

void StatusBar::onSecondTick() {
    const QString newTimeText = QDateTime::currentDateTime().toString("HH:mm");

    // Only trigger a repaint if the minute has actually changed
    if (newTimeText != m_timeText) {
        m_timeText = newTimeText;
        update();
    }
}

void StatusBar::onSdCardStateChanged(bool ready) {
    if (m_sdCardReady == ready) return;
    m_sdCardReady = ready;
    update();
}

void StatusBar::onUsbDiskStateChanged(bool ready) {
    if (m_usbDiskReady == ready) return;
    m_usbDiskReady = ready;
    update();
}

void StatusBar::initUdcMonitoring() {
    const QDir udcDir("/sys/class/udc");
    const QFileInfoList udcs = udcDir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo& udc : udcs) {
        const QString statePath = udc.filePath() + "/state";
        const int fd = ::open(statePath.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }

        auto* notifier = new QSocketNotifier(fd, QSocketNotifier::Exception, this);
        connect(notifier, &QSocketNotifier::activated, this,
                [this](auto...) { refreshPcConnection(); });
        m_udcStateFds.append(fd);
        m_udcStateNotifiers.append(notifier);
    }

    refreshPcConnection();
}

void StatusBar::refreshPcConnection() {
    // A UDC reaches "configured" only after a USB host has enumerated this device.
    // Type-C/extcon attachment alone also occurs with chargers and power banks.
    bool connected = false;
    for (const int fd : m_udcStateFds) {
        if (::lseek(fd, 0, SEEK_SET) < 0) {
            continue;
        }

        char buffer[64];
        const ssize_t bytesRead = ::read(fd, buffer, sizeof(buffer));
        if (bytesRead > 0 && QByteArray(buffer, bytesRead).trimmed() == "configured") {
            connected = true;
            break;
        }
    }

    if (m_pcConnected == connected) return;
    m_pcConnected = connected;
    update();
}

void StatusBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Apply global content opacity for slide-to-fade effect
    p.setOpacity(m_contentsOpacity);

    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0) return;

    const int horizontalInset = qRound(w * kHorizontalInsetWidthRatio);
    const int leftClusterGap = qRound(w * kLeftClusterGapWidthRatio);
    const int contentYOffset = qRound(h * kContentYOffsetRatio);
    const QRect barRect(0, 0, w, h);

    p.save();
    p.translate(0, contentYOffset);

    int leftCursorX  = horizontalInset;
    const qreal rightStatusLeftX = drawRightStatusItems(p, barRect, w - horizontalInset - 1.0);

    // Emissivity text stops at the right cluster's painted bounds.
    QFont font("Roboto");
    font.setPixelSize(qRound(h * kTextSizeRatio));
    font.setBold(true);
    p.setFont(font);
    QFontMetrics fm(font);

    const int timeAdvanceW = fm.horizontalAdvance(m_timeText);
    const int timeInkW = fm.boundingRect(m_timeText).width();
    const int timeDrawW = qMax(timeAdvanceW, timeInkW) + 3; // Reserve outline overdraw on the right edge.
    const QRect timeRect(leftCursorX, 0, timeDrawW, h);
    drawTime(p, timeRect);
    leftCursorX += timeDrawW + leftClusterGap;

    const int maxEmissivityW = qMax(0, qRound(rightStatusLeftX) - leftCursorX);
    const QRect emissivityRect(leftCursorX, 0, maxEmissivityW, h);
    drawEmissivity(p, emissivityRect);
    p.restore();
}

namespace {
QFont statusIconFont(qreal barHeight) {
    constexpr qreal kIconSizeRatio = 0.64;

    QFont iconFont("tabler-icons");
    iconFont.setPixelSize(qRound(barHeight * kIconSizeRatio));
    iconFont.setWeight(QFont::DemiBold);
    return iconFont;
}

// Battery layout and drawing share this geometry.
struct BatterySize {
    qreal bodyH;
    qreal bodyW;
    qreal nippleW;
    qreal gap;
    qreal visualW;
};

BatterySize batterySize(qreal barHeight) {
    constexpr qreal kBodyHeightRatio = 0.55;
    constexpr qreal kBodyWidthRatio = 2.05;
    constexpr qreal kNippleWidthHeightRatio = 0.1112;
    constexpr qreal kNippleGapHeightRatio = 0.0139;

    BatterySize size;
    size.bodyH = barHeight * kBodyHeightRatio;
    size.bodyW = size.bodyH * kBodyWidthRatio;
    size.nippleW = barHeight * kNippleWidthHeightRatio;
    size.gap = barHeight * kNippleGapHeightRatio;
    size.visualW = size.bodyW + size.gap + size.nippleW;
    return size;
}
} // namespace

qreal StatusBar::drawRightStatusItems(QPainter& p, const QRect& barRect, qreal rightEdge) {
    // Cursor advances by painted width, not by preallocated slots.
    const qreal visualGap = qRound(barRect.width() * kRightVisualGapWidthRatio);
    const QFontMetricsF iconFm(statusIconFont(barRect.height()));
    qreal cursorX = rightEdge;

    drawBattery(p, cursorX, barRect);
    cursorX -= batterySize(barRect.height()).visualW;

    auto placeStatusIcon = [&](bool visible, QChar icon) {
        if (!visible) return;
        cursorX -= visualGap;
        drawStatusIcon(p, icon, cursorX, barRect);
        cursorX -= qMax<qreal>(1.0, iconFm.boundingRect(QString(icon)).width());
    };

    placeStatusIcon(m_sdCardReady, QChar(ICON_SD_CARD));
    placeStatusIcon(m_usbDiskReady, QChar(ICON_USB_DISK));
    placeStatusIcon(m_pcConnected, QChar(ICON_PC_CONNECTION));

    return cursorX;
}

void StatusBar::drawTime(QPainter& p, const QRect& rect) {
    const int fontSize = qRound(height() * kTextSizeRatio);
    QFont font("Roboto");
    font.setPixelSize(fontSize);
    font.setBold(true);
    p.setFont(font);

    drawOutlinedText(p, rect, Qt::AlignLeft | Qt::AlignVCenter, m_timeText);
}

void StatusBar::drawEmissivity(QPainter& p, const QRect& rect) {
    const int fontSize = qRound(height() * kTextSizeRatio);
    QFont font("Roboto");
    font.setPixelSize(fontSize);
    font.setBold(true);
    p.setFont(font);

    const QString text = QString("ε: %1").arg(m_emissivity, 0, 'f', 2);
    drawOutlinedText(p, rect, Qt::AlignLeft | Qt::AlignVCenter, text, EMISSIVITY_TEXT_COLOR);
}

void StatusBar::drawStatusIcon(QPainter& p, QChar icon, qreal visualRightX, const QRect& barRect) {
    const QFont iconFont = statusIconFont(barRect.height());
    p.setFont(iconFont);

    const QString text(icon);
    const QFontMetricsF fm(iconFont);
    const QRectF inkRect = fm.boundingRect(text);

    // Align by glyph ink bounds; Tabler icon advance includes side bearings.
    const qreal baselineX = visualRightX - inkRect.right();
    const qreal baselineY = barRect.center().y() - (inkRect.top() + inkRect.bottom()) / 2.0;
    const QPointF baseline(baselineX, baselineY);

    if (m_contentsOpacity < 0.02) return;

    const qreal currentAlpha = m_contentsOpacity;
    if (currentAlpha > 0.1) {
        const int outlineAlpha = qRound(255 * (currentAlpha * currentAlpha));
        p.setPen(QColor(0, 0, 0, outlineAlpha));

        static const int dx[] = {-1, 1, -1, 1};
        static const int dy[] = {-1, -1, 1, 1};
        for (int i = 0; i < 4; ++i) {
            p.drawText(baseline + QPointF(dx[i], dy[i]), text);
        }
    }

    p.setPen(Qt::white);
    p.drawText(baseline, text);
}

void StatusBar::drawBattery(QPainter& p, qreal visualRightX, const QRect& barRect) {
    constexpr qreal kBodyCornerRadiusRatio = 0.30;
    constexpr qreal kNippleHeightBodyRatio = 0.40;
    constexpr qreal kNippleChordCut = 0.30;
    constexpr qreal kTextSizeBodyRatio = 0.80;
    constexpr qreal kTextStretch = 110;
    constexpr qreal kTextStrokeBodyRatio = 0.05;
    constexpr qreal kTextOpticalOffsetRatio = 0.02;
    constexpr qreal kBoltWidthBodyRatio = 0.55;
    constexpr qreal kBoltHeightBodyRatio = 0.85;
    constexpr qreal kBoltStrokeBodyRatio = 0.08;
    constexpr qreal kErrorXSizeBodyRatio = 0.25;
    constexpr qreal kErrorXStrokeBodyRatio = 0.15;

    const qreal barHeight = barRect.height();
    const BatterySize size = batterySize(barHeight);
    const qreal bodyH = size.bodyH;
    const qreal bodyW = size.bodyW;
    const qreal nippleW = size.nippleW;
    const qreal gap = size.gap;
    const qreal bodyY = barRect.y() + (barHeight - bodyH) / 2.0;
    const qreal bodyRadius = bodyH * kBodyCornerRadiusRatio;
    const qreal groupStartX = visualRightX - size.visualW;

    const QRectF bodyRect(groupStartX, bodyY, bodyW, bodyH);
    QPainterPath bodyPath;
    bodyPath.addRoundedRect(bodyRect, bodyRadius, bodyRadius);

    const qreal nippleH = bodyH * kNippleHeightBodyRatio;
    const qreal nippleY = barRect.y() + (barHeight - nippleH) / 2.0;
    const QRectF nippleRect(bodyRect.right() + gap, nippleY, nippleW, nippleH);

    // Shell and terminal cap.
    p.setPen(Qt::NoPen);

    p.setBrush(BATT_SURFACE);
    p.drawPath(bodyPath);

    if (m_batteryStatus.isPresent && m_batteryStatus.level == 100) {
        if (m_batteryStatus.isChargerConnected) {
            p.setBrush(BATT_FILL_CHG);
        } else {
            p.setBrush(BATT_FILL_STD);
        }
    } else {
        p.setBrush(BATT_SURFACE);
    }

    QPainterPath nipplePath;
    nipplePath.addRoundedRect(nippleRect, nippleW * 0.8, nippleW * 0.8);

    QPainterPath clipPath;
    clipPath.addRect(nippleRect.adjusted(nippleW * kNippleChordCut, -1, 1, 1));

    p.drawPath(nipplePath.intersected(clipPath));

    // Battery-absent marker replaces level content.
    if (!m_batteryStatus.isPresent) {
        p.setPen(QPen(BATT_MARK_ERR, bodyH * kErrorXStrokeBodyRatio, Qt::SolidLine, Qt::RoundCap));
        const qreal xHalf = bodyH * kErrorXSizeBodyRatio;
        const QPointF c = bodyRect.center();
        p.drawLine(c.x() - xHalf, c.y() - xHalf, c.x() + xHalf, c.y() + xHalf);
        p.drawLine(c.x() + xHalf, c.y() - xHalf, c.x() - xHalf, c.y() + xHalf);
        return;
    }

    // Level fill: external power takes precedence over low-battery coloring.
    QColor fillColor = BATT_FILL_STD;

    if (m_batteryStatus.isChargerConnected) {
        fillColor = BATT_FILL_CHG;
    } else if (m_batteryStatus.level <= LOW_BATTERY_THRESHOLD) {
        fillColor = BATT_FILL_LOW;
    }

    qreal fillWidth = bodyW * (m_batteryStatus.level / 100.0);
    if (m_batteryStatus.level > 0) {
        const qreal kMinVisibleFillPx = qMax<qreal>(1.0, bodyH * 0.12);
        fillWidth = qMax(fillWidth, kMinVisibleFillPx);
    }
    fillWidth = qMin(fillWidth, bodyW);

    if (fillWidth > 0) {
        p.setBrush(fillColor);
        QPainterPath fillClipPath;
        fillClipPath.addRect(QRectF(bodyRect.x(), bodyRect.y(), fillWidth, bodyH));
        p.drawPath(bodyPath.intersected(fillClipPath));
    }

    const QColor contentColor = m_batteryStatus.isChargerConnected ? BATT_TEXT_CHG : BATT_TEXT_STD;

    // Centered battery content, optionally with a charging bolt.
    QFont font("Roboto");
    font.setPixelSize(bodyH * kTextSizeBodyRatio);
    font.setWeight(QFont::Black);
    font.setStretch(kTextStretch);
    p.setFont(font);

    const QString valStr = QString::number(m_batteryStatus.level);
    const QFontMetricsF fm(font);
    const qreal textWidth = fm.horizontalAdvance(valStr);

    // Roboto's built-in weights are too thin at HUD size, so the percentage is stroked as a path.
    QPainterPath textPath;
    textPath.addText(0, 0, font, valStr);
    const QRectF pathBox = textPath.boundingRect();

    if (m_batteryStatus.isCharging) {
        const qreal boltWidth = bodyH * kBoltWidthBodyRatio;
        const qreal spacing = gap;
        const qreal totalGroupWidth = textWidth + spacing + boltWidth;

        const qreal groupStartX = bodyRect.left() + (bodyRect.width() - totalGroupWidth) / 2.0;

        p.save();
        qreal textX = groupStartX;
        qreal textY = bodyRect.top() + (bodyH - pathBox.height()) / 2.0 - pathBox.top();
        p.translate(textX, textY);

        QPen textPen(contentColor, bodyH * kTextStrokeBodyRatio, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(textPen);
        p.setBrush(contentColor);
        p.drawPath(textPath);
        p.restore();

        QPainterPath boltSourcePath;
        boltSourcePath.moveTo(0.6, 0.1); boltSourcePath.lineTo(0.2, 0.55);
        boltSourcePath.lineTo(0.5, 0.55); boltSourcePath.lineTo(0.4, 0.9);
        boltSourcePath.lineTo(0.8, 0.45); boltSourcePath.lineTo(0.5, 0.45);
        boltSourcePath.closeSubpath();

        QTransform boltTransform;
        qreal boltX = groupStartX + textWidth + spacing;
        boltTransform.translate(boltX, bodyRect.top() + (bodyH * (1.0 - kBoltHeightBodyRatio) / 2.0));
        boltTransform.scale(boltWidth, bodyH * kBoltHeightBodyRatio);
        QPainterPath renderedBoltPath = boltTransform.map(boltSourcePath);

        QPen boltPen(contentColor, bodyH * kBoltStrokeBodyRatio, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(boltPen);
        p.setBrush(contentColor);
        p.drawPath(renderedBoltPath);

    } else {
        p.save();
        qreal textX = bodyRect.left() + (bodyRect.width() - pathBox.width()) / 2.0;
        textX -= (bodyRect.width() * kTextOpticalOffsetRatio);
        qreal textY = bodyRect.top() + (bodyH - pathBox.height()) / 2.0 - pathBox.top();
        p.translate(textX, textY);

        QPen thickPen(contentColor, bodyH * kTextStrokeBodyRatio, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(thickPen);
        p.setBrush(contentColor);
        p.drawPath(textPath);
        p.restore();
    }
}

void StatusBar::drawOutlinedText(QPainter& p, const QRect& rect, int flags, const QString& text,
                                 const QColor& textColor) {
    if (m_contentsOpacity < 0.02) return;

    const qreal currentAlpha = m_contentsOpacity;

    if (currentAlpha > 0.1) {
        const int outlineAlpha = qRound(255 * (currentAlpha * currentAlpha));
        p.setPen(QColor(0, 0, 0, outlineAlpha));

        // Four diagonal samples are enough for the one-pixel HUD outline.
        static const int dx[] = {-1, 1, -1, 1};
        static const int dy[] = {-1, -1, 1, 1};

        for (int i = 0; i < 4; ++i) {
            p.drawText(rect.translated(dx[i], dy[i]), flags, text);
        }
    }

    p.setPen(textColor);
    p.drawText(rect, flags, text);
}
