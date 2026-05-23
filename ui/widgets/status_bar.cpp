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
    const int rightItemGap = qRound(w * kRightItemGapWidthRatio);
    const int contentYOffset = qRound(h * kContentYOffsetRatio);

    p.save();
    p.translate(0, contentYOffset);

    int leftCursorX  = horizontalInset;
    int rightCursorX = w - horizontalInset;

    /* 1. RIGHT STACK (Layout Order: Right -> Left) */

    // SLOT R1: Battery
    const int battW = qRound(h * kBatterySlotWidthRatio);
    rightCursorX -= battW;
    const QRect battRect(rightCursorX, 0, battW, h);
    drawBattery(p, battRect);

    // SLOT R2/R3: Removable Storage Icons
    QFont iconFont("tabler-icons");
    iconFont.setPixelSize(qRound(h * 0.64));
    iconFont.setWeight(QFont::DemiBold);
    const QFontMetrics iconFm(iconFont);
    const int iconGlyphW = qMax(iconFm.horizontalAdvance(QChar(ICON_SD_CARD)),
                                iconFm.horizontalAdvance(QChar(ICON_USB_DISK)));
    const int iconPaddingW = qMax(2, qRound(h * kStorageIconPaddingRatio));
    const int storageIconW = iconGlyphW + iconPaddingW;

    auto placeStorageIcon = [&](QChar icon) {
        rightCursorX -= rightItemGap;
        rightCursorX -= storageIconW;
        drawStorageIcon(p, QRect(rightCursorX, 0, storageIconW, h), icon);
    };
    if (m_sdCardReady) placeStorageIcon(QChar(ICON_SD_CARD));
    if (m_usbDiskReady) placeStorageIcon(QChar(ICON_USB_DISK));

    /* 2. LEFT STACK (Layout Order: Left -> Right) */

    // Setup font for layout metrics calculation
    QFont font("Roboto");
    font.setPixelSize(qRound(h * kTextSizeRatio));
    font.setBold(true);
    p.setFont(font);
    QFontMetrics fm(font);

    // SLOT L1: Time
    const int timeAdvanceW = fm.horizontalAdvance(m_timeText);
    const int timeInkW = fm.boundingRect(m_timeText).width();
    const int timeDrawW = qMax(timeAdvanceW, timeInkW) + 3; // Reserve outline overdraw on the right edge.
    const QRect timeRect(leftCursorX, 0, timeDrawW, h);
    drawTime(p, timeRect);
    leftCursorX += timeDrawW + leftClusterGap;

    // SLOT L2: Emissivity
    const int maxEmissivityW = qMax(0, rightCursorX - leftCursorX);
    const QRect emissivityRect(leftCursorX, 0, maxEmissivityW, h);
    drawEmissivity(p, emissivityRect);
    p.restore();
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

void StatusBar::drawStorageIcon(QPainter& p, const QRect& rect, QChar icon) {
    QFont iconFont("tabler-icons");
    iconFont.setPixelSize(qRound(height() * 0.64));
    iconFont.setWeight(QFont::DemiBold);
    p.setFont(iconFont);
    drawOutlinedText(p, rect, Qt::AlignCenter, QString(icon));
}

