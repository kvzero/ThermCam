#include "settings_view.h"
#include "ui/widgets/settings_row.h"
#include "ui/widgets/scroll_indicator.h"
#include "core/app_translator.h"
#include "core/event_bus.h"
#include "core/settings_store.h"
#include "hardware/hardware_manager.h"
#include "hardware/platform/system_control.h"
#include "hardware/storage/storage_manager.h"
#include "services/operation_service.h"
#include "ui/app.h"
#include "ui/settings_catalog.h"

#include <QMouseEvent>
#include <QPainter>
#include <QEasingCurve>
#include <QCoreApplication>
#include <QDebug>
#include <cmath>

namespace {

const QString kTopBarBackIcon = QString(QChar(0xea60));
const QString kTopBarCloseIcon = QString(QChar(0xeb55));

QString formatStorageCapacityValue(quint64 mb) {
    if (mb >= 1024) {
        return QString("%1 GB").arg(QString::number(static_cast<double>(mb) / 1024.0, 'f', 1));
    }
    return QString("%1 MB").arg(mb);
}

QString formatStorageCapacity(const StorageVolumeStatus& status) {
    if (!status.ready || status.totalMB == 0) return "--";
    return QString("%1 / %2")
        .arg(formatStorageCapacityValue(status.usedMB))
        .arg(formatStorageCapacityValue(status.totalMB));
}

void showOperationStartFeedback(App* app,
                                OperationStartCode result,
                                const QString& failureText) {
    if (!app || result == OperationStartCode::Started) return;
    app->showToast(result == OperationStartCode::Busy
                       ? SettingsView::tr("SYSTEM OPERATION IN PROGRESS")
                       : failureText,
                   result == OperationStartCode::Busy
                       ? ToastLevel::Info
                       : ToastLevel::Error);
}

} // namespace

/**
 * @brief Full-page layer background that keeps the shared scroll indicator above its black cover.
 *
 * The page rows deliberately remain SettingsView children so they can pass beneath the fixed top
 * bar.  This child therefore cannot be a plain black widget: it is also the correct paint layer
 * for the existing zero-widget scroll indicator while a full page is open.
 */
class SettingsPageBackdrop final : public QWidget {
public:
    explicit SettingsPageBackdrop(ScrollIndicator* scrollIndicator, QWidget* parent)
        : QWidget(parent), m_scrollIndicator(scrollIndicator) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setScrollViewport(const QRect& viewport) {
        if (m_scrollViewport == viewport) return;
        m_scrollViewport = viewport;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        m_scrollIndicator->paint(painter, m_scrollViewport);
    }

private:
    ScrollIndicator* m_scrollIndicator = nullptr;
    QRect m_scrollViewport;
};

// ============================================================
// SettingsTopBar
// ============================================================

SettingsTopBar::SettingsTopBar(QWidget* parent) : QWidget(parent) {
}

void SettingsTopBar::setTitle(const QString& title) {
    if (m_title == title) return;
    m_title = title;
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
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_pressedZone = zoneAt(event->pos());
    if (m_pressedZone == PressZone::None) {
        event->ignore();
        return;
    }

    m_lastPos = event->pos();
    update();
    event->accept();
}

void SettingsTopBar::mouseMoveEvent(QMouseEvent* event) {
    if (m_pressedZone == PressZone::None || !(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    m_lastPos = event->pos();
    update();
    event->accept();
}

void SettingsTopBar::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || m_pressedZone == PressZone::None) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    m_lastPos = event->pos();
    if (m_pressedZone == PressZone::Back && zoneAt(m_lastPos) == PressZone::Back) {
        emit backTriggered();
    } else if (m_pressedZone == PressZone::Close && zoneAt(m_lastPos) == PressZone::Close) {
        emit closeTriggered();
    }
    m_pressedZone = PressZone::None;
    update();
    event->accept();
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

    const QRect gradRect = rect();
    QLinearGradient grad(gradRect.topLeft(), gradRect.bottomLeft());
    grad.setColorAt(0.0, Qt::black);
    grad.setColorAt(0.7, QColor(0, 0, 0, 190));
    grad.setColorAt(1.0, Qt::transparent);
    p.fillRect(gradRect, grad);

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

    const PressZone currentZone = zoneAt(m_lastPos);
    drawButton(m_backRect,
               kTopBarBackIcon,
               m_pressedZone == PressZone::Back && currentZone == PressZone::Back);
    drawButton(m_closeRect,
               kTopBarCloseIcon,
               m_pressedZone == PressZone::Close && currentZone == PressZone::Close);

    QFont titleFont("Roboto");
    titleFont.setPixelSize(qRound(height() * 0.33));
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(Qt::white);
    p.drawText(rect(), Qt::AlignCenter, m_title);
}

// ============================================================
// SettingsView: Lifecycle and Composition
// ============================================================

