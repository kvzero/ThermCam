#include "settings_view.h"
#include "ui/widgets/settings_row.h"
#include "ui/widgets/scroll_indicator.h"
#include "core/event_bus.h"
#include "core/settings_store.h"
#include "hardware/hardware_manager.h"
#include "hardware/hmi/system_control.h"
#include "hardware/imaging/thermal_camera.h"
#include "hardware/storage/storage_manager.h"
#include "ui/app.h"
#include "ui/settings_catalog.h"

#include <QMouseEvent>
#include <QPainter>
#include <QEasingCurve>
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

} // namespace

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
    initBubbles();

    m_leftScrollAnim = new QPropertyAnimation(this, "leftScroll", this);
    m_leftScrollAnim->setDuration(m_cfg.SNAP_DURATION_MS);
    m_leftScrollAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_rightScrollAnim = new QPropertyAnimation(this, "rightScroll", this);
    m_rightScrollAnim->setDuration(m_cfg.SNAP_DURATION_MS);
    m_rightScrollAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_splitAnim = new QPropertyAnimation(this, "splitProgress", this);
    m_splitAnim->setDuration(m_cfg.SNAP_DURATION_MS);
    m_splitAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(&SettingsService::instance(), &SettingsService::applyCompleted,
            this, &SettingsView::onSettingsApplyCompleted);
    connect(&SettingsStore::instance(), &SettingsStore::settingsChanged, this,
            [this](const SettingsChangeEvent& change) {
                if (m_mode == PanelMode::Expanded &&
                    SettingsCatalog::sectionVisibilityAffectedBySettingsChange(
                        m_activePrimary, change.changedKeys)) {
                    rebuildSecondaryRows(m_activePrimary);
                    m_rightScroll = qBound<qreal>(0.0, m_rightScroll, rightMaxScroll());
                    relayoutRows();
                    return;
                }
                refreshSecondaryRowsFromSnapshot(change.snapshot);
            });

    if (auto* storage = HardwareManager::instance().storage()) {
        connect(storage, &StorageManager::sdCardStateChanged, this,
                [this](bool /*ready*/) { refreshStorageRowsIfVisible(); });
        connect(storage, &StorageManager::usbDiskStateChanged, this,
                [this](bool /*ready*/) { refreshStorageRowsIfVisible(); });
    }

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

    rebuildSecondaryRows(0);
    for (auto* row : m_primaryRows) row->setSelected(false);
    m_topBar->setTitle(QStringLiteral("Settings"));
    relayoutRows();
    refreshSecondaryRowsFromStore();
    m_scrollIndicator->forceHide();
    update();
}

void SettingsView::openItem(SettingID item) {
    expandPrimary(SettingsCatalog::sectionIndexForItem(item));

    int rowIndex = -1;
    for (int index = 0; index < m_secondaryRows.size(); ++index) {
        if (m_secondaryRows[index]->data().id == item) {
            rowIndex = index;
            break;
        }
    }
    if (rowIndex < 0) return;

    const qreal visibleHeight = qMax(0, height() - topBarHeight());
    const qreal target = rowIndex * rowHeight() - (visibleHeight - rowHeight()) * 0.5;
    setRightScroll(qBound<qreal>(0.0, target, rightMaxScroll()));
    refreshTopMask();
    relayoutRows();
}

void SettingsView::onExit() {
    cancelPointerSession();
    dismissBubblesImmediately();
    m_leftScrollAnim->stop();
    m_rightScrollAnim->stop();
    m_splitAnim->stop();
}

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
    m_dragStartRight = m_rightScroll;

    m_leftScrollAnim->stop();
    m_rightScrollAnim->stop();

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

    if (m_mode == PanelMode::Expanded && m_splitProgress > 0.95) {
        m_scrollTarget = (m_pressStartPos.x() > leftPanelWidth()) ? ScrollTarget::Right : ScrollTarget::Left;
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
            if (m_mode == PanelMode::Expanded) {
                collapseToSingle();
            } else {
                triggerExitToCamera();
            }
            return;
        }
    }

    if (m_swipeAxis == SwipeAxis::Vertical) {
        settleScroll(m_scrollTarget == ScrollTarget::Left,
                     static_cast<float>(m_velocityPxPerMs.y()));
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

    if (auto* row = findRow(m_secondaryRows)) return row;
    return findRow(m_primaryRows);
}