void StatusBar::drawBattery(QPainter& p, const QRect& rect) {
    // ---------------------------------------------------------
    // 1. Configuration: Visual Ratios (Modify these to tune UI)
    // ---------------------------------------------------------

    // --- Chassis & Positive Pole (Nipple) ---
    const qreal kBodyHeightRatio       = 0.55;  // Vertical scale of the battery body relative to the container height
    const qreal kBodyWidthRatio        = 2.05;  // Fixed aspect ratio (Width/Height) for the body to ensure consistent shape across resolutions
    const qreal kBodyCornerRadiusRatio = 0.30;  // Rounding intensity: 0.5 creates a perfect semi-circle (capsule)
    const qreal kNippleWidthRatio      = 0.08;  // Width of the positive pole relative to the total component width
    const qreal kNippleHeightRatio     = 0.40;  // Vertical height of the pole relative to the body height
    const qreal kNippleGapRatio        = 0.01;  // Horizontal gap between the body and pole to create a "floating" effect
    const qreal kNippleChordCut        = 0.30;  // Amount of the circular pole to be flattened on the left side (0.0 to 1.0)

    // --- Typography & Iconography ---
    const qreal kTextSizeRatio         = 0.80;  // Font pixel size relative to the internal body height
    const qreal kTextStretch           = 110;   // Horizontal glyph expansion (100 is original, >100 is wider/sturdier)
    const qreal kTextStrokeRatio       = 0.05;  // Artificial boldening: stroke weight added to the path outline
    const qreal kTextOpticalOffsetRatio = 0.02;
    const qreal kBoltWidthRatio        = 0.55;  // Width of the lightning icon relative to the internal body height
    const qreal kBoltHeightRatio       = 0.85;  // Height of the lightning icon relative to the internal body height
    const qreal kBoltStrokeRatio       = 0.08;  // Additional stroke weight applied to the bolt icon outline for boldening
    const qreal kCompoundSpacing       = 0.01;  // Padding between the bolt icon and the numeric percentage glyphs

    // --- Diagnostics (Failure Indicators) ---
    const qreal kErrorXSizeRatio       = 0.25;  // The diagonal span of the 'X' mark when isPresent is false
    const qreal kErrorXStrokeRatio     = 0.15;  // Line weight of the 'X' mark strokes

    // ---------------------------------------------------------
    // 2. Geometry Calculation (Automatic Alignment)
    // ---------------------------------------------------------
    const qreal h = rect.height();
    const qreal w = rect.width();

    // Calculate vertical metrics first
    const qreal bodyH = h * kBodyHeightRatio;
    const qreal bodyY = rect.y() + (h - bodyH) / 2.0;
    const qreal bodyRadius = bodyH * kBodyCornerRadiusRatio;

    // Determine horizontal metrics based on fixed ratios to prevent stretching
    const qreal bodyW   = bodyH * kBodyWidthRatio; // Locked to height
    const qreal nippleW = w * kNippleWidthRatio;
    const qreal gap     = w * kNippleGapRatio;

    // Calculate total visual span and align to the RIGHT edge
    const qreal totalVisualW = bodyW + gap + nippleW;
    const qreal groupStartX  = rect.right() - totalVisualW;

    // Define final body and nipple rectangles
    const QRectF bodyRect(groupStartX, bodyY, bodyW, bodyH);
    QPainterPath bodyPath;
    bodyPath.addRoundedRect(bodyRect, bodyRadius, bodyRadius);

    const qreal nippleH = bodyH * kNippleHeightRatio;
    const qreal nippleY = rect.y() + (h - nippleH) / 2.0;
    const QRectF nippleRect(bodyRect.right() + gap, nippleY, nippleW, nippleH);

    // ---------------------------------------------------------
    // PHASE 1: Base Layer (Surface Rendering)
    // ---------------------------------------------------------
    p.setPen(Qt::NoPen);

    // 1A: Draw Body Capsule (Always Surface Color)
    p.setBrush(BATT_SURFACE);
    p.drawPath(bodyPath);

    // 1B: Draw Chord-cut Nipple (Conditional Coloring)
    if (m_batteryStatus.isPresent && m_batteryStatus.level == 100) {
        if (m_batteryStatus.isChargerConnected) {
            p.setBrush(BATT_FILL_CHG); // Full & Plugged -> Green
        } else {
            p.setBrush(BATT_FILL_STD); // Full & Unplugged -> White
        }
    } else {
        p.setBrush(BATT_SURFACE);      // Not Full -> Dark Gray
    }

    QPainterPath nipplePath;
    nipplePath.addRoundedRect(nippleRect, nippleW * 0.8, nippleW * 0.8);

    QPainterPath clipPath;
    clipPath.addRect(nippleRect.adjusted(nippleW * kNippleChordCut, -1, 1, 1));

    p.drawPath(nipplePath.intersected(clipPath));

    // ---------------------------------------------------------
    // PHASE 2: Presence Firewall (Battery Absent)
    // ---------------------------------------------------------
    if (!m_batteryStatus.isPresent) {
        p.setPen(QPen(BATT_MARK_ERR, bodyH * kErrorXStrokeRatio, Qt::SolidLine, Qt::RoundCap));
        const qreal xHalf = bodyH * kErrorXSizeRatio;
        const QPointF c = bodyRect.center();
        p.drawLine(c.x() - xHalf, c.y() - xHalf, c.x() + xHalf, c.y() + xHalf);
        p.drawLine(c.x() + xHalf, c.y() - xHalf, c.x() - xHalf, c.y() + xHalf);
        return;
    }

    // ---------------------------------------------------------
    // PHASE 3: Dynamic Fill Layer
    // ---------------------------------------------------------
    QColor fillColor = BATT_FILL_STD;

    // Logic: Background is Green if physically connected to power.
    // Otherwise, it follows the battery level (Red if low, White if normal).
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

    // ---------------------------------------------------------
    // PHASE 4: Content Composite (Text & Bolt Push Logic)
    // ---------------------------------------------------------
    // Content color follows the background: Black on White/Red, White on Green.
    const QColor contentColor = m_batteryStatus.isChargerConnected ? BATT_TEXT_CHG : BATT_TEXT_STD;

    QFont font("Roboto");
    font.setPixelSize(bodyH * kTextSizeRatio);
    font.setWeight(QFont::Black);
    font.setStretch(kTextStretch);
    p.setFont(font);

    const QString valStr = QString::number(m_batteryStatus.level);
    const QFontMetricsF fm(font);
    const qreal textWidth = fm.horizontalAdvance(valStr);

    // Prepare text path for manual thickening
    QPainterPath textPath;
    textPath.addText(0, 0, font, valStr);
    const QRectF pathBox = textPath.boundingRect();

    // Logic: Show Bolt ONLY when actively charging (Current > 0).
    // If connected but Full, the bolt remains hidden.
    if (m_batteryStatus.isCharging) {
        const qreal boltWidth = bodyH * kBoltWidthRatio;
        const qreal spacing    = w * kCompoundSpacing;
        const qreal totalGroupWidth = textWidth + spacing + boltWidth; // Layout: [Text][Gap][Bolt]

        // Calculate the starting position for the whole group to be centered
        const qreal groupStartX = bodyRect.left() + (bodyRect.width() - totalGroupWidth) / 2.0;

        // 4A: Draw Thickened Text (Left side of the group)
        p.save();
        qreal textX = groupStartX;
        qreal textY = bodyRect.top() + (bodyH - pathBox.height()) / 2.0 - pathBox.top();
        p.translate(textX, textY);

        QPen textPen(contentColor, bodyH * kTextStrokeRatio, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(textPen);
        p.setBrush(contentColor);
        p.drawPath(textPath);
        p.restore();

        // 4B: Draw Thickened Bolt (Right side of the group)
        QPainterPath boltSourcePath;
        boltSourcePath.moveTo(0.6, 0.1); boltSourcePath.lineTo(0.2, 0.55);
        boltSourcePath.lineTo(0.5, 0.55); boltSourcePath.lineTo(0.4, 0.9);
        boltSourcePath.lineTo(0.8, 0.45); boltSourcePath.lineTo(0.5, 0.45);
        boltSourcePath.closeSubpath();

        QTransform boltTransform;
        // Offset the bolt by the text width and spacing
        qreal boltX = groupStartX + textWidth + spacing;
        boltTransform.translate(boltX, bodyRect.top() + (bodyH * (1.0 - kBoltHeightRatio) / 2.0));
        boltTransform.scale(boltWidth, bodyH * kBoltHeightRatio);
        QPainterPath renderedBoltPath = boltTransform.map(boltSourcePath);

        QPen boltPen(contentColor, bodyH * kBoltStrokeRatio, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(boltPen);
        p.setBrush(contentColor);
        p.drawPath(renderedBoltPath);

    } else {
        // Simple Absolute Geometric Center (Standard discharge or Full state)
        p.save();
        qreal textX = bodyRect.left() + (bodyRect.width() - pathBox.width()) / 2.0;
        textX -= (bodyRect.width() * kTextOpticalOffsetRatio);
        qreal textY = bodyRect.top() + (bodyH - pathBox.height()) / 2.0 - pathBox.top();
        p.translate(textX, textY);

        QPen thickPen(contentColor, bodyH * kTextStrokeRatio, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(thickPen);
        p.setBrush(contentColor);
        p.drawPath(textPath);
        p.restore();
    }
}

void StatusBar::drawOutlinedText(QPainter& p, const QRect& rect, int flags, const QString& text,
                                 const QColor& textColor) {
    // 1. Performance Guard: If HUD is nearly invisible, skip rendering entirely
    if (m_contentsOpacity < 0.02) return;

    // 2. Math Optimization: Use simple multiplication instead of std::pow
    const qreal currentAlpha = m_contentsOpacity;

    // Draw outline only if it's visually significant
    if (currentAlpha > 0.1) {
        // Square the alpha for aggressive outline fading (Alpha^3 total)
        const int outlineAlpha = qRound(255 * (currentAlpha * currentAlpha));
        p.setPen(QColor(0, 0, 0, outlineAlpha));

        // 3. Rendering Optimization: Reduced from 8-direction to 4-diagonal
        // For a 1px outline, 4 draws are visually indistinguishable from 8 on small screens.
        static const int dx[] = {-1, 1, -1, 1};
        static const int dy[] = {-1, -1, 1, 1};

        for (int i = 0; i < 4; ++i) {
            p.drawText(rect.translated(dx[i], dy[i]), flags, text);
        }
    }

    // 4. Primary text rendering
    p.setPen(textColor);
    p.drawText(rect, flags, text);
}
