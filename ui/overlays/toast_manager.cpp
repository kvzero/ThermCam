#include "toast_manager.h"

#include <QApplication>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QTimer>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace {
constexpr qreal kToastWidthRatio = 0.72;
constexpr qreal kTextSizeRatio = 0.45;
constexpr qreal kHorizontalPaddingRatio = 0.4;
constexpr qreal kIconSizeRatio = 0.55;
constexpr qreal kIconGapRatio = 0.4;

QColor accentColor(ToastLevel level) {
    switch (level) {
    case ToastLevel::Success:
        return QColor("#41D7A1");
    case ToastLevel::Warning:
        return QColor("#FFB454");
    case ToastLevel::Error:
        return QColor("#FF6670");
    case ToastLevel::Info:
    default:
        return QColor("#5EB8FF");
    }
}

int toastBaselineHeight(int screenHeight) {
    return qMax(qRound(92.0), qRound(screenHeight * 0.20));
}

int contentMetricHeight(int screenHeight) {
    return qBound(qRound(56.0), qRound(screenHeight * 0.125), qRound(72.0));
}

void drawToastIcon(QPainter& painter, const QRectF& rect, ToastLevel level, const QColor& accent) {
    const qreal stroke = qMax<qreal>(1.8, rect.width() * 0.105);
    painter.setPen(QPen(accent, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    if (level == ToastLevel::Warning) {
        QPainterPath triangle;
        triangle.moveTo(rect.center().x(), rect.top());
        triangle.lineTo(rect.right(), rect.bottom());
        triangle.lineTo(rect.left(), rect.bottom());
        triangle.closeSubpath();
        painter.drawPath(triangle);
        painter.drawLine(QPointF(rect.center().x(), rect.top() + rect.height() * 0.35),
                         QPointF(rect.center().x(), rect.top() + rect.height() * 0.62));
        painter.drawPoint(QPointF(rect.center().x(), rect.top() + rect.height() * 0.78));
        return;
    }

    painter.drawEllipse(rect);
    const QPointF center = rect.center();
    if (level == ToastLevel::Success) {
        QPainterPath check;
        check.moveTo(rect.left() + rect.width() * 0.24, center.y());
        check.lineTo(rect.left() + rect.width() * 0.43, rect.bottom() - rect.height() * 0.25);
        check.lineTo(rect.right() - rect.width() * 0.20, rect.top() + rect.height() * 0.25);
        painter.drawPath(check);
    } else if (level == ToastLevel::Error) {
        painter.drawLine(QPointF(rect.left() + rect.width() * 0.29, rect.top() + rect.height() * 0.29),
                         QPointF(rect.right() - rect.width() * 0.29, rect.bottom() - rect.height() * 0.29));
        painter.drawLine(QPointF(rect.right() - rect.width() * 0.29, rect.top() + rect.height() * 0.29),
                         QPointF(rect.left() + rect.width() * 0.29, rect.bottom() - rect.height() * 0.29));
    } else {
        painter.drawLine(QPointF(center.x(), rect.top() + rect.height() * 0.43),
                         QPointF(center.x(), rect.bottom() - rect.height() * 0.23));
        painter.drawPoint(QPointF(center.x(), rect.top() + rect.height() * 0.26));
    }
}

class ToastCard final : public QWidget {
public:
    ToastCard(const QString& message, ToastLevel level, bool progress, QWidget* parent)
        : QWidget(parent), m_message(message), m_level(level), m_progress(progress) {
        setAttribute(Qt::WA_TranslucentBackground);
    }

    int preferredHeight(int width, int screenHeight) const {
        // Card height may grow for wrapped text, while content metrics stay fixed so
        // every toast level keeps the same typography, icon size, and horizontal grid.
        const int baselineHeight = toastBaselineHeight(screenHeight);
        const int metricHeight = contentMetricHeight(screenHeight);
        const int horizontalPadding = qRound(metricHeight * kHorizontalPaddingRatio);
        const int iconSize = qRound(metricHeight * kIconSizeRatio);
        const int iconGap = qRound(metricHeight * kIconGapRatio);
        const int textWidth = width - horizontalPadding * 2 - iconSize - iconGap;

        QFont font("Roboto");
        font.setWeight(QFont::DemiBold);
        font.setPixelSize(qRound(metricHeight * kTextSizeRatio));
        const QFontMetrics metrics(font);
        const int textHeight = metrics.boundingRect(QRect(0, 0, textWidth, 1000),
                                                    Qt::TextWordWrap, m_message).height();
        return qMax(baselineHeight, textHeight + qRound(metricHeight * 0.70));
    }

    void setVisualState(qreal opacity, qreal scale) {
        if (qFuzzyCompare(m_opacity, opacity) && qFuzzyCompare(m_scale, scale)) return;
        m_opacity = opacity;
        m_scale = scale;
        update();
    }

    void setProgressPhase(qreal phase) {
        if (qFuzzyCompare(m_progressPhase, phase)) return;
        m_progressPhase = phase;
        update();
    }

    void setProgressValue(int percent) {
        m_determinateProgress = true;
        m_progressValue = qBound(0, percent, 100) / 100.0;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (width() <= 0 || height() <= 0 || m_opacity <= 0.0) return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(m_opacity);

        const QRectF cardRect = QRectF(rect()).adjusted(2.0, 2.0, -2.0, -2.0);
        painter.translate(cardRect.center());
        painter.scale(m_scale, m_scale);
        painter.translate(-cardRect.center());

        const qreal cornerRadius = cardRect.width() * 0.05;
        const QColor accent = accentColor(m_level);

        QPainterPath cardPath;
        cardPath.addRoundedRect(cardRect, cornerRadius, cornerRadius);

        QLinearGradient glass(cardRect.topLeft(), cardRect.bottomLeft());
        glass.setColorAt(0.0, QColor(31, 40, 50, 238));
        glass.setColorAt(1.0, QColor(8, 13, 19, 236));
        painter.fillPath(cardPath, glass);

        QLinearGradient tint(cardRect.topLeft(), QPointF(cardRect.right(), cardRect.top()));
        QColor tintStart = accent;
        tintStart.setAlpha(36);
        tint.setColorAt(0.0, tintStart);
        tint.setColorAt(0.62, QColor(255, 255, 255, 0));
        painter.fillPath(cardPath, tint);

        QColor border = accent;
        border.setAlpha(135);
        painter.setPen(QPen(border, 1.6));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(cardPath);

        const int metricHeight = contentMetricHeight(parentWidget() ? parentWidget()->height() : height());
        const int horizontalPadding = qRound(metricHeight * kHorizontalPaddingRatio);
        const int iconSize = qRound(metricHeight * kIconSizeRatio);
        const int iconGap = qRound(metricHeight * kIconGapRatio);
        const QRectF iconRect(cardRect.left() + horizontalPadding,
                              cardRect.center().y() - iconSize / 2.0,
                              iconSize, iconSize);
        drawToastIcon(painter, iconRect, m_level, accent);

        QFont font("Roboto");
        font.setWeight(QFont::DemiBold);
        font.setPixelSize(qRound(metricHeight * kTextSizeRatio));
        painter.setFont(font);
        painter.setPen(QColor(246, 249, 252));
        const QRectF textRect(iconRect.right() + iconGap, cardRect.top(),
                              cardRect.right() - horizontalPadding - iconRect.right() - iconGap,
                              cardRect.height());
        painter.drawText(textRect.toRect(), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                         m_message);

        if (m_progress) {
            const qreal trackHeight = qMax<qreal>(3.0, metricHeight * 0.07);
            const QRectF track(cardRect.left() + horizontalPadding,
                               cardRect.bottom() - trackHeight - 5.0,
                               cardRect.width() - horizontalPadding * 2,
                               trackHeight);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 35));
            painter.drawRoundedRect(track, trackHeight / 2.0, trackHeight / 2.0);

            painter.save();
            painter.setClipRect(track);
            painter.setBrush(accent);
            if (m_determinateProgress) {
                const qreal fillWidth = track.width() * m_progressValue;
                if (fillWidth > 0.0) {
                    painter.drawRoundedRect(
                        QRectF(track.left(), track.top(), fillWidth, track.height()),
                        trackHeight / 2.0,
                        trackHeight / 2.0);
                }
            } else {
                const qreal segmentWidth = track.width() * 0.32;
                const qreal travel = track.width() + segmentWidth;
                const qreal segmentX = track.left() - segmentWidth + travel * m_progressPhase;
                painter.drawRoundedRect(
                    QRectF(segmentX, track.top(), segmentWidth, track.height()),
                    trackHeight / 2.0,
                    trackHeight / 2.0);
            }
            painter.restore();
        }
    }

private:
    QString m_message;
    ToastLevel m_level = ToastLevel::Info;
    bool m_progress = false;
    bool m_determinateProgress = false;
    qreal m_progressPhase = 0.0;
    qreal m_progressValue = 0.0;
    qreal m_opacity = 0.0;
    qreal m_scale = 0.8;
};
} // namespace

