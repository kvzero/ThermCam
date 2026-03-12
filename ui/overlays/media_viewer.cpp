#include "media_viewer.h"
#include "services/gallery_service.h"
#include "core/event_bus.h"
#include <QEasingCurve>
#include <algorithm>
#include <QPainterPath>

MediaViewer::MediaViewer(QObject* parent) : QObject(parent) {
    m_topBar = new ViewerTopBar(qobject_cast<QWidget*>(parent));
    m_videoBar = new VideoControlBar(qobject_cast<QWidget*>(parent));

    // --- TopBar Connections ---
    connect(m_topBar, &ViewerTopBar::backRequested, [this]() {
        emit dismissRequested();
    });
    connect(m_topBar, &ViewerTopBar::deleteRequested, [this]() {
        emit deleteRequested(m_currentIndex);
    });

    // --- VideoBar Connections ---
    connect(m_videoBar, &VideoControlBar::togglePlayRequested, this, [this]() {
        if (m_player->state() == VideoPlayer::State::Playing) m_player->pause();
        else m_player->play();
        // Immediate UI feedback before engine async callback
        syncVideoBarState();
    });

    /* Intent: Pause video while dragging, resume (if playing before) on release */
    connect(m_videoBar, &VideoControlBar::scrubbingStateChanged, this, [this](bool isScrubbing){
        if (isScrubbing) {
            m_wasPlayingBeforeScrub = (m_player->state() == VideoPlayer::State::Playing);
            m_player->pause();
        } else {
            if (m_wasPlayingBeforeScrub) m_player->play();
        }
        syncVideoBarState();
    });

    connect(m_videoBar, &VideoControlBar::seekRequested, this, [this](qint64 targetMs) {
        m_player->seek(targetMs);
    });


    auto syncHudWakeup = [this]() {
        m_topBar->show();
        MediaFileInfo info = GalleryService::instance().getMediaInfo(m_currentIndex);
        if (info.type == CaptureMode::Video) {
            m_videoBar->show();
        }
    };

    connect(m_topBar, &ViewerTopBar::interactionActive, this, syncHudWakeup);
    connect(m_videoBar, &VideoControlBar::interactionActive, this, syncHudWakeup);

    // --- Physics Engines ---
    m_morphAnim = new QPropertyAnimation(this, "morphProgress", this);
    m_morphAnim->setDuration(m_cfg.MORPH_DUR_MS);
    m_morphAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_morphAnim, &QPropertyAnimation::finished, this, &MediaViewer::onMorphFinished);

    m_slideAnim = new QPropertyAnimation(this, "slideOffset", this);
    m_slideAnim->setDuration(m_cfg.SNAP_DUR_MS);
    m_slideAnim->setEasingCurve(QEasingCurve::OutQuad);
    connect(m_slideAnim, &QPropertyAnimation::finished, this, &MediaViewer::onSlideFinished);

    // --- Video Engine ---
    m_player = new VideoPlayer(this);
    connect(m_player, &VideoPlayer::frameReady, this, &MediaViewer::onVideoFrameReady);
    connect(m_player, &VideoPlayer::positionChanged, this, &MediaViewer::onVideoPositionChanged);

    /* Intent: Ensure UI logic runs on Main Thread to avoid QTimer thread affinity issues */
    connect(m_player, &VideoPlayer::playbackFinished, this, [this](){
        syncVideoBarState();
        m_topBar->show(); // Auto-reveal controls on finish
        emit requestRedraw();
    });

    connect(m_player, &VideoPlayer::errorOccurred, this, [this](const QString& msg) {
        emit EventBus::instance().toastRequested(msg, ToastLevel::Warning);
        syncVideoBarState();
    });
}