void SettingsView::cancelPointerSession() {
    m_pressActive = false;
    cancelActiveRowPress();
    m_swipeAxis = SwipeAxis::None;
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
    if (!row) return;
    const SecondaryItemData item = row->data();

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

    auto* app = qobject_cast<App*>(window());

    const SettingsSnapshot snapshot = SettingsStore::instance().current();
    switch (item.editor) {
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
    case SettingsEditor::Action:
        break;
    }

    if (item.id == SettingID::TriggerFlatSceneCorrection) {
        if (!app) return;
        app->showTextModal(
            QStringLiteral("FLAT-SCENE CORRECTION"),
            QStringLiteral("Cover the lens with a uniform surface before continuing.\n"
                           "Incorrect setup may cause ghosting."),
            [app]() {
                QString error;
                if (!SettingsService::instance().triggerFlatSceneCorrection(&error)) {
                    qWarning() << "[Settings] Flat-scene correction failed:" << error;
                    app->showToast("FLAT-SCENE CORRECTION FAILED", ToastLevel::Error);
                    return;
                }
                app->showToast("FLAT-SCENE CORRECTION TRIGGERED", ToastLevel::Success);
            },
            ModalLevel::Critical,
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
    if (item.id == SettingID::RestoreDefaults) {
        if (!app) return;
        app->showTextModal(
            QStringLiteral("RESTORE DEFAULTS?"),
            QStringLiteral("All settings will be reset.\nMedia & calibration unchanged."),
            [this, app]() {
                m_applyInFlight = true;
                const SettingsService::ApplyResult result =
                    SettingsService::instance().restoreDefaults();
                if (result.code == SettingsService::ApplyCode::Ok ||
                    result.code == SettingsService::ApplyCode::NoChange) {
                    app->showToast("SETTINGS RESTORED", ToastLevel::Success);
                }
            },
            ModalLevel::Critical,
            TextModalSize::Large);
        return;
    }
    if (item.id == SettingID::Palette) {
        emit EventBus::instance().cameraRequested(QRect(), TransitionMode::Instant);
        emit EventBus::instance().paletteSelectorRequested(true);
        return;
    }
    if (item.id == SettingID::SdCardSafeEject || item.id == SettingID::UsbDiskSafeEject) {
        const bool sdCard = (item.id == SettingID::SdCardSafeEject);
        const QString targetName = sdCard ? QStringLiteral("SD Card")
                                          : QStringLiteral("USB Disk");
        if (!app) return;
        app->showTextModal(
            QStringLiteral("EJECT %1?").arg(targetName.toUpper()),
            QStringLiteral("Wait for confirmation before removing it."),
            [this, app, targetName, sdCard]() {
                auto* storage = HardwareManager::instance().storage();
                if (!storage) {
                    app->showToast("STORAGE UNAVAILABLE", ToastLevel::Error);
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
                    app->showToast(targetName + " eject failed", ToastLevel::Error);
                } else {
                    app->showToast(targetName + " can be removed", ToastLevel::Success);
                }

                refreshStorageRowsIfVisible();
            },
            ModalLevel::Normal,
            TextModalSize::Normal);
        return;
    }
    if (item.id == SettingID::SdCardFormat || item.id == SettingID::UsbDiskFormat) {
        const bool sdCard = (item.id == SettingID::SdCardFormat);
        const QString targetName = sdCard ? QStringLiteral("SD Card")
                                          : QStringLiteral("USB Disk");
        const StorageVolume targetVolume = sdCard ? StorageVolume::SdCard
                                                  : StorageVolume::UsbDisk;
        if (!app) return;
        auto* storage = HardwareManager::instance().storage();
        const QString fileSystem = storage ? storage->formatFileSystemName(targetVolume)
                                           : QString();
        const QString warningBody = fileSystem.isEmpty()
            ? QStringLiteral("All files will be deleted.")
            : QStringLiteral("All files will be deleted.\nIt will be formatted as %1.")
                  .arg(fileSystem);
        app->showWarningModal(QStringLiteral("FORMAT %1?").arg(targetName.toUpper()),
                              warningBody,
                              [this, app, targetName, targetVolume]() {
                               auto* storage = HardwareManager::instance().storage();
                               if (!storage) {
                                   app->showToast("STORAGE UNAVAILABLE", ToastLevel::Error);
                                   return;
                               }

                               QString error;
                               const bool ok = storage->formatVolume(targetVolume, &error);

                               if (!ok) {
                                   qWarning() << "[Settings] Format failed:" << targetName
                                              << "reason:" << error;
                                   app->showToast(targetName + " format failed", ToastLevel::Error);
                               } else {
                                   app->showToast(targetName + " formatted", ToastLevel::Success);
                               }

                                  refreshStorageRowsIfVisible();
                              });
        return;
    }
    if (item.id == SettingID::InternalStorageCapacity ||
        item.id == SettingID::SdCardCapacity ||
        item.id == SettingID::UsbDiskCapacity) {
        return;
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

void SettingsView::rebuildSecondaryRows(int primaryIndex) {
    cancelActiveRowPress();
    for (auto* row : m_secondaryRows) delete row;
    m_secondaryRows.clear();

    auto* storage = HardwareManager::instance().storage();
    const bool sdCardReady = storage && storage->isSdCardReady();
    const bool usbDiskReady = storage && storage->isUsbDiskReady();
    const auto items = SettingsCatalog::visibleItems(primaryIndex,
                                                      SettingsStore::instance().current(),
                                                      sdCardReady,
                                                      usbDiskReady);
    for (const auto& item : items) {
        auto* row = new SettingsSecondaryRow(this);
        row->setData(item);
        connect(row, &SettingsSecondaryRow::activated, this, &SettingsView::onSecondaryRowActivated);
        m_secondaryRows.append(row);
    }

    refreshSecondaryRowsFromStore();
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
    raiseVisibleBubbles();
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
    m_topBar->setTitle(QStringLiteral("Settings"));
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

    rebuildSecondaryRows(primaryIndex);
    m_rightScroll = 0.0;
    refreshTopMask();
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

void SettingsView::refreshSecondaryRowsFromSnapshot(const SettingsSnapshot& snapshot) {
    StorageVolumeStatus sdStatus;
    StorageVolumeStatus usbStatus;
    StorageVolumeStatus nandStatus;
    if (auto* storage = HardwareManager::instance().storage()) {
        sdStatus = storage->volumeStatus(StorageVolume::SdCard);
        usbStatus = storage->volumeStatus(StorageVolume::UsbDisk);
        nandStatus = storage->volumeStatus(StorageVolume::Nand);
    }

    for (auto* row : m_secondaryRows) {
        const SecondaryItemData item = row->data();
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

void SettingsView::refreshSecondaryRowsFromStore() {
    refreshSecondaryRowsFromSnapshot(SettingsStore::instance().current());
}

void SettingsView::refreshStorageRowsIfVisible() {
    if (m_mode != PanelMode::Expanded ||
        !SettingsCatalog::sectionVisibilityAffectedByStorageState(m_activePrimary)) {
        return;
    }
    rebuildSecondaryRows(m_activePrimary);
    m_rightScroll = qBound<qreal>(0.0, m_rightScroll, rightMaxScroll());
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
        return;
    }

    auto* app = qobject_cast<App*>(window());
    if (!app) return;

    if (result.code == SettingsService::ApplyCode::RuntimeApplyFailed && result.persisted) {
        app->showToast("APPLY DEFERRED", ToastLevel::Warning);
        return;
    }

    app->showToast("SET FAILED", ToastLevel::Error);
}
