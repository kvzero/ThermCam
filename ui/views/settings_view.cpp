#include "settings_view.h"
#include "ui/widgets/settings_row.h"
#include "ui/widgets/scroll_indicator.h"
#include "core/event_bus.h"
#include "ui/app.h"

#include <QMouseEvent>
#include <QPainter>
#include <QEasingCurve>
#include <QDebug>
#include <cmath>

namespace {
const QVector<PrimaryItemData> kMenuBlueprint = {
    {
        0, QString(QChar(0xecf6)), QColor(72, 104, 255), "Thermal",
        {
            {SettingID::Palette, "Palette", ActionType::Action},
            {SettingID::Emissivity, "Emissivity", ActionType::Value},
            {SettingID::TemperatureUnit, "Temperature Unit", ActionType::Toggle},
            {SettingID::ShutterCalibration, "Shutter Calibration", ActionType::Action}
        }
    },
    {
        1, QString(QChar(0xf676)), QColor(28, 158, 112), "Capture",
        {
            {SettingID::OSDOverlay, "Save OSD Overlay", ActionType::Toggle},
            {SettingID::StorageFormat, "Storage Format", ActionType::Action}
        }
    },
    {
        2, QString(QChar(0xf596)), QColor(182, 102, 45), "System",
        {
            {SettingID::DeviceInfo, "Device Information", ActionType::Action},
            {SettingID::FactoryReset, "Factory Reset", ActionType::Action}
        }
    }
};

const QString kTopBarBackIcon = QString(QChar(0xea60));
const QString kTopBarCloseIcon = QString(QChar(0xeb55));
} // namespace

// ============================================================
// SettingsTopBar
// ============================================================

SettingsTopBar::SettingsTopBar(QWidget* parent) : QWidget(parent) {
    setProperty("isInteractable", true);
    setProperty("allowSlideTrigger", false);
}

void SettingsTopBar::setTitle(const QString& title) {
    if (m_title == title) return;
    m_title = title;
    update();
}

void SettingsTopBar::setMaskOpacity(qreal v) {
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(m_maskOpacity, v)) return;
    m_maskOpacity = v;
    update();
}

SettingsTopBar::PressZone SettingsTopBar::zoneAt(const QPoint& pos) const {
    const int expand = qRound(height() * 0.38);
    const QRect backTouch = m_backRect.adjusted(-expand, -expand, expand, expand);
    const QRect closeTouch = m_closeRect.adjusted(-expand, -expand, expand, expand);

    if (backTouch.contains(pos)) return PressZone::Back;
    if (closeTouch.contains(pos)) return PressZone::Close;
    return PressZone::None;
}

void SettingsTopBar::mousePressEvent(QMouseEvent* event) {
    m_pressedZone = zoneAt(event->pos());
    m_lastPos = event->pos();
    update();
}

bool SettingsTopBar::handleInteractionUpdate(QPoint localPos) {
    if (m_pressedZone == PressZone::None) return false;
    m_lastPos = localPos;
    update();
    return true;
}

void SettingsTopBar::finalizeGesture(int /*dy*/) {
    if (m_pressedZone == PressZone::Back && zoneAt(m_lastPos) == PressZone::Back) {
        emit backTriggered();
    } else if (m_pressedZone == PressZone::Close && zoneAt(m_lastPos) == PressZone::Close) {
        emit closeTriggered();
    }
    m_pressedZone = PressZone::None;
    update();
}

void SettingsTopBar::cancelGesture() {
    if (m_pressedZone == PressZone::None) return;
    m_pressedZone = PressZone::None;
    update();
}

void SettingsTopBar::resizeEvent(QResizeEvent* /*event*/) {
    const int h = height();
    const int w = width();
    const int btnSize = qRound(h * 0.65);
    const int margin = qRound(h * 0.15);

    m_backRect = QRect(margin, (h - btnSize) / 2, btnSize, btnSize);
    m_closeRect = QRect(w - margin - btnSize, (h - btnSize) / 2, btnSize, btnSize);
}