struct ToastManager::ToastEntry {
    ToastCard* card = nullptr;
    // Layout space, visual transform, and physical drag are intentionally independent:
    // only heightProgress reflows neighbouring entries.
    qreal heightProgress = 0.0;
    qreal visualProgress = 0.0;
    qreal dragOffsetY = 0.0;
    bool isDismissing = false;
    bool isProgress = false;
    QVariantAnimation* heightAnimation = nullptr;
    QVariantAnimation* visualAnimation = nullptr;
    QVariantAnimation* dragAnimation = nullptr;
    QVariantAnimation* progressAnimation = nullptr;
    QTimer* dismissTimer = nullptr;
};

ToastManager::ToastManager(QWidget* parent) : QWidget(parent) {
    hide();
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_TranslucentBackground);
    // The transparent overlay never consumes normal input. The application filter
    // below captures only presses that begin on a visible toast card.
    qApp->installEventFilter(this);
}

ToastManager::~ToastManager() {
    qApp->removeEventFilter(this);
    qDeleteAll(m_entries);
}

void ToastManager::showToast(const QString& msg, ToastLevel level) {
    createEntry(msg, level, false);
}

void ToastManager::showProgressToast(const QString& msg) {
    Q_ASSERT(!m_progressEntry);
    m_progressEntry = createEntry(msg, ToastLevel::Info, true);
}