void MediaViewer::openAt(int index, const QRect& sourceGridRect, const QSize& screenSize) {
    m_currentIndex = index;
    m_gridAnchorRect = sourceGridRect;
    m_screenSize = screenSize;
    m_state = MorphingIn;
    m_hudIntendedVisible = true;

    const int barHeight = qRound(screenSize.height() * 0.15 * 1.5);
    m_topBar->setGeometry(0, 0, screenSize.width(), barHeight);
    m_topBar->raise();
    m_topBar->setVisible(true);

    const int videoBarHeight = qRound(screenSize.height() * 0.25);
        m_videoBar->setGeometry(0, screenSize.height() - videoBarHeight, screenSize.width(), videoBarHeight);
        m_videoBar->raise();
        m_videoBar->hideImmediate();

    // Reset Physics
    m_slideOffset = 0;
    m_dragOffsetY = 0;

    prefetchImages();

    MediaFileInfo info = GalleryService::instance().getMediaInfo(index);
    m_topBar->updateInfo(info);
    m_topBar->hideImmediate(); // Clean entry transition

    // Setup Video if applicable
    if (info.type == CaptureMode::Video) {
        m_currentVideoFrame = QImage();
        m_videoPositionMs = 0;
        m_player->open(info.filePath);
        syncVideoBarState();
    }

    m_morphAnim->stop();
    m_morphAnim->setStartValue(0.0);
    m_morphAnim->setEndValue(1.0);
    m_morphAnim->start();
}

void MediaViewer::closeTo(const QRect& targetGridRect) {
    forceStopPlayback();

    QRectF fullScreenRect(0, 0, m_screenSize.width(), m_screenSize.height());

    // Intent: Capture visual state at release point for smooth interpolation
    if (m_state == Dismissing) {
        qreal compressRatio = 1.0 - (m_dragOffsetY / m_screenSize.height());
        qreal contentScale = std::max(m_cfg.MIN_DISMISS_SCALE, compressRatio);

        qreal w = m_screenSize.width() * contentScale;
        qreal h = m_screenSize.height() * contentScale;
        qreal x = (m_screenSize.width() - w) / 2.0 + m_slideOffset;
        qreal y = m_dragOffsetY;

        m_dragReleaseRect = QRectF(x, y, w, h);
        m_morphStartBgAlpha = std::clamp(1.0 - (m_dragOffsetY / (m_screenSize.height() * m_cfg.BG_FADE_RESISTANCE)), 0.0, 1.0);
        m_morphStartRadius = (m_dragOffsetY / m_screenSize.height()) * (m_screenSize.width() * m_cfg.GRID_CORNER_RADIUS_RATIO);
    } else {
        m_dragReleaseRect = fullScreenRect;
        m_morphStartBgAlpha = 1.0;
        m_morphStartRadius = 0.0;
    }

    m_gridAnchorRect = targetGridRect.isEmpty() ?
                       QRect(m_screenSize.width()/2, m_screenSize.height()/2, 0, 0) : targetGridRect;

    m_state = MorphingOut;
    m_topBar->hideImmediate();
    m_videoBar->hideImmediate();

    m_morphAnim->stop();
    m_morphAnim->setStartValue(m_morphProgress);
    m_morphAnim->setEndValue(0.0);
    m_morphAnim->start();
}

void MediaViewer::forceStopPlayback() {
    if (m_player->state() != VideoPlayer::State::Stopped) {
        m_player->stop();
    }
    m_currentVideoFrame = QImage();
    m_videoPositionMs = 0;
}

void MediaViewer::onVideoFrameReady(const QImage& frame) {
    m_currentVideoFrame = frame;
    emit requestRedraw();
}

void MediaViewer::onVideoPositionChanged(qint64 posMs) {
    m_videoPositionMs = posMs;
    syncVideoBarState();
}

void MediaViewer::syncVideoBarState() {
    if (m_currentIndex < 0 || m_currentIndex >= GalleryService::instance().getMediaCount()) return;

    MediaFileInfo info = GalleryService::instance().getMediaInfo(m_currentIndex);
    if (info.type != CaptureMode::Video) return;

    m_videoBar->updatePlaybackState(
        m_player->state() == VideoPlayer::State::Playing,
        m_videoPositionMs,
        m_player->durationMs(),
        info.durationStr
    );
}