void SettingsTopBar::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_maskOpacity > 0.01) {
        const QRect gradRect = rect();
        QLinearGradient grad(gradRect.topLeft(), gradRect.bottomLeft());
        grad.setColorAt(0.0, QColor(0, 0, 0, qRound(255 * m_maskOpacity)));
        grad.setColorAt(0.7, QColor(0, 0, 0, qRound(190 * m_maskOpacity)));
        grad.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(gradRect, grad);
    }

    auto drawButton = [&](const QRect& r, const QString& iconGlyph, bool active) {
        QColor bg = active ? QColor(255, 255, 255, 45) : QColor(255, 255, 255, 28);
        p.setPen(QPen(QColor(255, 255, 255, 52), 1));
        p.setBrush(bg);
        p.drawEllipse(r);

        QFont iconFont("tabler-icons");
        iconFont.setPixelSize(qRound(r.height() * 0.50)); // Match GalleryTopBar icon scale
        p.setFont(iconFont);
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, iconGlyph);
    };

    drawButton(m_backRect, kTopBarBackIcon, m_pressedZone == PressZone::Back);
    drawButton(m_closeRect, kTopBarCloseIcon, m_pressedZone == PressZone::Close);

    QFont titleFont("Roboto");
    titleFont.setPixelSize(qRound(height() * 0.33));
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(Qt::white);
    p.drawText(rect(), Qt::AlignCenter, m_title);
}

// ============================================================
// SettingsView
// ============================================================

SettingsView::SettingsView(QWidget* parent) : BaseView(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setStyleSheet("background-color: black;");

    m_topBar = new SettingsTopBar(this);
    connect(m_topBar, &SettingsTopBar::backTriggered, this, &SettingsView::onTopBarBackTriggered);
    connect(m_topBar, &SettingsTopBar::closeTriggered, this, &SettingsView::onTopBarCloseTriggered);

    m_scrollIndicator = new ScrollIndicator(this);
    connect(m_scrollIndicator, &ScrollIndicator::opacityChanged, this, QOverload<>::of(&QWidget::update));

    m_leftScrollAnim = new QPropertyAnimation(this, "leftScroll", this);
    m_leftScrollAnim->setDuration(m_cfg.SNAP_DURATION_MS);
    m_leftScrollAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_rightScrollAnim = new QPropertyAnimation(this, "rightScroll", this);
    m_rightScrollAnim->setDuration(m_cfg.SNAP_DURATION_MS);
    m_rightScrollAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_splitAnim = new QPropertyAnimation(this, "splitProgress", this);
    m_splitAnim->setDuration(m_cfg.SNAP_DURATION_MS);
    m_splitAnim->setEasingCurve(QEasingCurve::OutCubic);

    buildPrimaryRows();
    rebuildSecondaryRows(0);
}

void SettingsView::onEnter() {
    m_mode = PanelMode::Single;
    m_activePrimary = -1;
    m_leftScroll = 0.0;
    m_rightScroll = 0.0;
    m_splitProgress = 0.0;

    m_leftScrollAnim->stop();
    m_rightScrollAnim->stop();
    m_splitAnim->stop();

    for (auto* row : m_primaryRows) row->setSelected(false);
    m_topBar->setTitle("Settings");
    relayoutRows();
    m_scrollIndicator->forceHide();
    update();
}

void SettingsView::onExit() {
    m_leftScrollAnim->stop();
    m_rightScrollAnim->stop();
    m_splitAnim->stop();
}

void SettingsView::onGestureStarted() {
    m_lastDx = 0;
    m_lastDy = 0;
    m_swipeAxis = SwipeAxis::None;
    m_dragStartLeft = m_leftScroll;
    m_dragStartRight = m_rightScroll;

    m_leftScrollAnim->stop();
    m_rightScrollAnim->stop();
    m_splitAnim->stop();
}