void ToastManager::updateProgressToast(int percent) {
    if (!m_progressEntry || m_progressEntry->isDismissing) return;
    if (m_progressEntry->progressAnimation) {
        m_progressEntry->progressAnimation->stop();
    }
    m_progressEntry->card->setProgressValue(percent);
}

void ToastManager::finishProgressToast(const QString& msg,
                                       ToastLevel level) {
    Q_ASSERT(m_progressEntry);
    if (!m_progressEntry) return;
    ToastEntry* const progressEntry = m_progressEntry;
    m_progressEntry = nullptr;
    dismissEntry(progressEntry);
    QTimer::singleShot(220, this, [this, msg, level]() { showToast(msg, level); });
}

ToastManager::ToastEntry* ToastManager::createEntry(const QString& msg,
                                                    ToastLevel level,
                                                    bool progress) {
    if (msg.isEmpty()) return nullptr;

    while (activeEntryCount() >= 3) {
        auto it = std::find_if(m_entries.cbegin(), m_entries.cend(),
                               [](const ToastEntry* entry) {
                                   return !entry->isDismissing && !entry->isProgress;
                               });
        if (it == m_entries.cend()) break;
        dismissEntry(*it);
    }

    auto* entry = new ToastEntry;
    entry->isProgress = progress;
    entry->card = new ToastCard(msg, level, progress, this);

    entry->heightAnimation = new QVariantAnimation(this);
    entry->heightAnimation->setDuration(200);
    entry->heightAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(entry->heightAnimation, &QVariantAnimation::valueChanged, this,
            [this, entry](const QVariant& value) {
                if (!m_entries.contains(entry)) return;
                entry->heightProgress = value.toReal();
                relayoutEntries();
            });

    entry->visualAnimation = new QVariantAnimation(this);
    entry->visualAnimation->setDuration(100);
    entry->visualAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(entry->visualAnimation, &QVariantAnimation::valueChanged, this,
            [this, entry](const QVariant& value) {
                if (!m_entries.contains(entry)) return;
                entry->visualProgress = value.toReal();
                relayoutEntries();
            });

    entry->dragAnimation = new QVariantAnimation(this);
    entry->dragAnimation->setDuration(280);
    entry->dragAnimation->setEasingCurve(QEasingCurve::OutBack);
    connect(entry->dragAnimation, &QVariantAnimation::valueChanged, this,
            [this, entry](const QVariant& value) {
                if (!m_entries.contains(entry)) return;
                entry->dragOffsetY = value.toReal();
                relayoutEntries();
            });

    if (progress) {
        entry->progressAnimation = new QVariantAnimation(this);
        entry->progressAnimation->setDuration(1100);
        entry->progressAnimation->setStartValue(0.0);
        entry->progressAnimation->setEndValue(1.0);
        entry->progressAnimation->setLoopCount(-1);
        connect(entry->progressAnimation, &QVariantAnimation::valueChanged,
                entry->card, [card = entry->card](const QVariant& value) {
                    card->setProgressPhase(value.toReal());
                });
    }

    entry->dismissTimer = new QTimer(this);
    entry->dismissTimer->setSingleShot(true);
    connect(entry->dismissTimer, &QTimer::timeout, this,
            [this, entry] { dismissEntry(entry); });

    m_entries.append(entry);
    show();
    raise();
    startEntry(entry);
    return entry;
}

