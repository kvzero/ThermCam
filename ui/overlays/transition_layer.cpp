#include "transition_layer.h"
#include <QPainter>
#include <QPainterPath>
#include <QEasingCurve>

TransitionLayer::TransitionLayer(QWidget* parent) : QWidget(parent) {
    hide();

    // Protect underlying views from interaction during transition
    setAttribute(Qt::WA_TransparentForMouseEvents, false);

    m_animMorph = new QPropertyAnimation(this, "progress", this);

    m_animFade = new QPropertyAnimation(this, "layerOpacity", this);
    m_animFade->setDuration(m_cfg.FADE_DURATION);
    m_animFade->setEasingCurve(QEasingCurve::Linear);

    connect(m_animMorph, &QPropertyAnimation::finished, this, [this]() {
        if (m_morphCallback) m_morphCallback();
    });

    connect(m_animFade, &QPropertyAnimation::finished, this, [this]() {
        if (m_fadeCallback) m_fadeCallback();
        hide();

        // Critical Resource Cleanup
        m_snapshot = QImage();
        setLayerOpacity(1.0);
    });
}

void TransitionLayer::startMorph(const QRect& start, const QRect& end, bool expanding,
                                 const QString& icon, const QColor& color,
                                 const QImage& snapshot, std::function<void()> onComplete) {
    m_startRect = start;
    m_endRect = end;
    m_isExpanding = expanding;
    m_icon = icon;
    m_activeColor = color;
    m_snapshot = snapshot;
    m_morphCallback = onComplete;

    m_animMorph->setDuration(expanding ? m_cfg.EXPAND_DURATION : m_cfg.CONTRACT_DURATION);
    m_animMorph->setEasingCurve(expanding ? QEasingCurve::OutExpo : QEasingCurve::OutCubic);

    setLayerOpacity(1.0);
    show();
    raise();

    m_animMorph->stop();
    m_animMorph->setStartValue(0.0);
    m_animMorph->setEndValue(1.0);
    m_animMorph->start();
}

void TransitionLayer::startFadeOut(std::function<void()> onComplete) {
    m_fadeCallback = onComplete;
    m_animFade->stop();
    m_animFade->setStartValue(1.0);
    m_animFade->setEndValue(0.0);
    m_animFade->start();
}

void TransitionLayer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // 1. Core Geometry Interpolation
    qreal x = m_startRect.x() + (m_endRect.x() - m_startRect.x()) * m_progress;
    qreal y = m_startRect.y() + (m_endRect.y() - m_startRect.y()) * m_progress;
    qreal w = m_startRect.width() + (m_endRect.width() - m_startRect.width()) * m_progress;
    qreal h = m_startRect.height() + (m_endRect.height() - m_startRect.height()) * m_progress;
    QRectF currentRect(x, y, w, h);

    qreal startR = m_startRect.height() / 2.0;
    qreal endR   = m_isExpanding ? 0.0 : (m_endRect.height() / 2.0);
    if (!m_isExpanding) startR = 0.0;
    qreal currentRadius = startR + (endR - startR) * m_progress;

    QPainterPath path;
    path.addRoundedRect(currentRect, currentRadius, currentRadius);

    // 2. Render Layer: Background Mask
    // Strategy: Fades out during contraction to provide a "soft landing" into the semi-transparent UI.
    qreal bgAlpha = m_isExpanding
        ? std::min(1.0, m_progress * m_cfg.EXPANSION_ALPHA_RAMP)
        : (1.0 - m_progress);

    p.save();
    p.setOpacity(m_layerOpacity * bgAlpha);
    p.fillPath(path, m_activeColor);
    p.restore();

    // 3. Render Layer: Content Snapshot (Contracting only)
    bool isSnapshotVisible = false;
    if (!m_isExpanding && !m_snapshot.isNull()) {
        // Swap to icon once the content becomes too small to distinguish
        bool reachedSwapPoint = currentRect.width() < (m_endRect.width() * m_cfg.SNAPSHOT_SWAP_SCALE);

        if (!reachedSwapPoint) {
            isSnapshotVisible = true;
            p.save();
            // Start fading out the snapshot content towards the end of its visibility
            qreal snapshotAlpha = 1.0;
            if (m_progress > m_cfg.SNAPSHOT_FADE_START) {
                snapshotAlpha = 1.0 - (m_progress - m_cfg.SNAPSHOT_FADE_START) / (1.0 - m_cfg.SNAPSHOT_FADE_START);
            }
            p.setOpacity(m_layerOpacity * snapshotAlpha);
            p.setClipPath(path);
            p.drawImage(currentRect, m_snapshot);
            p.restore();
        }
    }

    // 4. Render Layer: Symbolic Icon
    if (!m_icon.isEmpty()) {
        p.save();

        qreal iconAlpha = 0.0;
        if (m_isExpanding) {
            // Fades out as the screen fills up with content
            iconAlpha = 1.0 - m_progress;
        } else {
            // Fades in quickly only AFTER the snapshot content has been removed
            if (!isSnapshotVisible) {
                // Linear ramp-up to full opacity for the landing landing
                iconAlpha = std::min(1.0, (m_progress - m_cfg.SNAPSHOT_FADE_START) * 2.5);
            }
        }

        p.setOpacity(m_layerOpacity * std::max(0.0, iconAlpha));
        p.setPen(Qt::white);

        QPointF center = currentRect.center();
        qreal baseSize = m_isExpanding ? m_startRect.height() : m_endRect.height();

        QFont iconFont("tabler-icons");
        iconFont.setPixelSize(qRound(baseSize * 0.55));
        p.setFont(iconFont);

        p.translate(center);
        qreal scale = m_isExpanding ? (1.0 + m_progress * m_cfg.ICON_GROW_FACTOR)
                                    : (m_cfg.ICON_SHRINK_START - m_progress * m_cfg.ICON_GROW_FACTOR);
        p.scale(scale, scale);
        p.translate(-center);

        p.drawText(QRectF(center.x() - baseSize, center.y() - baseSize, baseSize * 2, baseSize * 2),
                   Qt::AlignCenter, m_icon);
        p.restore();
    }
}