SettingsView::SettingsView(QWidget* parent) : BaseView(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setStyleSheet("background-color: black;");

    m_topBar = new SettingsTopBar(this);
    connect(m_topBar, &SettingsTopBar::backTriggered, this, &SettingsView::onTopBarBackTriggered);
    connect(m_topBar, &SettingsTopBar::closeTriggered, this, &SettingsView::onTopBarCloseTriggered);

    m_scrollIndicator = new ScrollIndicator(this);
    m_pageBackdrop = new SettingsPageBackdrop(m_scrollIndicator, this);
    connect(m_scrollIndicator, &ScrollIndicator::opacityChanged, this, [this]() {
        update();
        m_pageBackdrop->update();
    });
    m_pageBackdrop->hide();
    initBubbles();

    m_leftScrollAnim = new QPropertyAnimation(this, "leftScroll", this);
    m_leftScrollAnim->setDuration(m_cfg.SNAP_DURATION_MS);
    m_leftScrollAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_rightScrollAnim = new QPropertyAnimation(this, "rightScroll", this);
    m_rightScrollAnim->setDuration(m_cfg.SNAP_DURATION_MS);
    m_rightScrollAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_pageScrollAnim = new QPropertyAnimation(this, "pageScroll", this);
    m_pageScrollAnim->setDuration(m_cfg.SNAP_DURATION_MS);
    m_pageScrollAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_splitAnim = new QPropertyAnimation(this, "splitProgress", this);
    m_splitAnim->setDuration(m_cfg.SNAP_DURATION_MS);
    m_splitAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_rootRetreatAnim = new QPropertyAnimation(this, "rootRetreatProgress", this);
    m_rootRetreatAnim->setDuration(m_cfg.ROOT_PAGE_RETREAT_MS);
    m_rootRetreatAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_pageEntranceAnim = new QPropertyAnimation(this, "pageEntranceProgress", this);
    m_pageEntranceAnim->setDuration(m_cfg.PAGE_ENTER_MS);
    m_pageEntranceAnim->setEasingCurve(QEasingCurve::OutQuart);

    m_pageTransition = new QParallelAnimationGroup(this);
    m_pageTransition->addAnimation(m_rootRetreatAnim);
    m_pageTransition->addAnimation(m_pageEntranceAnim);
    connect(m_pageTransition, &QParallelAnimationGroup::finished, this, [this]() {
        m_pageTransitionInFlight = false;
        if (m_pageEntranceProgress > 0.01) return;

        m_activePage.reset();
        m_pageBackdrop->hide();
        m_topBar->setTitle(m_mode == PanelMode::Expanded
                               ? SettingsCatalog::sectionTitle(m_activePrimary)
                               : tr("Settings"));
        relayoutRows();
    });

    connect(&SettingsService::instance(), &SettingsService::applyCompleted,
            this, &SettingsView::onSettingsApplyCompleted);
    connect(&SettingsStore::instance(), &SettingsStore::settingsChanged, this,
            [this](const SettingsChangeEvent& change) {
                if (m_mode == PanelMode::Expanded &&
                    SettingsCatalog::sectionVisibilityAffectedBySettingsChange(
                        static_cast<SettingsSection>(m_activePrimary), change.changedKeys)) {
                    rebuildSectionItems(m_activePrimary);
                    setRightScroll(qBound<qreal>(0.0, rightScroll(), rightMaxScroll()));
                    relayoutRows();
                    return;
                }
                refreshItemRowsFromSnapshot(m_sectionItems.rows, change.snapshot);
                refreshItemRowsFromSnapshot(m_pageItems.rows, change.snapshot);
            });

    if (auto* storage = HardwareManager::instance().storage()) {
        connect(storage, &StorageManager::sdCardStateChanged, this,
                [this](bool /*ready*/) { refreshStorageRowsIfVisible(); });
        connect(storage, &StorageManager::usbDiskStateChanged, this,
                [this](bool /*ready*/) { refreshStorageRowsIfVisible(); });
    }
    buildPrimaryRows();
    rebuildSectionItems(0);
}

void SettingsView::onEnter() {
    m_mode = PanelMode::Single;
    m_activePrimary = -1;
    m_leftScroll = 0.0;
    m_sectionItems.scroll = 0.0;
    m_pageItems.scroll = 0.0;
    m_splitProgress = 0.0;
    resetPageImmediately();

    m_leftScrollAnim->stop();
    m_rightScrollAnim->stop();
    m_pageScrollAnim->stop();
    m_splitAnim->stop();

    rebuildSectionItems(0);
    for (auto* row : m_primaryRows) row->setSelected(false);
    m_topBar->setTitle(tr("Settings"));
    relayoutRows();
    refreshItemRowsFromStore();
    m_scrollIndicator->forceHide();
    update();
}

void SettingsView::openItem(SettingID item) {
    expandPrimary(SettingsCatalog::sectionIndexForItem(item));

    int rowIndex = -1;
    for (int index = 0; index < m_sectionItems.rows.size(); ++index) {
        if (m_sectionItems.rows[index]->data().id == item) {
            rowIndex = index;
            break;
        }
    }
    if (rowIndex < 0) return;

    const qreal visibleHeight = qMax(0, height() - topBarHeight());
    const qreal target = rowIndex * rowHeight() - (visibleHeight - rowHeight()) * 0.5;
    setRightScroll(qBound<qreal>(0.0, target, rightMaxScroll()));
    relayoutRows();
}

void SettingsView::onExit() {
    cancelPointerSession();
    dismissBubblesImmediately();
    m_leftScrollAnim->stop();
    m_rightScrollAnim->stop();
    m_pageScrollAnim->stop();
    m_splitAnim->stop();
    resetPageImmediately();
}

// ============================================================
// SettingsView: Input and Gesture Handling
// ============================================================

void SettingsView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        BaseView::mousePressEvent(event);
        return;
    }

    startPointerSession(event->pos(), true);
    event->accept();
}

void SettingsView::mouseMoveEvent(QMouseEvent* event) {
    if (!m_pressActive || !(event->buttons() & Qt::LeftButton)) {
        BaseView::mouseMoveEvent(event);
        return;
    }

    updatePointerSession(event->pos());
    event->accept();
}

void SettingsView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !m_pressActive) {
        BaseView::mouseReleaseEvent(event);
        return;
    }

    finishPointerSession(event->pos());
    event->accept();
}

void SettingsView::startPointerSession(const QPoint& pos, bool allowRowPress) {
    cancelPointerSession();
    m_pressActive = true;
    m_pressStartPos = pos;
    m_lastPos = pos;
    m_previousSamplePos = pos;
    m_velocityPxPerMs = QPointF();
    m_velocityTimer.restart();
    m_previousSampleMs = 0;

    m_swipeAxis = SwipeAxis::None;
    m_dragStartLeft = m_leftScroll;
    m_dragStartRight = rightScroll();
    m_dragStartPage = pageScroll();

    m_leftScrollAnim->stop();
    m_rightScrollAnim->stop();
    m_pageScrollAnim->stop();

    m_pressedRow = allowRowPress ? rowAt(pos) : nullptr;
    if (m_pressedRow && !m_pressedRow->beginPress(m_pressedRow->mapFromParent(pos))) {
        m_pressedRow = nullptr;
    }
}