void ToastManager::startEntry(ToastEntry* entry) {
    entry->heightAnimation->setStartValue(0.0);
    entry->heightAnimation->setEndValue(1.0);
    entry->heightAnimation->start();

    entry->visualAnimation->setStartValue(0.0);
    entry->visualAnimation->setEndValue(1.0);
    entry->visualAnimation->start();
    if (entry->isProgress) {
        entry->progressAnimation->start();
    } else {
        entry->dismissTimer->start(3000);
    }
}

void ToastManager::dismissEntry(ToastEntry* entry) {
    if (!entry || entry->isDismissing || !m_entries.contains(entry)) return;

    entry->isDismissing = true;
    entry->dismissTimer->stop();
    entry->dragAnimation->stop();
    if (entry->progressAnimation) entry->progressAnimation->stop();

    entry->heightAnimation->stop();
    entry->heightAnimation->setStartValue(entry->heightProgress);
    entry->heightAnimation->setEndValue(0.0);
    entry->heightAnimation->start();

    entry->visualAnimation->stop();
    entry->visualAnimation->setStartValue(entry->visualProgress);
    entry->visualAnimation->setEndValue(0.0);
    entry->visualAnimation->start();

    QTimer::singleShot(400, this, [this, entry] { removeEntry(entry); });
}

void ToastManager::removeEntry(ToastEntry* entry) {
    if (!entry || !m_entries.removeOne(entry)) return;

    if (m_progressEntry == entry) {
        m_progressEntry = nullptr;
    }
    if (m_dragEntry == entry) {
        m_dragEntry = nullptr;
    }

    entry->heightAnimation->stop();
    entry->visualAnimation->stop();
    entry->dragAnimation->stop();
    if (entry->progressAnimation) entry->progressAnimation->stop();
    entry->dismissTimer->stop();
    entry->heightAnimation->deleteLater();
    entry->visualAnimation->deleteLater();
    entry->dragAnimation->deleteLater();
    if (entry->progressAnimation) entry->progressAnimation->deleteLater();
    entry->dismissTimer->deleteLater();
    entry->card->deleteLater();
    delete entry;

    if (m_entries.isEmpty()) {
        hide();
    } else {
        relayoutEntries();
    }
}

