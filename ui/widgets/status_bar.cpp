#include "status_bar.h"
#include "hardware/hardware_manager.h"
#include "hardware/sensor/battery_monitor.h"
#include "hardware/imaging/thermal_camera.h"
#include "hardware/storage/storage_manager.h"
#include "core/event_bus.h"

#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QDateTime>
#include <QTimer>
#include <QFontMetrics>
#include <QtMath>
#include <QDir>
#include <QFile>
#include <QSocketNotifier>

#include <fcntl.h>
#include <unistd.h>

namespace {
// Layout ratios are relative to the status bar.
constexpr qreal kHorizontalInsetWidthRatio = 0.03;
constexpr qreal kLeftClusterGapWidthRatio = 0.045;
constexpr qreal kRightVisualGapWidthRatio = 0.031;
constexpr qreal kContentYOffsetRatio = 0.08;

// Typography and shared outline.
constexpr qreal kHudOutlinePx = 1.0;
constexpr qreal kLeftTextSizeRatio = 0.64;
constexpr qreal kBatteryTextSizeRatio = 0.6;
constexpr qreal kIconSizeRatio = 0.68;

constexpr ushort kSdCardIcon = 0xf384;
constexpr ushort kUsbDiskIcon = 0xfc59;
constexpr ushort kPcConnectionIcon = 0xf00c;

// Battery proportions, relative to bar height or battery body as named.
constexpr qreal kBodyHeightRatio = 0.55;
constexpr qreal kBodyWidthRatio = 2.05;

// State-dependent colors.
constexpr int kLowBatteryThreshold = 20;
const QColor kBatteryChargingColor("#34C759");
const QColor kBatteryNormalColor("#FFFFFF");
const QColor kBatteryLowColor("#FF3B30");
const QColor kBatteryErrorColor("#FF3B30");
const QColor kEmissivityColor("#FFC84A");

QPainterPath statusTextPath(const QString& text, qreal barHeight, qreal textSizeRatio) {
    QFont font("Roboto");
    font.setPixelSize(qRound(barHeight * textSizeRatio));
    font.setBold(true);
    QPainterPath path;
    path.addText(0, 0, font, text);
    return path;
}
void drawOutlinedPath(QPainter& p, const QPainterPath& path, const QColor& color,
                      const QPainterPath& exclusion = QPainterPath()) {
    if (p.opacity() < 0.02 || path.isEmpty()) return;

    p.save();
    if (p.opacity() > 0.1) {
        QPainterPathStroker stroker;
        stroker.setWidth(2.0 * kHudOutlinePx);
        stroker.setJoinStyle(Qt::RoundJoin);
        const QPainterPath outline = stroker.createStroke(path).subtracted(exclusion);
        const int outlineAlpha = qRound(255 * p.opacity() * p.opacity());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, outlineAlpha));
        p.drawPath(outline);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(path);
    p.restore();
}

// Per-paint geometry; no battery state is retained here.
struct BatteryGeometry {
    QRectF bodyRect;
    QRectF fillRect;
    QPainterPath border;
    QPainterPath fillArea;
    QPainterPath terminal;
    QPainterPath bolt;
    QPainterPath boltClearance;
};