void MediaViewer::handleDeletion() {
    int total = GalleryService::instance().getMediaCount();
    if (total == 0) {
        closeTo(QRect());
        return;
    }

    bool slideFromLeft = (m_currentIndex >= total);
    if (m_currentIndex >= total) m_currentIndex = total - 1;

    prefetchImages();

    // Refresh info for the new item at index
    MediaFileInfo info = GalleryService::instance().getMediaInfo(m_currentIndex);
    m_topBar->updateInfo(info);
    m_topBar->hideImmediate();
    m_videoBar->hideImmediate();

    qreal stride = m_screenSize.width() * (1.0 + m_cfg.PAGE_GAP_RATIO);
    m_slideOffset = slideFromLeft ? -stride : stride;
    m_state = Deleting;

    m_slideAnim->stop();
    m_slideAnim->setDuration(m_cfg.SNAP_DUR_MS);
    m_slideAnim->setStartValue(m_slideOffset);
    m_slideAnim->setEndValue(0.0);
    m_slideAnim->start();

    emit requestRedraw();
}

void MediaViewer::prefetchImages() {
    auto& svc = GalleryService::instance();
    // 3-Frame window for smooth sliding
    svc.requestImage(m_currentIndex, m_screenSize);
    svc.requestImage(m_currentIndex - 1, m_screenSize);
    svc.requestImage(m_currentIndex + 1, m_screenSize);
}

void MediaViewer::onMorphFinished() {
    if (m_state == MorphingIn) {
        m_state = Idle;
        m_topBar->show();
        MediaFileInfo info = GalleryService::instance().getMediaInfo(m_currentIndex);
        if (info.type == CaptureMode::Video) m_videoBar->show();
    } else if (m_state == MorphingOut) {
        m_state = Hidden;
        m_topBar->hide();
        m_videoBar->hide();
        emit closedCompletely();
    }
}

void MediaViewer::onSlideFinished() {
    if (m_state == Paging || m_state == Deleting) {
        m_state = Idle;

        if (m_hudIntendedVisible) {
            m_topBar->show();
        }

        prefetchImages();
        emit requestSync(m_currentIndex);

        forceStopPlayback();
        MediaFileInfo info = GalleryService::instance().getMediaInfo(m_currentIndex);
        if (info.type == CaptureMode::Video) {
            m_player->open(info.filePath);
            syncVideoBarState();
            if (m_hudIntendedVisible) {
                m_videoBar->show();
            }
        } else {
            m_videoBar->hideImmediate();
        }
    }
}

// ============================================================================
// Interaction Routing & Arbitration
// ============================================================================

void MediaViewer::onTap(const QPoint& pos) {
    Q_UNUSED(pos);
    if (m_state != Idle) return;

    if (m_topBar->opacity() > 0.5) {
        m_hudIntendedVisible = false;
        m_topBar->hideImmediate();
        m_videoBar->hideImmediate();
    } else {
        m_hudIntendedVisible = true;
        m_topBar->show();
        MediaFileInfo info = GalleryService::instance().getMediaInfo(m_currentIndex);
        if (info.type == CaptureMode::Video) {
            syncVideoBarState();
            m_videoBar->show();
        }
    }
    emit requestRedraw();
}

void MediaViewer::onPanUpdate(const QPoint& currentPos, int deltaX, int deltaY) {
    Q_UNUSED(currentPos);
    if (m_state == MorphingIn || m_state == MorphingOut || m_state == Deleting) return;

    if (m_state == Idle) {
        m_state = (std::abs(deltaY) > std::abs(deltaX)) ? Dismissing : Paging;
        m_topBar->hideImmediate();
        m_videoBar->hideImmediate();

        if (m_state == Dismissing) {
            emit requestSync(m_currentIndex);
        }
    }

    if (m_state == Paging) {
        m_slideOffset += deltaX;
    } else if (m_state == Dismissing) {
        if (m_dragOffsetY + deltaY > 0) {
            m_dragOffsetY += deltaY;
            m_slideOffset += deltaX;
        }
    }
    emit requestRedraw();
}

