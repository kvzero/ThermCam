#include "settings_row.h"
#include <QMouseEvent>
#include <QPainter>
#include <QLinearGradient>
#include <QFont>

namespace {
const QString kExpandChevronIcon = QString(QChar(0xea61));
}

SettingsBaseRow::SettingsBaseRow(QWidget* parent) : QWidget(parent) {
    setProperty("isInteractable", true);
    setProperty("allowSlideTrigger", false);
    setAttribute(Qt::WA_TranslucentBackground, true);
}

void SettingsBaseRow::mousePressEvent(QMouseEvent* event) {
    m_isPressed = true;
    m_clickCanceled = false;
    m_pressPos = event->pos();
    m_lastPos = event->pos();
    onPressStateChanged();
}

void SettingsBaseRow::mouseReleaseEvent(QMouseEvent* /*event*/) {}

bool SettingsBaseRow::handleInteractionUpdate(QPoint localPos) {
    if (!m_isPressed) return false;

    m_lastPos = localPos;
    const int dx = localPos.x() - m_pressPos.x();
    const int dy = localPos.y() - m_pressPos.y();
    const int threshold = qMax(6, qRound(height() * 0.12));

    // Vertical drag means list scrolling intent, row should relinquish ownership.
    if (std::abs(dy) > threshold && std::abs(dy) > std::abs(dx)) {
        m_isPressed = false;
        m_clickCanceled = true;
        onPressStateChanged();
        return false;
    }

    // Horizontal drag means navigation gesture intent, row should relinquish ownership.
    if (std::abs(dx) > threshold && std::abs(dx) > std::abs(dy)) {
        m_isPressed = false;
        m_clickCanceled = true;
        onPressStateChanged();
        return false;
    }

    if (!rect().contains(localPos)) {
        m_isPressed = false;
        m_clickCanceled = true;
        onPressStateChanged();
        return false;
    }

    return true;
}

void SettingsBaseRow::finalizeGesture(int /*dy*/) {
    const bool shouldActivate = m_isPressed && !m_clickCanceled && rect().contains(m_lastPos);
    m_isPressed = false;
    m_clickCanceled = false;
    onPressStateChanged();

    if (shouldActivate) emit activated();
}

void SettingsBaseRow::cancelGesture() {
    if (!m_isPressed && !m_clickCanceled) return;
    m_isPressed = false;
    m_clickCanceled = false;
    onPressStateChanged();
}

void SettingsBaseRow::onPressStateChanged() {
    update();
}

SettingsPrimaryRow::SettingsPrimaryRow(QWidget* parent) : SettingsBaseRow(parent) {}

void SettingsPrimaryRow::setData(const PrimaryItemData& data) {
    m_data = data;
    update();
}

void SettingsPrimaryRow::setSplitProgress(qreal p) {
    p = qBound(0.0, p, 1.0);
    if (qFuzzyCompare(m_splitProgress, p)) return;
    m_splitProgress = p;
    update();
}

void SettingsPrimaryRow::setSelected(bool selected) {
    if (m_isSelected == selected) return;
    m_isSelected = selected;
    update();
}

void SettingsPrimaryRow::setTargetLayoutWidth(int width) {
    width = qMax(1, width);
    if (m_targetLayoutWidth == width) return;
    m_targetLayoutWidth = width;
    update();
}

void SettingsPrimaryRow::setBottomDividerVisible(bool visible) {
    if (m_showBottomDivider == visible) return;
    m_showBottomDivider = visible;
    update();
}

