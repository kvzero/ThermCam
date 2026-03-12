#include "video_controlbar.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEasingCurve>
#include <algorithm>

VideoControlBar::VideoControlBar(QWidget* parent) : QWidget(parent) {
    setProperty("isInteractable", true);

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, &VideoControlBar::initiateFadeOut);

    m_fadeAnim = new QPropertyAnimation(this, "opacity", this);
    m_fadeAnim->setDuration(m_cfg.FADE_DURATION_MS);
    m_fadeAnim->setEasingCurve(QEasingCurve::InOutQuad);

    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this](){
        if (m_opacity <= 0.01) this->hide();
    });
}

void VideoControlBar::show() {
    QWidget::show();
    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(m_opacity);
    m_fadeAnim->setEndValue(1.0);
    m_fadeAnim->start();
    m_hideTimer->start(m_cfg.FADE_DELAY_MS);
}

void VideoControlBar::hideImmediate() {
    m_hideTimer->stop();
    m_fadeAnim->stop();
    setOpacity(0.0);
    this->hide();
}

void VideoControlBar::initiateFadeOut() {
    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(m_opacity);
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->start();
}

void VideoControlBar::updatePlaybackState(bool playing, qint64 currentMs, qint64 totalMs, const QString& durationStr) {
    if (m_isScrubbing) return;
    m_isPlaying = playing;
    m_currentMs = currentMs;
    m_totalMs = totalMs;
    m_durationStr = durationStr;
    update();
}

QString VideoControlBar::formatTimeMs(qint64 ms, bool forceHours) const {
    qint64 totalSecs = ms / 1000;
    qint64 h = totalSecs / 3600;
    qint64 m = (totalSecs % 3600) / 60;
    qint64 s = totalSecs % 60;
    if (h > 0 || forceHours) return QString::asprintf("%02lld:%02lld:%02lld", h, m, s);
    return QString::asprintf("%02lld:%02lld", m, s);
}

void VideoControlBar::mousePressEvent(QMouseEvent* event) {
    handleInteractionUpdate(event->pos());
    QWidget::mousePressEvent(event);
}

bool VideoControlBar::handleInteractionUpdate(QPoint localPos) {
    if (m_opacity < 0.1) return false;

    QRectF expandedPlay = m_playHitbox.adjusted(-m_cfg.HIT_EXPANSION_PX, -m_cfg.HIT_EXPANSION_PX,
                                                 m_cfg.HIT_EXPANSION_PX, m_cfg.HIT_EXPANSION_PX);
    m_hoverPlay = expandedPlay.contains(localPos);

    if (m_scrubHitbox.contains(localPos)) {
        if (!m_isScrubbing) {
            m_isScrubbing = true;
            emit scrubbingStateChanged(true);
        }
        if (m_totalMs > 0 && m_scrubHitbox.width() > 0) {
            qreal ratio = std::clamp((localPos.x() - m_scrubHitbox.left()) / m_scrubHitbox.width(), 0.0, 1.0);
            m_scrubTargetMs = static_cast<qint64>(ratio * m_totalMs);
            emit seekRequested(m_scrubTargetMs);
        }
    }

    update();

    bool interacting = m_hoverPlay || m_isScrubbing || m_scrubHitbox.contains(localPos);
    if (interacting) {
        m_hideTimer->start(m_cfg.FADE_DELAY_MS);
        emit interactionActive();
        return true;
    }
    return false;
}

void VideoControlBar::finalizeGesture(int) {
    if (m_hoverPlay) {
        emit togglePlayRequested();
    }

    m_hoverPlay = false;

    if (m_isScrubbing) {
        m_isScrubbing = false;
        m_scrubTargetMs = -1;
        emit scrubbingStateChanged(false);
    }
    update();
}