void MediaViewer::onPanFinished(int vx, int /*vy*/) {
    if (m_state == Paging) {
        qreal targetOffset = 0.0;
        int maxItems = GalleryService::instance().getMediaCount();
        qreal stride = m_screenSize.width() * (1.0 + m_cfg.PAGE_GAP_RATIO);
        qreal offsetThreshold = m_screenSize.width() * m_cfg.SWIPE_OFFSET_THRESHOLD;

        if (m_slideOffset < -offsetThreshold || vx < -m_cfg.SWIPE_VELOCITY_THRESHOLD) {
            if (m_currentIndex < maxItems - 1) {
                targetOffset = -stride;
                m_currentIndex++;
            }
        } else if (m_slideOffset > offsetThreshold || vx > m_cfg.SWIPE_VELOCITY_THRESHOLD) {
            if (m_currentIndex > 0) {
                targetOffset = stride;
                m_currentIndex--;
            }
        }

        m_slideAnim->stop();
        m_slideAnim->setStartValue(m_slideOffset);
        m_slideAnim->setEndValue(targetOffset);
        m_slideAnim->start();

        if (targetOffset != 0.0) {
            m_slideOffset -= targetOffset;
            m_slideAnim->setStartValue(m_slideOffset);
            m_slideAnim->setEndValue(0.0);
        }

    } else if (m_state == Dismissing) {
        qreal exitThreshold = m_screenSize.height() * m_cfg.DISMISS_THRESHOLD_RATIO;
        if (m_dragOffsetY > exitThreshold) {
            emit dismissRequested();
        } else {
            m_dragOffsetY = 0;
            m_slideOffset = 0;
            m_state = Idle;

            if (m_hudIntendedVisible) {
                m_topBar->show();

                MediaFileInfo info = GalleryService::instance().getMediaInfo(m_currentIndex);
                if (info.type == CaptureMode::Video) {
                    m_videoBar->show();
                }
            }
            emit requestRedraw();
        }
    }
}

// ============================================================================
// Render Pipeline
// ============================================================================