void SettingsPrimaryRow::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (isPressed()) {
        p.fillRect(rect().adjusted(0, 0, 0, -2), QColor(255, 255, 255, 24));
    }

    const int h = height();
    const int w = width();
    const qreal split = m_splitProgress;
    const qreal marginRatio = 0.06;
    const int marginX = qRound(w * marginRatio);
    const int iconSize = qRound(h * 0.58);
    const int expandedW = (m_targetLayoutWidth > 0) ? m_targetLayoutWidth : w;
    const qreal finalCenteredX = (expandedW - iconSize) * 0.5;
    const qreal baseOffset = finalCenteredX - (expandedW * marginRatio);
    const int iconX = qRound(marginX + (baseOffset * split));
    const int iconY = (h - iconSize) / 2;
    const QRect iconRect(iconX, iconY, iconSize, iconSize);

    if (m_isSelected && m_splitProgress > 0.01) {
        const QRect fillRect(0, 0, w, h);
        if (fillRect.height() > 0) {
            p.save();
            p.setClipRect(fillRect);
            p.setPen(Qt::NoPen);
            QColor selectedFill = m_data.iconBgColor;
            selectedFill.setAlpha(100);
            p.fillRect(fillRect, selectedFill);
            p.restore();
        }
    }

    QLinearGradient iconBg(iconRect.topLeft(), iconRect.bottomRight());
    iconBg.setColorAt(0.0, m_data.iconBgColor.lighter(110));
    iconBg.setColorAt(1.0, m_data.iconBgColor.darker(105));
    p.setPen(Qt::NoPen);
    p.setBrush(iconBg);
    p.drawRoundedRect(iconRect, iconSize * 0.22, iconSize * 0.22);

    {
        QFont iconFont("tabler-icons");
        iconFont.setPixelSize(qRound(iconSize * 0.60));
        p.setFont(iconFont);
        p.setPen(QColor(0, 0, 0, 180));
        p.drawText(iconRect.adjusted(1, 1, 1, 1), Qt::AlignCenter, m_data.icon);
        p.setPen(Qt::white);
        p.drawText(iconRect, Qt::AlignCenter, m_data.icon);
    }

    const int textLeft = iconRect.right() + qRound(w * 0.05);
    const int arrowRight = w - marginX;
    const int textRight = arrowRight - qRound(w * 0.08);
    const int shift = qRound(w * 0.15 * split);
    const qreal textAlpha = 1.0 - split;

    p.save();
    p.setOpacity(textAlpha);
    p.translate(-shift, 0);

    QFont titleFont("Roboto");
    titleFont.setPixelSize(qRound(h * 0.30));
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(Qt::white);
    p.drawText(QRect(textLeft, 0, qMax(1, textRight - textLeft), h),
               Qt::AlignVCenter | Qt::AlignLeft, m_data.title);

    QFont arrowFont("tabler-icons");
    arrowFont.setPixelSize(qRound(h * 0.34));
    p.setFont(arrowFont);
    p.drawText(QRect(arrowRight - qRound(w * 0.04), 0, qRound(w * 0.04), h),
               Qt::AlignVCenter | Qt::AlignRight, kExpandChevronIcon);
    p.restore();

    if (m_showBottomDivider) {
        const int longL = textLeft;
        const int longR = arrowRight;
        const int shortL = iconRect.left();
        const int shortR = iconRect.right();
        const int lineY = h - 1;
        const int lineL = qRound(longL * (1.0 - split) + shortL * split);
        const int lineR = qRound(longR * (1.0 - split) + shortR * split);
        QColor lineColor(255, 255, 255, 56);
        p.setPen(QPen(lineColor, 2));
        p.drawLine(lineL, lineY, lineR, lineY);
    }
}

SettingsSecondaryRow::SettingsSecondaryRow(QWidget* parent) : SettingsBaseRow(parent) {}

void SettingsSecondaryRow::setData(const SecondaryItemData& data) {
    m_data = data;
    m_valueText.clear();
    m_toggleOn = false;
    update();
}

void SettingsSecondaryRow::setBottomDividerVisible(bool visible) {
    if (m_showBottomDivider == visible) return;
    m_showBottomDivider = visible;
    update();
}

void SettingsSecondaryRow::setToggleOn(bool on) {
    if (m_toggleOn == on) return;
    m_toggleOn = on;
    update();
}

