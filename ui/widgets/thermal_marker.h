#ifndef THERMAL_MARKER_H
#define THERMAL_MARKER_H

#include "core/types.h"
#include <QPointF>
#include <QColor>
#include <QSize>

class QPainter;

/**
 * @brief Scalable thermal indicator with adaptive contrast and boundary awareness.
 *
 * Renders a reticle and a value label. The label automatically adjusts its position
 * to remain visible within the screen boundaries.
 */
class ThermalMarker {
public:
    enum Type { Center, Hot, Cold };

    explicit ThermalMarker(Type type);

    /**
     * @brief Update spot data.
     * @param pt Raw temperature point from the processor.
     */
    void update(const TempPt& pt);

    /**
     * @brief Render the marker using proportional scaling based on viewport size.
     * @param p The painter instance.
     * @param screenSize Current window/screen size for relative scaling.
     * @param isFahrenheit Unit toggle.
     */
    void paint(QPainter& p, const QSize& screenSize, bool isFahrenheit = false);

private:
    Type m_type;
    TempPt m_data;
    QColor m_primaryColor;

    /* Internal drawing procedures */
    void drawReticle(QPainter& p, const QPointF& pos, qreal sizeBase);

    /**
     * @brief Draws the temperature text box with dynamic boundary avoidance.
     */
    void drawValueLabel(QPainter& p, const QPointF& pos, qreal sizeBase,
                        bool isFahrenheit, const QSize& screenSize);
};

#endif // THERMAL_MARKER_H