void SettingsView::updatePointerSession(const QPoint& pos) {
    if (!m_pressActive) return;

    updateDragVelocity(pos);
    m_lastPos = pos;

    const int dx = pos.x() - m_pressStartPos.x();
    const int dy = pos.y() - m_pressStartPos.y();
    const int threshold = m_pressedRow
                              ? qMax(m_cfg.DEADZONE_PX, qRound(m_pressedRow->height() * 0.12))
                              : m_cfg.DEADZONE_PX;

    if (m_swipeAxis == SwipeAxis::None) {
        if (std::abs(dx) > threshold || std::abs(dy) > threshold) {
            m_swipeAxis = (std::abs(dx) > std::abs(dy)) ? SwipeAxis::Horizontal : SwipeAxis::Vertical;
        }
    }

    if (m_swipeAxis != SwipeAxis::None) {
        cancelActiveRowPress();
    } else if (m_pressedRow) {
        m_pressedRow->updatePress(m_pressedRow->mapFromParent(pos));
    }

    if (m_swipeAxis != SwipeAxis::Vertical) return;

    if (m_activePage.has_value()) {
        m_scrollTarget = ScrollTarget::Page;
    } else if (m_mode == PanelMode::Expanded && m_splitProgress > 0.95) {
        m_scrollTarget = (m_pressStartPos.x() > leftPanelWidth()) ? ScrollTarget::Right : ScrollTarget::Left;
    } else {
        m_scrollTarget = ScrollTarget::Left;
    }

    if (m_scrollTarget == ScrollTarget::Left) {
        qreal candidate = m_dragStartLeft - dy;
        setLeftScroll(applyOverscroll(candidate, leftMaxScroll()));
    } else if (m_scrollTarget == ScrollTarget::Right) {
        qreal candidate = m_dragStartRight - dy;
        setRightScroll(applyOverscroll(candidate, rightMaxScroll()));
    } else {
        qreal candidate = m_dragStartPage - dy;
        setPageScroll(applyOverscroll(candidate, pageMaxScroll()));
    }
}

void SettingsView::finishPointerSession(const QPoint& pos) {
    if (!m_pressActive) return;

    updateDragVelocity(pos);
    m_pressActive = false;
    m_lastPos = pos;

    const int dx = pos.x() - m_pressStartPos.x();
    if (m_swipeAxis == SwipeAxis::None) {
        auto* row = m_pressedRow;
        m_pressedRow = nullptr;
        if (row) {
            row->releasePress(row->mapFromParent(pos));
        }
        return;
    }

    cancelActiveRowPress();

    if (m_swipeAxis == SwipeAxis::Horizontal) {
        const int edgeZone = qRound(width() * m_cfg.SWIPE_BACK_EDGE_RATIO);
        if (m_pressStartPos.x() < edgeZone && dx > width() * m_cfg.SWIPE_BACK_DIST_RATIO) {
            if (m_activePage.has_value()) {
                closePage();
            } else if (m_mode == PanelMode::Expanded) {
                collapseToSingle();
            } else {
                triggerExitToCamera();
            }
            return;
        }
    }

    if (m_swipeAxis == SwipeAxis::Vertical) {
        if (m_scrollTarget == ScrollTarget::Page) {
            const qreal current = pageScroll();
            const qreal maximum = pageMaxScroll();
            const qreal target = current < 0.0 ? 0.0
                : (current > maximum ? maximum
                   : qBound(0.0, current - m_velocityPxPerMs.y() * 140.0, maximum));
            m_pageScrollAnim->stop();
            m_pageScrollAnim->setStartValue(current);
            m_pageScrollAnim->setEndValue(target);
            m_pageScrollAnim->start();
        } else {
            settleScroll(m_scrollTarget == ScrollTarget::Left,
                         static_cast<float>(m_velocityPxPerMs.y()));
        }
    }
}

void SettingsView::cancelActiveRowPress() {
    if (!m_pressedRow) return;
    m_pressedRow->cancelPress();
    m_pressedRow = nullptr;
}

void SettingsView::updateDragVelocity(const QPoint& pos) {
    const qint64 elapsedMs = m_velocityTimer.isValid() ? m_velocityTimer.elapsed() : 0;
    if (m_previousSampleMs <= 0) {
        m_previousSampleMs = elapsedMs;
        m_previousSamplePos = pos;
        return;
    }

    const qint64 dtMs = elapsedMs - m_previousSampleMs;
    if (dtMs <= 0) return;

    const QPoint delta = pos - m_previousSamplePos;
    if (!delta.isNull()) {
        const QPointF instant(static_cast<qreal>(delta.x()) / static_cast<qreal>(dtMs),
                              static_cast<qreal>(delta.y()) / static_cast<qreal>(dtMs));
        m_velocityPxPerMs = (m_velocityPxPerMs * 0.45) + (instant * 0.55);
    }

    m_previousSampleMs = elapsedMs;
    m_previousSamplePos = pos;
}

SettingsBaseRow* SettingsView::rowAt(const QPoint& pos) const {
    if (pos.y() < topBarHeight()) return nullptr;

    auto findRow = [pos](const auto& rows) -> SettingsBaseRow* {
        for (auto* row : rows) {
            if (row && row->isVisible() && row->geometry().contains(pos)) {
                return row;
            }
        }
        return nullptr;
    };

    if (m_activePage.has_value()) return findRow(m_pageItems.rows);
    if (auto* row = findRow(m_sectionItems.rows)) return row;
    return findRow(m_primaryRows);
}

void SettingsView::cancelPointerSession() {
    m_pressActive = false;
    cancelActiveRowPress();
    m_swipeAxis = SwipeAxis::None;
}

// ============================================================
// SettingsView: Animated Properties and Painting
// ============================================================

void SettingsView::setLeftScroll(qreal v) {
    if (qFuzzyCompare(m_leftScroll, v)) return;
    m_leftScroll = v;
    relayoutRows();

    if (m_mode == PanelMode::Single || m_splitProgress < 0.99) {
        m_scrollIndicator->updateState(m_leftScroll, leftMaxScroll());
    }
}

void SettingsView::setRightScroll(qreal v) {
    if (qFuzzyCompare(m_sectionItems.scroll, v)) return;
    m_sectionItems.scroll = v;
    relayoutRows();

    if (!m_activePage.has_value() && m_mode == PanelMode::Expanded && m_splitProgress > 0.99) {
        m_scrollIndicator->updateState(rightScroll(), rightMaxScroll());
    }
}