void SettingsSecondaryRow::setValueText(const QString& valueText) {
    if (m_valueText == valueText) return;
    m_valueText = valueText;
    update();
}

void SettingsSecondaryRow::toggleVisualState() {
    if (m_data.type != ActionType::Toggle) return;
    m_toggleOn = !m_toggleOn;
    update();
}

QRect SettingsSecondaryRow::toggleTrackRect() const {
    const int h = height();
    const int w = width();
    const int margin = qRound(w * 0.08);
    const int trackW = qRound(h * 0.95);
    const int trackH = qRound(h * 0.50);
    const int right = w - margin;
    const int x = right - trackW;
    const int y = (h - trackH) / 2;
    return QRect(x, y, trackW, trackH);
}

QRect SettingsSecondaryRow::toggleHitRect() const {
    const int pad = qMax(6, qRound(height() * 0.14));
    return toggleTrackRect().adjusted(-pad, -pad, pad, pad);
}

void SettingsSecondaryRow::mousePressEvent(QMouseEvent* event) {
    if (m_data.type == ActionType::Toggle && !toggleHitRect().contains(event->pos())) {
        return;
    }
    SettingsBaseRow::mousePressEvent(event);
}

void SettingsSecondaryRow::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (isPressed()) {
        p.fillRect(rect().adjusted(0, 0, 0, -2), QColor(255, 255, 255, 24));
    }

    const int h = height();
    const int w = width();
    const int margin = qRound(w * 0.08);

    QFont textFont("Roboto");
    textFont.setPixelSize(qRound(h * 0.30));
    textFont.setBold(true);
    p.setFont(textFont);
    p.setPen(Qt::white);
    p.drawText(QRect(margin, 0, w - margin * 2, h), Qt::AlignVCenter | Qt::AlignLeft, m_data.title);

    QString tail = m_valueText;
    bool useIconFont = false;
    bool drawToggle = false;
    if (m_data.type == ActionType::Toggle) {
        drawToggle = true;
    } else if (tail.isEmpty()) {
        switch (m_data.type) {
        case ActionType::Action:
            tail = kExpandChevronIcon;
            useIconFont = true;
            break;
        case ActionType::Value:
            tail = "--";
            break;
        case ActionType::Toggle:
            break;
        }
    }

    if (drawToggle) {
        const QRect toggleRect = toggleTrackRect();
        const int trackW = toggleRect.width();
        const int trackH = toggleRect.height();
        const int x = toggleRect.x();
        const int y = toggleRect.y();
        const QRectF trackRect(toggleRect);

        QColor trackColor = m_toggleOn ? QColor(72, 196, 104, 235) : QColor(138, 138, 138, 185);
        p.setPen(Qt::NoPen);
        p.setBrush(trackColor);
        p.drawRoundedRect(trackRect, trackH / 2.0, trackH / 2.0);

        const qreal knobH = qMax(6.0, trackH - 4.0);
        const qreal knobW = knobH * 1.25;
        const qreal knobY = y + (trackH - knobH) * 0.5;
        const qreal knobX = m_toggleOn ? (x + trackW - knobW - 2.0) : (x + 2.0);
        p.setBrush(Qt::white);
        p.drawRoundedRect(QRectF(knobX, knobY, knobW, knobH), knobH / 2.0, knobH / 2.0);
    } else {
        p.setPen(QColor(220, 220, 220));
        if (useIconFont) {
            QFont iconFont("tabler-icons");
            iconFont.setPixelSize(qRound(h * 0.34));
            p.setFont(iconFont);
        }
        p.drawText(QRect(margin, 0, w - margin * 2, h), Qt::AlignVCenter | Qt::AlignRight, tail);
    }

    if (m_showBottomDivider) {
        p.setPen(QPen(QColor(255, 255, 255, 56), 2));
        p.drawLine(margin, h - 1, w - margin, h - 1);
    }
}