void SettingsView::onGestureUpdate(const QPoint& start, int dx, int dy) {
    m_lastDx = dx;
    m_lastDy = dy;

    if (m_swipeAxis == SwipeAxis::None) {
        if (std::abs(dx) > m_cfg.DEADZONE_PX || std::abs(dy) > m_cfg.DEADZONE_PX) {
            m_swipeAxis = (std::abs(dx) > std::abs(dy)) ? SwipeAxis::Horizontal : SwipeAxis::Vertical;
        }
    }
    if (m_swipeAxis != SwipeAxis::Vertical) return;

    if (m_mode == PanelMode::Expanded && m_splitProgress > 0.95) {
        m_scrollTarget = (start.x() > leftPanelWidth()) ? ScrollTarget::Right : ScrollTarget::Left;
    } else {
        m_scrollTarget = ScrollTarget::Left;
    }

    if (m_scrollTarget == ScrollTarget::Left) {
        qreal candidate = m_dragStartLeft - dy;
        setLeftScroll(applyOverscroll(candidate, leftMaxScroll()));
    } else {
        qreal candidate = m_dragStartRight - dy;
        setRightScroll(applyOverscroll(candidate, rightMaxScroll()));
    }
}

void SettingsView::onGestureFinished(const QPoint& start, int dx, int /*dy*/, float /*vx*/, float vy) {
    if (m_swipeAxis == SwipeAxis::Horizontal) {
        const int edgeZone = qRound(width() * m_cfg.SWIPE_BACK_EDGE_RATIO);
        if (start.x() < edgeZone && dx > width() * m_cfg.SWIPE_BACK_DIST_RATIO) {
            if (m_mode == PanelMode::Expanded) {
                collapseToSingle();
            } else {
                triggerExitToCamera();
            }
            return;
        }
    }

    if (m_swipeAxis == SwipeAxis::Vertical) {
        settleScroll(m_scrollTarget == ScrollTarget::Left, vy);
    }
}

void SettingsView::setLeftScroll(qreal v) {
    if (qFuzzyCompare(m_leftScroll, v)) return;
    m_leftScroll = v;
    relayoutRows();
    refreshTopMask();

    if (m_mode == PanelMode::Single || m_splitProgress < 0.99) {
        m_scrollIndicator->updateState(m_leftScroll, leftMaxScroll());
    }
}

void SettingsView::setRightScroll(qreal v) {
    if (qFuzzyCompare(m_rightScroll, v)) return;
    m_rightScroll = v;
    relayoutRows();
    refreshTopMask();

    if (m_mode == PanelMode::Expanded && m_splitProgress > 0.99) {
        m_scrollIndicator->updateState(m_rightScroll, rightMaxScroll());
    }
}

void SettingsView::setSplitProgress(qreal v) {
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(m_splitProgress, v)) return;
    m_splitProgress = v;
    relayoutRows();
    refreshTopMask();
    update();
}

void SettingsView::resizeEvent(QResizeEvent* /*event*/) {
    relayoutRows();
}

void SettingsView::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0));
    const int topH = topBarHeight();

    if (m_splitProgress > 0.01) {
        const int leftWTarget = leftPanelWidth();
        const int leftW = qRound(width() - (width() - leftWTarget) * m_splitProgress);
        const int dividerW = qMax(1, qRound(width() * m_cfg.DIVIDER_WIDTH_RATIO));
        const int dividerX = leftW;

        QColor divider(255, 255, 255, qRound(70 * m_splitProgress));
        p.fillRect(QRect(dividerX, topH, dividerW, height() - topH), divider);
    }

    if (m_mode == PanelMode::Expanded && m_splitProgress > 0.99) {
        const int leftWTarget = leftPanelWidth();
        const int dividerW = qMax(1, qRound(width() * m_cfg.DIVIDER_WIDTH_RATIO));
        const int rightX = leftWTarget + dividerW;
        QRect rightRect(rightX, topH, width() - rightX, height() - topH);
        m_scrollIndicator->paint(p, rightRect);
    } else {
        QRect singleRect(0, topH, width(), height() - topH);
        m_scrollIndicator->paint(p, singleRect);
    }
}