void SettingsView::setPageScroll(qreal v) {
    if (qFuzzyCompare(m_pageItems.scroll, v)) return;
    m_pageItems.scroll = v;
    relayoutRows();

    if (m_activePage.has_value()) {
        m_scrollIndicator->updateState(pageScroll(), pageMaxScroll());
    }
}

void SettingsView::setSplitProgress(qreal v) {
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(m_splitProgress, v)) return;
    m_splitProgress = v;
    relayoutRows();
    update();
}

void SettingsView::setRootRetreatProgress(qreal v) {
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(m_rootRetreatProgress, v)) return;
    m_rootRetreatProgress = v;
    relayoutRows();
}

void SettingsView::setPageEntranceProgress(qreal v) {
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(m_pageEntranceProgress, v)) return;
    m_pageEntranceProgress = v;
    relayoutRows();
}

void SettingsView::resizeEvent(QResizeEvent* /*event*/) {
    relayoutRows();
    resizeBubbles();
}

void SettingsView::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0));
    const int topH = topBarHeight();

    if (m_splitProgress > 0.01) {
        const int leftWTarget = leftPanelWidth();
        const int leftW = qRound(width() - (width() - leftWTarget) * m_splitProgress);
        const int dividerW = qMax(1, qRound(width() * m_cfg.DIVIDER_WIDTH_RATIO));
        const int rootOffset = -qRound(width() * m_cfg.ROOT_PAGE_RETREAT_RATIO *
                                       m_rootRetreatProgress);
        const int dividerX = leftW + rootOffset;

        QColor divider(255, 255, 255, qRound(70 * m_splitProgress));
        p.fillRect(QRect(dividerX, topH, dividerW, height() - topH), divider);
    }

    if (m_activePage.has_value()) {
        return;
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

// ============================================================
// SettingsView: Bubble Editor Flow
// ============================================================

void SettingsView::initBubbles() {
    m_radioListBubble = new RadioListBubble(this);
    m_radioListBubble->hide();
    connectBubble(m_radioListBubble);

    m_sliderBubble = new SliderBubble(this);
    m_sliderBubble->hide();
    connectBubble(m_sliderBubble);

    m_stepperBubble = new StepperBubble(this);
    m_stepperBubble->hide();
    connectBubble(m_stepperBubble);
}

void SettingsView::connectBubble(BubbleBase* bubble) {
    if (!bubble) return;

    connect(bubble, &BubbleBase::outsideDragStarted,
            this, &SettingsView::onBubbleOutsideDragStarted);
    connect(bubble, &BubbleBase::outsideDragMoved,
            this, &SettingsView::onBubbleOutsideDragMoved);
    connect(bubble, &BubbleBase::outsideDragReleased,
            this, &SettingsView::onBubbleOutsideDragReleased);
    connect(bubble, &BubbleBase::outsideDragCanceled,
            this, &SettingsView::onBubbleOutsideDragCanceled);
}

void SettingsView::resizeBubbles() {
    const QRect bounds = rect();
    if (m_radioListBubble) m_radioListBubble->setGeometry(bounds);
    if (m_sliderBubble) m_sliderBubble->setGeometry(bounds);
    if (m_stepperBubble) m_stepperBubble->setGeometry(bounds);
}

void SettingsView::raiseVisibleBubbles() {
    if (m_radioListBubble && m_radioListBubble->isVisible()) m_radioListBubble->raise();
    if (m_sliderBubble && m_sliderBubble->isVisible()) m_sliderBubble->raise();
    if (m_stepperBubble && m_stepperBubble->isVisible()) m_stepperBubble->raise();
}

void SettingsView::dismissBubblesImmediately() {
    if (m_radioListBubble && m_radioListBubble->isVisible()) {
        m_radioListBubble->dismissImmediately();
    }
    if (m_sliderBubble && m_sliderBubble->isVisible()) {
        m_sliderBubble->dismissImmediately();
    }
    if (m_stepperBubble && m_stepperBubble->isVisible()) {
        m_stepperBubble->dismissImmediately();
    }
}

void SettingsView::showRadioListBubble(const RadioListBubble::Spec& spec,
                                       const BubbleAnchorContext& anchor) {
    if (!m_radioListBubble) return;
    if (m_sliderBubble && m_sliderBubble->isVisible()) {
        m_sliderBubble->dismissImmediately();
    }
    if (m_stepperBubble && m_stepperBubble->isVisible()) {
        m_stepperBubble->dismissImmediately();
    }
    m_radioListBubble->raise();
    m_radioListBubble->present(spec, anchor);
}

void SettingsView::showSliderBubble(const SliderBubble::Spec& spec,
                                    const BubbleAnchorContext& anchor) {
    if (!m_sliderBubble) return;
    if (m_radioListBubble && m_radioListBubble->isVisible()) {
        m_radioListBubble->dismissImmediately();
    }
    if (m_stepperBubble && m_stepperBubble->isVisible()) {
        m_stepperBubble->dismissImmediately();
    }
    m_sliderBubble->raise();
    m_sliderBubble->present(spec, anchor);
}

void SettingsView::showStepperBubble(const StepperBubble::Spec& spec,
                                     const BubbleAnchorContext& anchor) {
    if (!m_stepperBubble) return;
    if (m_radioListBubble && m_radioListBubble->isVisible()) {
        m_radioListBubble->dismissImmediately();
    }
    if (m_sliderBubble && m_sliderBubble->isVisible()) {
        m_sliderBubble->dismissImmediately();
    }
    m_stepperBubble->raise();
    m_stepperBubble->present(spec, anchor);
}

void SettingsView::onBubbleOutsideDragStarted(const QPoint& startGlobal,
                                              const QPoint& currentGlobal) {
    startPointerSession(mapFromGlobal(startGlobal), false);
    updatePointerSession(mapFromGlobal(currentGlobal));
}

void SettingsView::onBubbleOutsideDragMoved(const QPoint& currentGlobal) {
    updatePointerSession(mapFromGlobal(currentGlobal));
}

void SettingsView::onBubbleOutsideDragReleased(const QPoint& finalGlobal) {
    finishPointerSession(mapFromGlobal(finalGlobal));
}

void SettingsView::onBubbleOutsideDragCanceled() {
    cancelPointerSession();
}

// ============================================================
// SettingsView: Item Activation and Commands
// ============================================================

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

void SettingsView::onItemRowActivated() {
    auto* row = qobject_cast<SettingsItemRow*>(sender());
    if (!row) return;
    const SettingsItemData item = row->data();

    switch (item.role) {
    case SettingsItemRole::Setting:
        activateSettingItem(row, item);
        return;
    case SettingsItemRole::Status:
        return;
    case SettingsItemRole::Command:
        activateCommandItem(item);
        return;
    case SettingsItemRole::Navigation:
        openPage(*item.destinationSection, item.title);
        return;
    }
}

void SettingsView::activateSettingItem(SettingsItemRow* row,
                                       const SettingsItemData& item) {
    auto buildAnchor = [this, row]() {
        BubbleAnchorContext anchor;
        anchor.pressPosGlobal = row->mapToGlobal(row->rect().center());
        anchor.triggerRectGlobal = QRect(row->mapToGlobal(QPoint(0, 0)), row->size());

        const QRect rowLocal = row->geometry();
        const QRect submenuLocal(rowLocal.x(),
                                 topBarHeight(),
                                 rowLocal.width(),
                                 qMax(1, height() - topBarHeight()));
        anchor.submenuRectGlobal = QRect(mapToGlobal(submenuLocal.topLeft()), submenuLocal.size());
        anchor.submenuContentTopGlobalY = anchor.submenuRectGlobal.top();
        anchor.submenuContentBottomGlobalY = anchor.submenuRectGlobal.bottom();
        anchor.referenceRowHeightPx = rowHeight();
        return anchor;
    };

    const SettingsSnapshot snapshot = SettingsStore::instance().current();
    switch (*item.editor) {
    case SettingsEditor::Toggle: {
        const bool current = snapshot.values.value(*item.settingKey).toBool();
        SettingsPatch patch;
        patch.values.insert(*item.settingKey, !current);
        applyPatchFromUi(patch);
        return;
    }
    case SettingsEditor::Stepper: {
        const SettingsNumberEditor editor = SettingsCatalog::numberEditor(item.id, snapshot);
        StepperBubble::Spec spec;
        spec.minValue = editor.minimum;
        spec.maxValue = editor.maximum;
        spec.step = editor.step;
        spec.value = editor.value;
        spec.dismissOnCommit = editor.dismissOnCommit;
        spec.valueTextFormatter = [id = item.id](double value) {
            return SettingsCatalog::editedValueText(id, value);
        };
        spec.onValueChanging = [row, item](double value) {
            row->setValueText(SettingsCatalog::editedValueText(item.id, value));
            SettingsPatch patch;
            patch.values.insert(*item.settingKey,
                                SettingsCatalog::numberValue(item.id, value));
            SettingsService::instance().preview(patch);
        };
        spec.onValueCommitted = [this, item](double value) {
            SettingsPatch patch;
            patch.values.insert(*item.settingKey,
                                SettingsCatalog::numberValue(item.id, value));
            applyPatchFromUi(patch);
        };
        showStepperBubble(spec, buildAnchor());
        return;
    }
    case SettingsEditor::Slider: {
        const SettingsNumberEditor editor = SettingsCatalog::numberEditor(item.id, snapshot);
        SliderBubble::Spec spec;
        spec.iconGlyph = editor.iconGlyph;
        spec.minValue = editor.minimum;
        spec.maxValue = editor.maximum;
        spec.step = editor.step;
        spec.value = editor.value;
        spec.dismissOnCommit = editor.dismissOnCommit;
        spec.changingThrottleMs = editor.previewThrottleMs;
        spec.onValueChanging = [row, item](double value) {
            row->setValueText(SettingsCatalog::editedValueText(item.id, value));
            SettingsPatch patch;
            patch.values.insert(*item.settingKey,
                                SettingsCatalog::numberValue(item.id, value));
            SettingsService::instance().preview(patch);
        };
        spec.onValueCommitted = [this, item](double value) {
            SettingsPatch patch;
            patch.values.insert(*item.settingKey,
                                SettingsCatalog::numberValue(item.id, value));
            applyPatchFromUi(patch);
        };
        showSliderBubble(spec, buildAnchor());
        return;
    }
    case SettingsEditor::Choice: {
        const SettingsChoiceEditor editor = SettingsCatalog::choiceEditor(item.id, snapshot);
        RadioListBubble::Spec spec;
        for (const SettingsChoiceOption& option : editor.options) {
            spec.items.append({option.id, option.title});
        }
        spec.selectedIndex = editor.selectedIndex;
        spec.dismissOnSelection = true;
        spec.onSelected = [this, item](int index, const QString&) {
            SettingsPatch patch;
            patch.values.insert(*item.settingKey, SettingsCatalog::choiceValue(item.id, index));
            applyPatchFromUi(patch);
        };
        showRadioListBubble(spec, buildAnchor());
        return;
    }
    case SettingsEditor::Palette:
        emit EventBus::instance().cameraRequested(QRect(), TransitionMode::Instant);
        emit EventBus::instance().paletteSelectorRequested(true);
        return;
    }
}

void SettingsView::activateCommandItem(const SettingsItemData& item) {
    auto* app = qobject_cast<App*>(window());

    if (item.id == SettingID::TriggerFlatSceneCorrection) {
        if (!app) return;
        ModalSpec spec;
        spec.onPrimaryAction = [app]() {
            const OperationStartCode result =
                OperationService::instance().startFlatSceneCorrection();
            showOperationStartFeedback(
                app, result, SettingsView::tr("FLAT-SCENE CORRECTION FAILED"));
        };
        app->showTextModal(
            tr("FLAT-SCENE CORRECTION"),
            tr("Cover the lens with a uniform surface before continuing.\n"
               "Incorrect setup may cause ghosting."),
            spec,
            TextModalSize::Large);
        return;
    }
    if (item.id == SettingID::Clock) {
        if (!app) return;
        app->showClockModal([](const QDateTime& dateTime, QString* outError) {
            auto* system = HardwareManager::instance().systemControl();
            if (!system) {
                if (outError) *outError = "System control is unavailable";
                return false;
            }
            const bool ok = system->setSystemDateTime(dateTime, outError);
            if (!ok && outError) {
                qWarning() << "[Settings] Set date/time failed:" << *outError;
            }
            return ok;
        });
        return;
    }
    if (item.id == SettingID::RebootToLoader) {
        if (!app) return;
        ModalSpec spec;
        spec.onPrimaryAction = [app]() {
            if (OperationService::instance().isBusy()) {
                app->showToast(SettingsView::tr("SYSTEM OPERATION IN PROGRESS"),
                               ToastLevel::Info);
                return;
            }

            auto* system = HardwareManager::instance().systemControl();
            QString error;
            if (!system || !system->rebootToLoader(&error)) {
                qWarning() << "[Settings] Reboot to Loader failed:" << error;
                app->showToast(SettingsView::tr("REBOOT FAILED"), ToastLevel::Error);
            }
        };
        app->showTextModal(
            tr("REBOOT TO LOADER?"),
            tr("The camera will restart in Loader mode."),
            spec,
            TextModalSize::Normal);
        return;
    }
    if (item.id == SettingID::InitializeUserdata) {
        if (!app) return;
        app->showWarningModal(
            tr("INITIALIZE USERDATA?"),
            tr("All internal data will be erased.\n"
               "The camera will restart when complete."),
            [app]() {
                const OperationStartCode result =
                    OperationService::instance().startInitializeUserdata();
                showOperationStartFeedback(
                    app,
                    result,
                    SettingsView::tr("USERDATA INITIALIZATION FAILED"));
            });
        return;
    }
    if (item.id == SettingID::CalibrateHapticMotor) {
        if (!app) return;
        ModalSpec spec;
        spec.onPrimaryAction = [app]() {
            const OperationStartCode result =
                OperationService::instance().startHapticCalibration();
            showOperationStartFeedback(
                app,
                result,
                SettingsView::tr("HAPTIC MOTOR CALIBRATION FAILED"));
        };
        app->showTextModal(
            tr("CALIBRATE HAPTIC MOTOR?"),
            tr("The motor will vibrate briefly during calibration."),
            spec,
            TextModalSize::Normal);
        return;
    }
    if (item.id == SettingID::RestoreDefaults) {
        if (!app) return;
        ModalSpec spec;
        spec.onPrimaryAction = [this, app]() {
            m_applyInFlight = true;
            const SettingsService::ApplyResult result =
                SettingsService::instance().restoreDefaults();
            if (result.code == SettingsService::ApplyCode::Ok ||
                result.code == SettingsService::ApplyCode::NoChange) {
                app->showToast(tr("SETTINGS RESTORED"), ToastLevel::Success);
            }
        };
        app->showTextModal(
            tr("RESTORE DEFAULTS?"),
            tr("All settings will be reset.\nMedia & calibration unchanged."),
            spec,
            TextModalSize::Large);
        return;
    }
    if (item.id == SettingID::About) {
        if (!app) return;
        ModalSpec spec;
        spec.level = ModalLevel::Normal;
        spec.primaryText = tr("CLOSE");
        spec.showSecondaryButton = false;
        app->showTextModal(tr("ABOUT"),
                           QStringLiteral("ThermCam · v0.1.0\n"
                                          "github.com/kvzero/ThermCam\n"
                                          "© 2026 kvzero · GPLv3"),
                           spec,
                           TextModalSize::Large);
        return;
    }
    if (item.id == SettingID::SdCardSafeEject || item.id == SettingID::UsbDiskSafeEject) {
        const bool sdCard = (item.id == SettingID::SdCardSafeEject);
        const QString targetName = sdCard ? tr("SD Card") : tr("USB Disk");
        if (!app) return;
        ModalSpec spec;
        spec.level = ModalLevel::Normal;
        spec.onPrimaryAction = [this, app, targetName, sdCard]() {
            auto* storage = HardwareManager::instance().storage();
            if (!storage) {
                app->showToast(SettingsView::tr("STORAGE UNAVAILABLE"), ToastLevel::Error);
                return;
            }

            QString error;
            const StorageVolume targetVolume = sdCard
                                                   ? StorageVolume::SdCard
                                                   : StorageVolume::UsbDisk;
            const bool ok = storage->safeEjectVolume(targetVolume, &error);

            if (!ok) {
                qWarning() << "[Settings] Safe eject failed:" << targetName
                           << "reason:" << error;
                app->showToast(SettingsView::tr("%1 ejection failed").arg(targetName),
                               ToastLevel::Error);
            } else {
                app->showToast(SettingsView::tr("%1 can be removed").arg(targetName),
                               ToastLevel::Success);
            }

            refreshStorageRowsIfVisible();
        };
        app->showTextModal(
            tr("EJECT %1?").arg(targetName.toUpper()),
            tr("Wait for confirmation before removing it."),
            spec,
            TextModalSize::Normal);
        return;
    }
    if (item.id == SettingID::SdCardFormat || item.id == SettingID::UsbDiskFormat) {
        const bool sdCard = (item.id == SettingID::SdCardFormat);
        const QString targetName = sdCard ? tr("SD Card") : tr("USB Disk");
        const StorageVolume targetVolume = sdCard ? StorageVolume::SdCard
                                                  : StorageVolume::UsbDisk;
        if (!app) return;
        auto* storage = HardwareManager::instance().storage();
        const QString fileSystem = storage ? storage->formatFileSystemName(targetVolume)
                                           : QString();
        const QString warningBody = fileSystem.isEmpty()
            ? tr("All files will be deleted.")
            : tr("All files will be deleted.\nIt will be formatted as %1.")
                  .arg(fileSystem);
        app->showWarningModal(tr("FORMAT %1?").arg(targetName.toUpper()),
                              warningBody,
                              [app, targetName, targetVolume]() {
                                  const OperationStartCode result =
                                      OperationService::instance().startFormatVolume(targetVolume);
                                  showOperationStartFeedback(
                                      app,
                                      result,
                                      SettingsView::tr("%1 formatting failed").arg(targetName));
                              });
        return;
    }
}

void SettingsView::onTopBarBackTriggered() {
    if (m_activePage.has_value()) {
        closePage();
    } else if (m_mode == PanelMode::Expanded) {
        collapseToSingle();
    } else {
        triggerExitToCamera();
    }
}

void SettingsView::onTopBarCloseTriggered() {
    triggerExitToCamera();
}

// ============================================================
// SettingsView: Row Construction and Layout
// ============================================================

void SettingsView::buildPrimaryRows() {
    cancelActiveRowPress();
    for (auto* row : m_primaryRows) delete row;
    m_primaryRows.clear();

    for (int index = 0; index < SettingsCatalog::sectionCount(); ++index) {
        auto* row = new SettingsPrimaryRow(this);
        row->setData(SettingsCatalog::sectionAt(index));
        connect(row, &SettingsPrimaryRow::activated, this, &SettingsView::onPrimaryRowActivated);
        m_primaryRows.append(row);
    }
}

void SettingsView::rebuildSectionItems(int primaryIndex) {
    auto* storage = HardwareManager::instance().storage();
    const bool sdCardReady = storage && storage->isSdCardReady();
    const bool usbDiskReady = storage && storage->isUsbDiskReady();
    const bool userdataUbiAttached = storage && storage->isUserdataUbiAttached();
    rebuildItemRows(m_sectionItems.rows,
                    SettingsCatalog::visibleItems(static_cast<SettingsSection>(primaryIndex),
                                                  SettingsStore::instance().current(),
                                                  sdCardReady,
                                                  usbDiskReady,
                                                  userdataUbiAttached));
    refreshItemRowsFromSnapshot(m_sectionItems.rows, SettingsStore::instance().current());
}

void SettingsView::rebuildPageItems(SettingsSection section) {
    cancelActiveRowPress();
    auto* storage = HardwareManager::instance().storage();
    const bool sdCardReady = storage && storage->isSdCardReady();
    const bool usbDiskReady = storage && storage->isUsbDiskReady();
    const bool userdataUbiAttached = storage && storage->isUserdataUbiAttached();
    rebuildItemRows(m_pageItems.rows,
                    SettingsCatalog::visibleItems(section,
                                                  SettingsStore::instance().current(),
                                                  sdCardReady,
                                                  usbDiskReady,
                                                  userdataUbiAttached));
    refreshItemRowsFromSnapshot(m_pageItems.rows, SettingsStore::instance().current());
}

void SettingsView::rebuildItemRows(QVector<SettingsItemRow*>& rows,
                                   const std::vector<SettingsItemData>& items) {
    cancelActiveRowPress();
    for (auto* row : rows) delete row;
    rows.clear();

    for (const auto& item : items) {
        auto* row = new SettingsItemRow(this);
        row->setData(item);
        connect(row, &SettingsItemRow::activated, this, &SettingsView::onItemRowActivated);
        rows.append(row);
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
    const int rootOffset = -qRound(width() * m_cfg.ROOT_PAGE_RETREAT_RATIO *
                                   m_rootRetreatProgress);

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
        row->setGeometry(rootOffset, y, leftW, rowH);
        row->setVisible(y < height() && (y + rowH) > topH - rowH);
    }

    layoutItemRows(m_sectionItems.rows, rightScroll(), rightX + rootOffset, rightW,
                   m_splitProgress > 0.01);

    const int pageX = qRound(width() * (1.0 - m_pageEntranceProgress));
    m_pageBackdrop->setGeometry(pageX, 0, width(), height());
    m_pageBackdrop->setScrollViewport(QRect(0, topH, width(), qMax(0, height() - topH)));
    layoutItemRows(m_pageItems.rows, pageScroll(), pageX, width(), m_activePage.has_value());
    if (m_activePage.has_value()) {
        m_pageBackdrop->raise();
        for (auto* row : m_pageItems.rows) row->raise();
    }

    m_topBar->raise();
    raiseVisibleBubbles();
    update();
}

void SettingsView::layoutItemRows(const QVector<SettingsItemRow*>& rows,
                                  qreal scroll,
                                  int x,
                                  int itemWidth,
                                  bool visible) {
    const int topH = topBarHeight();
    const int rowH = rowHeight();
    for (int i = 0; i < rows.size(); ++i) {
        auto* row = rows[i];
        row->setBottomDividerVisible(i != rows.size() - 1);
        const int y = qRound(topH + i * rowH - scroll);
        row->setGeometry(x, y, itemWidth, rowH);
        row->setVisible(visible && y < height() && (y + rowH) > topH - rowH);
    }
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
    return itemRowsMaxScroll(m_sectionItems.rows);
}

qreal SettingsView::pageMaxScroll() const {
    return itemRowsMaxScroll(m_pageItems.rows);
}

qreal SettingsView::itemRowsMaxScroll(const QVector<SettingsItemRow*>& rows) const {
    const int contentH = rows.size() * rowHeight();
    const int viewH = qMax(0, height() - topBarHeight());
    return qMax(0, contentH - viewH);
}

qreal SettingsView::applyOverscroll(qreal candidate, qreal maxScroll) const {
    if (candidate < 0.0) return candidate * m_cfg.OVERSCROLL_FRICTION;
    if (candidate > maxScroll) return maxScroll + (candidate - maxScroll) * m_cfg.OVERSCROLL_FRICTION;
    return candidate;
}

void SettingsView::settleScroll(bool leftPanel, float velocity) {
    const qreal current = leftPanel ? m_leftScroll : rightScroll();
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

// ============================================================
// SettingsView: Navigation and Transitions
// ============================================================

void SettingsView::collapseToSingle() {
    if (m_mode == PanelMode::Single && m_splitProgress < 0.01) return;

    m_mode = PanelMode::Single;
    m_activePrimary = -1;
    m_topBar->setTitle(tr("Settings"));
    for (auto* row : m_primaryRows) row->setSelected(false);

    m_splitAnim->stop();
    m_splitAnim->setStartValue(m_splitProgress);
    m_splitAnim->setEndValue(0.0);
    m_splitAnim->start();
    m_scrollIndicator->forceHide();
}

void SettingsView::expandPrimary(int primaryIndex) {
    m_activePrimary = primaryIndex;
    for (int i = 0; i < m_primaryRows.size(); ++i) {
        m_primaryRows[i]->setSelected(i == m_activePrimary);
    }

    rebuildSectionItems(primaryIndex);
    setRightScroll(0.0);
    m_topBar->setTitle(SettingsCatalog::sectionTitle(primaryIndex));

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
    emit EventBus::instance().cameraRequested(QRect(), TransitionMode::Auto);
}

void SettingsView::openPage(SettingsSection section, const QString& title) {
    if (m_activePage.has_value() || m_pageTransitionInFlight) return;

    cancelPointerSession();
    dismissBubblesImmediately();
    m_activePage = section;
    m_pageBackdrop->show();
    rebuildPageItems(section);
    m_pageItems.scroll = 0.0;
    m_topBar->setTitle(title);

    m_pageTransitionInFlight = true;
    m_pageTransition->stop();
    m_rootRetreatAnim->setStartValue(m_rootRetreatProgress);
    m_rootRetreatAnim->setEndValue(1.0);
    m_pageEntranceAnim->setStartValue(m_pageEntranceProgress);
    m_pageEntranceAnim->setEndValue(1.0);
    m_pageTransition->start();
}

void SettingsView::closePage() {
    if (!m_activePage.has_value() || m_pageTransitionInFlight) return;

    cancelPointerSession();
    m_pageTransitionInFlight = true;
    m_pageTransition->stop();
    m_rootRetreatAnim->setStartValue(m_rootRetreatProgress);
    m_rootRetreatAnim->setEndValue(0.0);
    m_pageEntranceAnim->setStartValue(m_pageEntranceProgress);
    m_pageEntranceAnim->setEndValue(0.0);
    m_pageTransition->start();
}

void SettingsView::resetPageImmediately() {
    if (m_pageTransition) m_pageTransition->stop();
    m_pageTransitionInFlight = false;
    m_activePage.reset();
    if (m_pageBackdrop) m_pageBackdrop->hide();
    m_rootRetreatProgress = 0.0;
    m_pageEntranceProgress = 0.0;
}

// ============================================================
// SettingsView: Data Refresh and Apply Feedback
// ============================================================

void SettingsView::refreshItemRowsFromSnapshot(QVector<SettingsItemRow*>& rows,
                                               const SettingsSnapshot& snapshot) {
    StorageVolumeStatus sdStatus;
    StorageVolumeStatus usbStatus;
    StorageVolumeStatus nandStatus;
    if (auto* storage = HardwareManager::instance().storage()) {
        sdStatus = storage->volumeStatus(StorageVolume::SdCard);
        usbStatus = storage->volumeStatus(StorageVolume::UsbDisk);
        nandStatus = storage->volumeStatus(StorageVolume::Nand);
    }

    for (auto* row : rows) {
        const SettingsItemData item = row->data();
        row->setValueText(QString());
        row->setToggleOn(false);

        if (item.editor == SettingsEditor::Toggle) {
            row->setToggleOn(snapshot.values.value(*item.settingKey).toBool());
            continue;
        }

        if (item.id == SettingID::InternalStorageCapacity) {
            row->setValueText(formatStorageCapacity(nandStatus));
            continue;
        }
        if (item.id == SettingID::SdCardCapacity) {
            row->setValueText(formatStorageCapacity(sdStatus));
            continue;
        }
        if (item.id == SettingID::UsbDiskCapacity) {
            row->setValueText(formatStorageCapacity(usbStatus));
            continue;
        }

        row->setValueText(SettingsCatalog::valueText(item.id, snapshot));
    }
}

void SettingsView::refreshItemRowsFromStore() {
    const SettingsSnapshot snapshot = SettingsStore::instance().current();
    refreshItemRowsFromSnapshot(m_sectionItems.rows, snapshot);
    refreshItemRowsFromSnapshot(m_pageItems.rows, snapshot);
}

void SettingsView::refreshStorageRowsIfVisible() {
    if (m_mode != PanelMode::Expanded ||
        !SettingsCatalog::sectionVisibilityAffectedByStorageState(
            static_cast<SettingsSection>(m_activePrimary))) {
        return;
    }
    rebuildSectionItems(m_activePrimary);
    setRightScroll(qBound<qreal>(0.0, rightScroll(), rightMaxScroll()));
    relayoutRows();
}

void SettingsView::refreshLanguage() {
    dismissBubblesImmediately();

    const PanelMode previousMode = m_mode;
    const int previousPrimary = m_activePrimary;
    const qreal previousLeftScroll = m_leftScroll;
    const qreal previousRightScroll = rightScroll();

    buildPrimaryRows();

    if (previousMode == PanelMode::Expanded && previousPrimary >= 0) {
        m_mode = PanelMode::Expanded;
        m_activePrimary = previousPrimary;
        rebuildSectionItems(previousPrimary);
        m_topBar->setTitle(SettingsCatalog::sectionTitle(previousPrimary));
    } else {
        m_mode = PanelMode::Single;
        m_activePrimary = -1;
        rebuildSectionItems(0);
        m_topBar->setTitle(tr("Settings"));
    }

    m_leftScroll = qBound<qreal>(0.0, previousLeftScroll, leftMaxScroll());
    setRightScroll(qBound<qreal>(0.0, previousRightScroll, rightMaxScroll()));
    relayoutRows();
}

void SettingsView::applyPatchFromUi(const SettingsPatch& patch) {
    if (patch.isEmpty()) return;

    m_applyInFlight = true;
    SettingsService::instance().apply(patch);
}

void SettingsView::onSettingsApplyCompleted(const SettingsService::ApplyResult& result) {
    if (!m_applyInFlight) return;
    m_applyInFlight = false;

    if (result.code == SettingsService::ApplyCode::Ok ||
        result.code == SettingsService::ApplyCode::NoChange) {
        if (result.change.changedKeys.contains(SettingKey::AppLanguage)) {
            auto* application = QCoreApplication::instance();
            const AppLanguage language = appLanguageFromValue(
                result.change.snapshot.values.value(SettingKey::AppLanguage).toInt());
            if (application && AppTranslator::instance().setLanguage(*application, language)) {
                refreshLanguage();
            }
        }
        return;
    }

    auto* app = qobject_cast<App*>(window());
    if (!app) return;

    if (result.code == SettingsService::ApplyCode::RuntimeApplyFailed && result.persisted) {
        app->showToast(tr("APPLY DEFERRED"), ToastLevel::Warning);
        return;
    }

    app->showToast(tr("SET FAILED"), ToastLevel::Error);
}