void MediaViewer::paint(QPainter& p, const QRect& screen) {
    if (m_state == Hidden) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // 1. Occlusion Layer (Fade black background)
    qreal bgAlpha = 1.0;
    if (m_state == MorphingIn) bgAlpha = m_morphProgress;
    else if (m_state == MorphingOut) bgAlpha = m_morphStartBgAlpha * m_morphProgress;
    else if (m_state == Dismissing) bgAlpha = 1.0 - (m_dragOffsetY / (screen.height() * m_cfg.BG_FADE_RESISTANCE));

    bgAlpha = std::clamp(bgAlpha, 0.0, 1.0);
    p.fillRect(screen, QColor(0, 0, 0, qRound(255 * bgAlpha)));

    // 2. Geometry Calculation
    QRectF currentRect = screen;
    if (m_state == MorphingIn) {
        qreal w = m_gridAnchorRect.width() + (screen.width() - m_gridAnchorRect.width()) * m_morphProgress;
        qreal h = m_gridAnchorRect.height() + (screen.height() - m_gridAnchorRect.height()) * m_morphProgress;
        qreal x = m_gridAnchorRect.x() + (screen.x() - m_gridAnchorRect.x()) * m_morphProgress;
        qreal y = m_gridAnchorRect.y() + (screen.y() - m_gridAnchorRect.y()) * m_morphProgress;
        currentRect = QRectF(x, y, w, h);
    }
    else if (m_state == MorphingOut) {
        qreal w = m_gridAnchorRect.width() + (m_dragReleaseRect.width() - m_gridAnchorRect.width()) * m_morphProgress;
        qreal h = m_gridAnchorRect.height() + (m_dragReleaseRect.height() - m_gridAnchorRect.height()) * m_morphProgress;
        qreal x = m_gridAnchorRect.x() + (m_dragReleaseRect.x() - m_gridAnchorRect.x()) * m_morphProgress;
        qreal y = m_gridAnchorRect.y() + (m_dragReleaseRect.y() - m_gridAnchorRect.y()) * m_morphProgress;
        currentRect = QRectF(x, y, w, h);
    }
    else if (m_state == Dismissing) {
        qreal compressRatio = 1.0 - (m_dragOffsetY / screen.height());
        qreal contentScale = std::max(m_cfg.MIN_DISMISS_SCALE, compressRatio);
        qreal w = screen.width() * contentScale;
        qreal h = screen.height() * contentScale;
        qreal x = (screen.width() - w) / 2.0 + m_slideOffset;
        qreal y = m_dragOffsetY;
        currentRect = QRectF(x, y, w, h);
    }

    p.setClipRect(screen);

    // 3. Render Virtual Frame Buffer (Images or Live Video Frame)
    if (m_state == Paging || m_state == Idle) {
        qreal stride = screen.width() * (1.0 + m_cfg.PAGE_GAP_RATIO);

        drawImagePane(p, m_currentIndex - 1, currentRect.translated(-stride + m_slideOffset, 0), screen);

        // If Video is loaded and has a frame, render it over the static thumbnail
        if (!m_currentVideoFrame.isNull() && (m_state == Idle || m_state == Paging)) {
            QSizeF imgSize = m_currentVideoFrame.size();
            qreal scale = qMin(currentRect.width() / imgSize.width(), currentRect.height() / imgSize.height());
            qreal drawW = imgSize.width() * scale;
            qreal drawH = imgSize.height() * scale;
            QRectF drawRect(currentRect.x() + m_slideOffset + (currentRect.width() - drawW) / 2.0,
                            currentRect.y() + (currentRect.height() - drawH) / 2.0, drawW, drawH);
            p.drawImage(drawRect, m_currentVideoFrame);
        } else {
            drawImagePane(p, m_currentIndex, currentRect.translated(m_slideOffset, 0), screen);
        }

        drawImagePane(p, m_currentIndex + 1, currentRect.translated(stride + m_slideOffset, 0), screen);

    } else if (m_state == Deleting) {
        drawImagePane(p, m_currentIndex, currentRect.translated(m_slideOffset, 0), screen);

    } else {
        drawImagePane(p, m_currentIndex, currentRect, screen);
    }

    p.restore();
}

void MediaViewer::drawImagePane(QPainter& p, int index, const QRectF& baseRect, const QRect& screen) {
    if (index < 0 || index >= GalleryService::instance().getMediaCount()) return;

    QImage img = GalleryService::instance().requestImage(index, screen.size());
    if (img.isNull()) return;

    QSizeF imgSize = img.size();
    qreal scale = qMin(baseRect.width() / imgSize.width(), baseRect.height() / imgSize.height());

    qreal drawW = imgSize.width() * scale;
    qreal drawH = imgSize.height() * scale;
    qreal drawX = baseRect.x() + (baseRect.width() - drawW) / 2.0;
    qreal drawY = baseRect.y() + (baseRect.height() - drawH) / 2.0;
    QRectF drawRect(drawX, drawY, drawW, drawH);

    qreal cornerRadius = 0.0;
    qreal maxRadius = baseRect.width() * m_cfg.GRID_CORNER_RADIUS_RATIO;

    if (m_state == MorphingIn) cornerRadius = (1.0 - m_morphProgress) * maxRadius;
    else if (m_state == MorphingOut) cornerRadius = maxRadius + (m_morphStartRadius - maxRadius) * m_morphProgress;
    else if (m_state == Dismissing) cornerRadius = (m_dragOffsetY / screen.height()) * maxRadius;

    if (cornerRadius > 1.0) {
        QPainterPath path;
        path.addRoundedRect(drawRect, cornerRadius, cornerRadius);
        p.save();
        p.setClipPath(path);
        p.drawImage(drawRect, img);
        p.restore();
    } else {
        p.drawImage(drawRect, img);
    }
}