void SettingsView::onPrimaryRowActivated() {
    auto* row = qobject_cast<SettingsPrimaryRow*>(sender());
    const int index = m_primaryRows.indexOf(row);
    if (index < 0) return;

    if (m_mode == PanelMode::Expanded && m_activePrimary == index) {
        collapseToSingle();
    } else {
        expandPrimary(index);
    }
}

void SettingsView::onSecondaryRowActivated() {
    auto* row = qobject_cast<SettingsSecondaryRow*>(sender());
    const int index = m_secondaryRows.indexOf(row);
    if (index < 0 || m_activePrimary < 0 || m_activePrimary >= kMenuBlueprint.size()) return;

    const auto& sub = kMenuBlueprint[m_activePrimary].subItems;
    if (index < 0 || index >= static_cast<int>(sub.size())) return;
    if (sub[index].type == ActionType::Toggle) {
        row->toggleVisualState();
        return;
    }

    if (auto* app = qobject_cast<App*>(window())) {
        app->showConfirmDialog(sub[index].title.toUpper(), []() {});
    }
}

void SettingsView::onTopBarBackTriggered() {
    if (m_mode == PanelMode::Expanded) {
        collapseToSingle();
    } else {
        triggerExitToCamera();
    }
}

void SettingsView::onTopBarCloseTriggered() {
    triggerExitToCamera();
}

void SettingsView::buildPrimaryRows() {
    for (auto* row : m_primaryRows) delete row;
    m_primaryRows.clear();

    for (const auto& item : kMenuBlueprint) {
        auto* row = new SettingsPrimaryRow(this);
        row->setData(item);
        connect(row, &SettingsPrimaryRow::activated, this, &SettingsView::onPrimaryRowActivated);
        m_primaryRows.append(row);
    }
}

void SettingsView::rebuildSecondaryRows(int primaryIndex) {
    for (auto* row : m_secondaryRows) delete row;
    m_secondaryRows.clear();

    if (primaryIndex < 0 || primaryIndex >= kMenuBlueprint.size()) return;

    for (const auto& item : kMenuBlueprint[primaryIndex].subItems) {
        auto* row = new SettingsSecondaryRow(this);
        row->setData(item);
        connect(row, &SettingsSecondaryRow::activated, this, &SettingsView::onSecondaryRowActivated);
        m_secondaryRows.append(row);
    }
}

void SettingsView::relayoutRows() {
    const int topH = topBarHeight();
    const int rowH = rowHeight();

    m_topBar->setGeometry(0, 0, width(), topH);

    const int leftWTarget = leftPanelWidth();
    const int leftW = qRound(width() - (width() - leftWTarget) * m_splitProgress);
    const int dividerW = qMax(1, qRound(width() * m_cfg.DIVIDER_WIDTH_RATIO));
    const int rightW = qMax(0, width() - leftW - dividerW);
    const int rightX = qRound(width() * (1.0 - m_splitProgress) + (leftW + dividerW) * m_splitProgress);

    for (int i = 0; i < m_primaryRows.size(); ++i) {
        auto* row = m_primaryRows[i];
        row->setTargetLayoutWidth(leftWTarget);
        row->setSplitProgress(m_splitProgress);
        row->setSelected(i == m_activePrimary);
        const bool isLast = (i == m_primaryRows.size() - 1);
        const bool isSelected = (i == m_activePrimary);
        const bool isAboveSelected = (m_activePrimary >= 0 && i == m_activePrimary - 1);
        row->setBottomDividerVisible(!(isLast || isSelected || isAboveSelected));

        const int y = qRound(topH + i * rowH - m_leftScroll);
        row->setGeometry(0, y, leftW, rowH);
        row->setVisible(y < height() && (y + rowH) > topH - rowH);
    }

    for (int i = 0; i < m_secondaryRows.size(); ++i) {
        auto* row = m_secondaryRows[i];
        row->setBottomDividerVisible(i != m_secondaryRows.size() - 1);
        const int y = qRound(topH + i * rowH - m_rightScroll);
        row->setGeometry(rightX, y, rightW, rowH);
        const bool visible = (m_splitProgress > 0.01) && (y < height()) && ((y + rowH) > topH - rowH);
        row->setVisible(visible);
    }

    m_topBar->raise();
    update();
}