void ToastManager::relayoutEntries() {
    const int screenHeight = parentWidget() ? parentWidget()->height() : height();
    const int toastWidth = qRound(width() * kToastWidthRatio);
    const int toastX = (width() - toastWidth) / 2;
    int y = qRound(screenHeight * 0.0275);

    for (ToastEntry* entry : m_entries) {
        const int cardHeight = entry->card->preferredHeight(toastWidth, screenHeight);
        const int slotHeight = qRound(cardHeight * entry->heightProgress);
        const qreal enterOffset = entry->isDismissing ? 0.0
                                                      : -16.0 * (1.0 - entry->visualProgress);
        const qreal visualScale = 0.8 + entry->visualProgress * 0.2;

        entry->card->setGeometry(toastX, y + qRound(enterOffset + entry->dragOffsetY),
                                 toastWidth, cardHeight);
        entry->card->setVisualState(entry->visualProgress, visualScale);
        entry->card->setVisible(entry->visualProgress > 0.0);

        y += slotHeight + qRound(8.0 * entry->heightProgress);
    }
}

ToastManager::ToastEntry* ToastManager::entryAt(const QPoint& globalPosition) const {
    for (auto it = m_entries.crbegin(); it != m_entries.crend(); ++it) {
        ToastEntry* entry = *it;
        if (entry->isDismissing || !entry->card->isVisible()) continue;

        const QRect cardRect(entry->card->mapToGlobal(QPoint(0, 0)), entry->card->size());
        if (cardRect.contains(globalPosition)) return entry;
    }
    return nullptr;
}

void ToastManager::beginDrag(ToastEntry* entry, int globalY) {
    m_dragEntry = entry;
    m_dragStartGlobalY = globalY;
    m_dragStartOffsetY = entry->dragOffsetY;
    entry->dismissTimer->stop();
    entry->dragAnimation->stop();
    entry->card->raise();
}

void ToastManager::updateDrag(int globalY) {
    if (!m_dragEntry) return;

    const int physicalDeltaY = globalY - m_dragStartGlobalY;
    if (physicalDeltaY < 0) {
        m_dragEntry->dragOffsetY = m_dragStartOffsetY + physicalDeltaY;
    } else {
        const qreal dampedDelta = 3.0 * std::sqrt(qreal(physicalDeltaY));
        m_dragEntry->dragOffsetY = m_dragStartOffsetY + dampedDelta;
    }
    relayoutEntries();
}

void ToastManager::finishDrag() {
    if (!m_dragEntry) return;

    ToastEntry* entry = m_dragEntry;
    m_dragEntry = nullptr;

    const int screenHeight = parentWidget() ? parentWidget()->height() : height();
    const int toastWidth = qRound(width() * kToastWidthRatio);
    const int cardHeight = entry->card->preferredHeight(toastWidth, screenHeight);
    if (!entry->isProgress &&
        entry->dragOffsetY - m_dragStartOffsetY < -cardHeight * 0.40) {
        dismissEntry(entry);
        return;
    }

    entry->dragAnimation->setStartValue(entry->dragOffsetY);
    entry->dragAnimation->setEndValue(0.0);
    entry->dragAnimation->start();
    if (!entry->isProgress) {
        entry->dismissTimer->start(2000);
    }
}

int ToastManager::activeEntryCount() const {
    return std::count_if(m_entries.cbegin(), m_entries.cend(),
                         [](const ToastEntry* entry) { return !entry->isDismissing; });
}

bool ToastManager::eventFilter(QObject*, QEvent* event) {
    if (!isVisible()) return false;

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton) return false;

        ToastEntry* entry = entryAt(mouse->globalPos());
        if (!entry) return false;
        beginDrag(entry, mouse->globalPos().y());
        return true;
    }

    if (event->type() == QEvent::MouseMove && m_dragEntry) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (!(mouse->buttons() & Qt::LeftButton)) return false;
        updateDrag(mouse->globalPos().y());
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease && m_dragEntry) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton) return false;
        finishDrag();
        return true;
    }

    return false;
}

void ToastManager::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    relayoutEntries();
}