BatteryGeometry batteryGeometry(qreal visualRightX, const QRect& barRect, bool charging) {
    const qreal barHeight = barRect.height();
    const qreal bodyH = barHeight * kBodyHeightRatio;
    const qreal bodyW = bodyH * kBodyWidthRatio;
    constexpr qreal kNippleWidthHeightRatio = 0.1112;
    constexpr qreal kNippleGapHeightRatio = 0.0139;
    const qreal nippleW = barHeight * kNippleWidthHeightRatio;
    const qreal gap = barHeight * kNippleGapHeightRatio;
    const qreal visualW = bodyW + gap + nippleW;
    constexpr qreal kBodyCornerRadiusRatio = 0.24;
    constexpr qreal kBorderWidthBodyRatio = 0.075;
    constexpr qreal kFillGapBodyRatio = 0.065;
    const qreal bodyRadius = bodyH * kBodyCornerRadiusRatio;
    const qreal borderWidth = bodyH * kBorderWidthBodyRatio;
    const qreal fillInset = borderWidth + bodyH * kFillGapBodyRatio;
    const QRectF bodyRect(visualRightX - kHudOutlinePx - visualW,
                          barRect.y() + (barHeight - bodyH) / 2.0,
                          bodyW, bodyH);

    QPainterPath bodyPath;
    bodyPath.addRoundedRect(bodyRect, bodyRadius, bodyRadius);
    QPainterPath shellInnerPath;
    shellInnerPath.addRoundedRect(bodyRect.adjusted(borderWidth, borderWidth,
                                                    -borderWidth, -borderWidth),
                                  bodyRadius - borderWidth, bodyRadius - borderWidth);
    QPainterPath borderPath = bodyPath.subtracted(shellInnerPath);
    const QRectF fillRect = bodyRect.adjusted(fillInset, fillInset, -fillInset, -fillInset);
    QPainterPath fillAreaPath;
    const qreal fillRadius = qMax<qreal>(0.0, bodyRadius - fillInset);
    fillAreaPath.addRoundedRect(fillRect, fillRadius, fillRadius);

    QPainterPath boltPath;
    QPainterPath boltClearance;
    if (charging) {
        QPainterPath source;
        source.moveTo(0.74, 0.0);
        source.lineTo(0.0, 0.58);
        source.lineTo(0.43, 0.58);
        source.lineTo(0.26, 1.0);
        source.lineTo(1.0, 0.42);
        source.lineTo(0.57, 0.42);
        source.closeSubpath();

        constexpr qreal kBoltWidthBodyRatio = 0.68;
        constexpr qreal kBoltHeightBodyRatio = 1.14;
        const qreal boltW = bodyH * kBoltWidthBodyRatio;
        const qreal boltH = bodyH * kBoltHeightBodyRatio;
        QTransform transform;
        transform.translate(bodyRect.center().x() - boltW / 2.0,
                            bodyRect.center().y() - boltH / 2.0);
        transform.scale(boltW, boltH);
        boltPath = transform.map(source);

        constexpr qreal kBoltClearanceBodyRatio = 0.065;
        QPainterPathStroker stroker;
        stroker.setWidth(2.0 * (kHudOutlinePx + bodyH * kBoltClearanceBodyRatio));
        stroker.setJoinStyle(Qt::RoundJoin);
        boltClearance = boltPath.united(stroker.createStroke(boltPath));
        // Leave the underlying camera visible through the bolt's clearance.
        borderPath = borderPath.subtracted(boltClearance);
        fillAreaPath = fillAreaPath.subtracted(boltClearance);
    }

    constexpr qreal kNippleHeightBodyRatio = 0.40;
    const qreal nippleH = bodyH * kNippleHeightBodyRatio;
    const QRectF nippleRect(bodyRect.right() + gap,
                            bodyRect.center().y() - nippleH / 2.0,
                            nippleW, nippleH);
    QPainterPath nipplePath;
    nipplePath.addRoundedRect(nippleRect, nippleW * 0.8, nippleW * 0.8);
    constexpr qreal kNippleChordCut = 0.30;
    QPainterPath nippleClip;
    nippleClip.addRect(nippleRect.adjusted(nippleW * kNippleChordCut, -1, 1, 1));
    return {bodyRect, fillRect, borderPath, fillAreaPath,
            nipplePath.intersected(nippleClip), boltPath, boltClearance};
}

} // namespace

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
    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(m_contentsOpacity);
    p.translate(0, qRound(h * kContentYOffsetRatio));

    const QRect barRect(0, 0, w, h);
    const int horizontalInset = qRound(w * kHorizontalInsetWidthRatio);
    const int leftGap = qRound(w * kLeftClusterGapWidthRatio);
    const qreal rightGap = qRound(w * kRightVisualGapWidthRatio);

    qreal rightCursor = drawBattery(p, w - horizontalInset - 1.0, barRect);
    const struct {
        bool visible;
        ushort glyph;
    } icons[] = {
        {m_sdCardReady, kSdCardIcon},
        {m_usbDiskReady, kUsbDiskIcon},
        {m_pcConnected, kPcConnectionIcon}
    };
    for (const auto& icon : icons) {
        if (icon.visible) {
            rightCursor = drawStatusIcon(p, QChar(icon.glyph), rightCursor - rightGap, barRect);
        }
    }

    int leftCursor = horizontalInset;
    const QString texts[] = {m_timeText, QString("ε: %1").arg(m_emissivity, 0, 'f', 2)};
    const QColor colors[] = {Qt::white, kEmissivityColor};
    for (int i = 0; i < 2; ++i) {
        QPainterPath path = statusTextPath(texts[i], h, kLeftTextSizeRatio);
        const QRectF bounds = path.boundingRect();
        const int textWidth = qCeil(bounds.width() + 2.0 * kHudOutlinePx);
        const int availableWidth = i == 0 ? textWidth : qMax(0, qRound(rightCursor) - leftCursor);
        path.translate(leftCursor + kHudOutlinePx - bounds.left(),
                       h / 2.0 - bounds.center().y());
        // Preserve the right cluster when the emissivity text runs out of space.
        p.save();
        p.setClipRect(QRect(leftCursor, 0, availableWidth, h), Qt::IntersectClip);
        drawOutlinedPath(p, path, colors[i]);
        p.restore();
        leftCursor += textWidth + leftGap;
    }
}