void SettingsView::refreshTopMask() {
    const bool leftUnderTop = m_leftScroll > 0.5;
    const bool rightUnderTop = (m_splitProgress > 0.95) && (m_rightScroll > 0.5);
    m_topBar->setMaskOpacity((leftUnderTop || rightUnderTop) ? 1.0 : 0.0);
}

int SettingsView::topBarHeight() const {
    return qMax(44, qRound(height() * m_cfg.TOPBAR_H_RATIO));
}

int SettingsView::rowHeight() const {
    return qMax(40, qRound(height() * m_cfg.ROW_H_RATIO));
}

int SettingsView::leftPanelWidth() const {
    return qRound(width() * m_cfg.LEFT_PANEL_RATIO);
}

qreal SettingsView::leftMaxScroll() const {
    const int contentH = m_primaryRows.size() * rowHeight();
    const int viewH = qMax(0, height() - topBarHeight());
    return qMax(0, contentH - viewH);
}

qreal SettingsView::rightMaxScroll() const {
    const int contentH = m_secondaryRows.size() * rowHeight();
    const int viewH = qMax(0, height() - topBarHeight());
    return qMax(0, contentH - viewH);
}

qreal SettingsView::applyOverscroll(qreal candidate, qreal maxScroll) const {
    if (candidate < 0.0) return candidate * m_cfg.OVERSCROLL_FRICTION;
    if (candidate > maxScroll) return maxScroll + (candidate - maxScroll) * m_cfg.OVERSCROLL_FRICTION;
    return candidate;
}

void SettingsView::settleScroll(bool leftPanel, float velocity) {
    const qreal current = leftPanel ? m_leftScroll : m_rightScroll;
    const qreal maxScroll = leftPanel ? leftMaxScroll() : rightMaxScroll();
    qreal target = current;

    if (current < 0.0) {
        target = 0.0;
    } else if (current > maxScroll) {
        target = maxScroll;
    } else {
        target = qBound(0.0, current - (velocity * 140.0f), maxScroll);
    }

    auto* anim = leftPanel ? m_leftScrollAnim : m_rightScrollAnim;
    anim->stop();
    anim->setStartValue(current);
    anim->setEndValue(target);
    anim->start();
}

void SettingsView::collapseToSingle() {
    if (m_mode == PanelMode::Single && m_splitProgress < 0.01) return;

    m_mode = PanelMode::Single;
    m_activePrimary = -1;
    m_topBar->setTitle("Settings");
    for (auto* row : m_primaryRows) row->setSelected(false);

    m_splitAnim->stop();
    m_splitAnim->setStartValue(m_splitProgress);
    m_splitAnim->setEndValue(0.0);
    m_splitAnim->start();
    m_scrollIndicator->forceHide();
}

void SettingsView::expandPrimary(int primaryIndex) {
    if (primaryIndex < 0 || primaryIndex >= kMenuBlueprint.size()) return;

    m_activePrimary = primaryIndex;
    for (int i = 0; i < m_primaryRows.size(); ++i) {
        m_primaryRows[i]->setSelected(i == m_activePrimary);
    }

    rebuildSecondaryRows(primaryIndex);
    m_rightScroll = 0.0;
    refreshTopMask();
    m_topBar->setTitle(kMenuBlueprint[primaryIndex].title);

    if (m_mode == PanelMode::Single) {
        m_mode = PanelMode::Expanded;
        m_splitAnim->stop();
        m_splitAnim->setStartValue(m_splitProgress);
        m_splitAnim->setEndValue(1.0);
        m_splitAnim->start();
    } else {
        m_mode = PanelMode::Expanded;
        setSplitProgress(1.0);
    }

    relayoutRows();
    m_scrollIndicator->forceHide();
}

void SettingsView::triggerExitToCamera() {
    emit EventBus::instance().cameraRequested();
}