void VideoControlBar::paintEvent(QPaintEvent*) {
    if (m_opacity <= 0.01) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(m_opacity);

    // 1. Foundation: Calculate Bar Dimensions
    const qreal w = width();
    const qreal barH = height();

    QRectF barRect(0, 0, w, barH);
    QLinearGradient grad(barRect.topLeft(), barRect.bottomLeft());
    grad.setColorAt(0.0, Qt::transparent);
    grad.setColorAt(1.0, m_cfg.BG_GRADIENT_END);
    p.fillRect(barRect, grad);

    // Extract basic ratios for calculation
    qreal margin   = barH * m_cfg.SIDE_MARGIN_RATIO;
    qreal spacing  = barH * m_cfg.ITEM_SPACING_RATIO;
    qreal iconSize = barH * m_cfg.PLAY_ICON_SIZE_RATIO;

    // 2. Playback Action: Centered Button with Configurable Offset
    qreal btnCenterY = barRect.center().y() + (barH * m_cfg.PLAY_BTN_V_OFFSET);
    m_playHitbox = QRectF(barRect.left() + margin, btnCenterY - iconSize / 2.0, iconSize, iconSize);

    p.setPen(m_cfg.TEXT_COLOR);
    QFont iconFont("tabler-icons");
    iconFont.setPixelSize(qRound(iconSize));
    p.setFont(iconFont);
    p.drawText(m_playHitbox, Qt::AlignCenter, m_isPlaying ? m_cfg.ICON_PAUSE : m_cfg.ICON_PLAY);

    // 3. Right Section: Orchestrate the Scrubber (Time + Track)
    qreal rightX = m_playHitbox.right() + spacing;
    qreal rightW = (barRect.right() - margin) - rightX;

    // Intent: Define a massive monolithic hit-zone for easy scrubbing
    m_scrubHitbox = QRectF(rightX, barRect.top(), rightW, barRect.height());

    // --- Scrubber Tier 1: Time Metadata ---
    qreal textH = barH * m_cfg.TEXT_TIER_H_RATIO;
    qreal textY = barRect.top() + (barH * m_cfg.CONTENT_V_START_RATIO);
    QRectF textArea(rightX, textY, rightW, textH);

    QFont textFont("Roboto");
    textFont.setPixelSize(qRound(textH * m_cfg.TIME_FONT_SCALE));
    textFont.setBold(true);
    p.setFont(textFont);

    bool forceHours = m_totalMs >= 3600000;
    qint64 displayMs = m_isScrubbing ? m_scrubTargetMs : m_currentMs;
    p.drawText(textArea, Qt::AlignLeft | Qt::AlignBottom, formatTimeMs(displayMs, forceHours));
    p.drawText(textArea, Qt::AlignRight | Qt::AlignBottom, formatTimeMs(m_totalMs, forceHours));

    // --- Scrubber Tier 2: Progress Track ---
    qreal trackH = barH * m_cfg.TRACK_HEIGHT_RATIO;
    qreal trackY = textArea.bottom() + (barH * m_cfg.TRACK_V_GAP_RATIO);
    m_trackHitbox = QRectF(rightX, trackY, rightW, trackH);

    // Draw Background Track
    p.setBrush(m_cfg.TRACK_BG_COLOR);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(m_trackHitbox, trackH / 2.0, trackH / 2.0);

    // Draw Active Progress Fill
    if (m_totalMs > 0) {
        qreal progress = static_cast<qreal>(displayMs) / m_totalMs;
        QRectF trackFill = m_trackHitbox;
        trackFill.setWidth(m_trackHitbox.width() * progress);

        p.setBrush(m_cfg.TEXT_COLOR);
        p.drawRoundedRect(trackFill, trackH / 2.0, trackH / 2.0);

        // Intent: Render a physical drag handle ONLY when actively interacting
        if (m_isScrubbing) {
            qreal handleSize = trackH * m_cfg.SCRUB_HANDLE_RATIO;
            p.drawEllipse(trackFill.right() - handleSize / 2.0,
                          trackFill.center().y() - handleSize / 2.0,
                          handleSize, handleSize);
        }
    }
}