qreal StatusBar::drawStatusIcon(QPainter& p, QChar icon, qreal visualRightX, const QRect& barRect) {
    QFont iconFont("tabler-icons");
    iconFont.setPixelSize(qRound(barRect.height() * kIconSizeRatio));
    iconFont.setWeight(QFont::DemiBold);
    p.save();
    p.setFont(iconFont);

    const QString text(icon);
    const QFontMetricsF fm(iconFont);
    const QRectF inkRect = fm.boundingRect(text);

    // Align by glyph ink bounds; Tabler icon advance includes side bearings.
    const qreal baselineX = visualRightX - kHudOutlinePx - inkRect.right();
    const qreal baselineY = barRect.center().y() - (inkRect.top() + inkRect.bottom()) / 2.0;
    const QPointF baseline(baselineX, baselineY);

    const qreal leftEdge = visualRightX - qMax<qreal>(1.0, inkRect.width()) - 2.0 * kHudOutlinePx;
    if (m_contentsOpacity < 0.02) {
        p.restore();
        return leftEdge;
    }

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
    p.restore();
    return leftEdge;
}

qreal StatusBar::drawBattery(QPainter& p, qreal visualRightX, const QRect& barRect) {
    const BatteryGeometry geometry = batteryGeometry(
        visualRightX, barRect, m_batteryStatus.isPresent && m_batteryStatus.isCharging);

    p.save();
    p.setPen(Qt::NoPen);
    drawOutlinedPath(p, geometry.border, Qt::white, geometry.boltClearance);
    drawOutlinedPath(p, geometry.terminal, Qt::white);

    if (!m_batteryStatus.isPresent) {
        constexpr qreal kErrorXSizeBodyRatio = 0.25;
        constexpr qreal kErrorXStrokeBodyRatio = 0.15;
        p.setPen(QPen(kBatteryErrorColor, geometry.bodyRect.height() * kErrorXStrokeBodyRatio,
                      Qt::SolidLine, Qt::RoundCap));
        const qreal xHalf = geometry.bodyRect.height() * kErrorXSizeBodyRatio;
        const QPointF c = geometry.bodyRect.center();
        p.drawLine(QPointF(c.x() - xHalf, c.y() - xHalf),
                   QPointF(c.x() + xHalf, c.y() + xHalf));
        p.drawLine(QPointF(c.x() + xHalf, c.y() - xHalf),
                   QPointF(c.x() - xHalf, c.y() + xHalf));
        p.restore();
        return geometry.bodyRect.left() - kHudOutlinePx;
    }

    // External power takes precedence over low-battery coloring.
    QColor fillColor = kBatteryNormalColor;
    if (m_batteryStatus.isChargerConnected) {
        fillColor = kBatteryChargingColor;
    } else if (m_batteryStatus.level <= kLowBatteryThreshold) {
        fillColor = kBatteryLowColor;
    }

    qreal fillWidth = geometry.fillRect.width() * (m_batteryStatus.level / 100.0);
    if (m_batteryStatus.level > 0) {
        constexpr qreal kMinFillBodyRatio = 0.12;
        const qreal minVisibleFill = qMax<qreal>(1.0, geometry.bodyRect.height() * kMinFillBodyRatio);
        fillWidth = qMax(fillWidth, minVisibleFill);
    }
    fillWidth = qMin(fillWidth, geometry.fillRect.width());
    if (fillWidth > 0) {
        QPainterPath fillClip;
        fillClip.addRect(QRectF(geometry.fillRect.x(), geometry.fillRect.y(), fillWidth, geometry.fillRect.height()));
        p.setBrush(fillColor);
        p.drawPath(geometry.fillArea.intersected(fillClip));
    }

    drawOutlinedPath(p, geometry.bolt, Qt::white);

    QPainterPath textPath = statusTextPath(
        QString::number(m_batteryStatus.level) + QLatin1Char('%'), barRect.height(), kBatteryTextSizeRatio);
    const QRectF textBounds = textPath.boundingRect();
    const qreal visualGap = qRound(barRect.width() * kRightVisualGapWidthRatio);
    const qreal textRight = geometry.bodyRect.left() - kHudOutlinePx - visualGap;
    textPath.translate(textRight - kHudOutlinePx - textBounds.right(),
                       geometry.bodyRect.center().y() - textBounds.center().y());
    drawOutlinedPath(p, textPath, Qt::white);
    p.restore();
    return textPath.boundingRect().left() - kHudOutlinePx;
}
