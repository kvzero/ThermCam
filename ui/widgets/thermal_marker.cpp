#include "ui/widgets/thermal_marker.h"
#include <QPainter>
#include <QPainterPath>

ThermalMarker::ThermalMarker(Type type) : m_type(type) {
    switch (m_type) {
        case Hot:    m_primaryColor = Qt::red; break;
        case Cold:   m_primaryColor = QColor(0, 255, 255); break;
        case Center: m_primaryColor = Qt::white; break;
    }
}

void ThermalMarker::update(const TempPt& pt) {
    m_data = pt;
}

void ThermalMarker::paint(QPainter& p, const QSize& screenSize, bool isFahrenheit) {
    // Skip uninitialized markers (0,0 is usually invalid for dynamic spots)
    if (m_data.x == 0 && m_data.y == 0 && m_type != Center) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    // Proportional Base: 2% of Screen Height (~9.6px on 480p)
    const qreal base = screenSize.height() * 0.02;
    const QPointF pos(m_data.x, m_data.y);

    drawReticle(p, pos, base);
    drawValueLabel(p, pos, base, isFahrenheit, screenSize);

    p.restore();
}

void ThermalMarker::drawReticle(QPainter& p, const QPointF& pos, qreal base) {
    const qreal radius = base * 0.7;
    const qreal lineLen = base * 0.8;
    const qreal gap = base * 0.25;

    // 1. Outline (Contrast)
    p.setPen(QPen(Qt::black, base * 0.5));
    p.drawEllipse(pos, radius, radius);
    p.drawLine(pos.x(), pos.y() - radius - gap, pos.x(), pos.y() - radius - gap - lineLen);
    p.drawLine(pos.x(), pos.y() + radius + gap, pos.x(), pos.y() + radius + gap + lineLen);
    p.drawLine(pos.x() - radius - gap, pos.y(), pos.x() - radius - gap - lineLen, pos.y());
    p.drawLine(pos.x() + radius + gap, pos.y(), pos.x() + radius + gap + lineLen, pos.y());

    // 2. Core (Color)
    p.setPen(QPen(m_primaryColor, base * 0.28));
    p.drawEllipse(pos, radius, radius);
    p.drawLine(pos.x(), pos.y() - radius - gap, pos.x(), pos.y() - radius - gap - lineLen);
    p.drawLine(pos.x(), pos.y() + radius + gap, pos.x(), pos.y() + radius + gap + lineLen);
    p.drawLine(pos.x() - radius - gap, pos.y(), pos.x() - radius - gap - lineLen, pos.y());
    p.drawLine(pos.x() + radius + gap, pos.y(), pos.x() + radius + gap + lineLen, pos.y());
}

void ThermalMarker::drawValueLabel(QPainter& p, const QPointF& pos, qreal base,
                                   bool isFahrenheit, const QSize& screenSize) {
    float val = isFahrenheit ? (m_data.temperature * 1.8f + 32.0f) : m_data.temperature;
    QString text = QString::asprintf("%.1f%s", val, isFahrenheit ? "℉" : "℃");

    // 1. Font setup (Use Painter's font which is set in main.cpp)
    QFont font = p.font();
    int pixelSize = qRound(base * 1.9);
    if (pixelSize < 12) pixelSize = 12; // Minimum readability fallback
    font.setPixelSize(pixelSize);
    font.setBold(true);
    p.setFont(font);

    // 2. Metric calculation
    const QFontMetricsF fm(font);
    qreal visualHeight = fm.ascent();
    qreal visualWidth = fm.horizontalAdvance(text);

    // 3. Box Geometry
    const qreal vPadding = base * 0.6;
    const qreal hPadding = base * 0.6;
    const qreal offset = base * 2.5;

    qreal boxWidth  = visualWidth + hPadding * 2;
    qreal boxHeight = visualHeight + vPadding * 2;

    // Default: Right-Center
    QRectF boxRect(pos.x() + offset,
                   pos.y() - (boxHeight / 2.0),
                   boxWidth,
                   boxHeight);

    // 4. Boundary Avoidance (Bounce back logic)
    if (boxRect.right() > screenSize.width())   boxRect.moveRight(pos.x() - offset);
    if (boxRect.left() < 0)                      boxRect.moveLeft(pos.x() + offset);
    if (boxRect.bottom() > screenSize.height()) boxRect.moveBottom(pos.y() - offset);
    if (boxRect.top() < 0)                         boxRect.moveTop(pos.y() + offset);

    // 5. Draw Background
    QPainterPath path;
    path.addRoundedRect(boxRect, 4, 4);

    p.setOpacity(0.55); // Semi-transparent black
    p.fillPath(path, Qt::black);

    // 6. Draw Text
    p.setOpacity(1.0);
    p.setPen(Qt::white);
    p.drawText(boxRect, Qt::AlignCenter, text);
}
