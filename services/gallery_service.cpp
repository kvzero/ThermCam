#include "gallery_service.h"
#include "services/gallery_worker.h"
#include "hardware/storage/storage_manager.h"
#include "media/video_prober.h"
#include "core/global_context.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QFile>
#include <algorithm>
#include <QDebug>

GalleryService& GalleryService::instance() {
    static GalleryService inst;
    return inst;
}

GalleryService::GalleryService(QObject* parent) : QObject(parent) {
    m_thumbCache.setMaxCost(MAX_THUMBNAIL_CACHE_ITEMS);
    m_fullCache.setMaxCost(MAX_FULL_RES_CACHE_ITEMS);

    for (int i = 0; i < WORKER_COUNT; ++i) {
        m_workerThreads[i] = new QThread(this);
        m_workers[i] = new GalleryWorker();
        m_workers[i]->moveToThread(m_workerThreads[i]);

        connect(m_workers[i], &GalleryWorker::imageReady,
                this, &GalleryService::onWorkerImageReady);

        m_workerThreads[i]->start();
    }
}

GalleryService::~GalleryService() {
    for (int i = 0; i < WORKER_COUNT; ++i) {
        if (m_workerThreads[i]) {
            m_workerThreads[i]->quit();
            m_workerThreads[i]->wait();
        }
        delete m_workers[i];
    }
}

// ==========================================
// Memory Lifecycle (OOM Defense)
// ==========================================

void GalleryService::clearMemory() {
    m_thumbCache.clear();
    m_fullCache.clear();
    m_pendingRequests.clear();
    qInfo() << "[GalleryService] Absolute memory purge completed.";
}

void GalleryService::clearViewerCache() {
    m_fullCache.clear();
    // Allow pending requests for full images to complete and be discarded naturally
    qInfo() << "[GalleryService] Viewer cache purged.";
}

// ==========================================
// Directory Scanning & Sorting
// ==========================================

void GalleryService::scanDirectory() {
    QStringList dirs = StorageManager::instance().getAvailableMediaDirectories();
    QFileInfoList allFiles;

    // 1. Traverse all physical mounts
    for (const QString& dirPath : dirs) {
        QDir dir(dirPath);
        QStringList filters;
        filters << "*.jpg" << "*.JPG" << "*.jpeg" << "*.JPEG" << "*.avi" << "*.AVI";
        allFiles.append(dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot));
    }

    // 2. Global Chronological Sort (Oldest at index 0, Newest at index N-1)
    std::sort(allFiles.begin(), allFiles.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return a.lastModified().toMSecsSinceEpoch() < b.lastModified().toMSecsSinceEpoch();
    });

    // 3. Populate lightweight dataset
    m_fileList.clear();
    for (const QFileInfo& info : allFiles) {
        MediaFileInfo item;
        item.filePath = info.absoluteFilePath();
        item.timestamp = info.lastModified().toMSecsSinceEpoch();

        if (info.suffix().toLower() == "avi") {
            item.type = CaptureMode::Video;
            // Extract lightweight metadata upfront to prevent UI stutter
            item.durationStr = VideoProber::getDuration(item.filePath);
        } else {
            item.type = CaptureMode::Photo;
        }

        m_fileList.append(item);
    }

    emit datasetChanged();
}

// ==========================================
// Request Routing & Caching
// ==========================================

bool GalleryService::isFullResolutionRequest(const QSize& targetSize) const {
    int screenWidth = GlobalContext::instance().screenSize().width();
    return targetSize.width() >= (screenWidth * 0.6);
}

int GalleryService::getMediaCount() const {
    return m_fileList.size();
}

MediaFileInfo GalleryService::getMediaInfo(int index) const {
    if (index >= 0 && index < m_fileList.size()) {
        return m_fileList.at(index);
    }
    return MediaFileInfo();
}

QImage GalleryService::requestImage(int index, const QSize& targetSize) {
    if (index < 0 || index >= m_fileList.size()) return QImage();

    const MediaFileInfo& item = m_fileList.at(index);
    const QString reqKey = item.filePath + "_" + QString::number(targetSize.width());

    auto* cache = isFullResolutionRequest(targetSize) ? &m_fullCache : &m_thumbCache;
    QImage* cachedImg = cache->object(item.filePath);

    bool needsHigherResolution = cachedImg && (cachedImg->width() < targetSize.width() - 5);
    bool missingCompletely = !cachedImg;

    if (needsHigherResolution || missingCompletely) {
        if (!m_pendingRequests.contains(reqKey)) {
            m_pendingRequests.insert(reqKey);

            // Dispatch task using simple Round-Robin load balancing
            QMetaObject::invokeMethod(m_workers[m_nextWorkerIndex], "processImageRequest", Qt::QueuedConnection,
                                      Q_ARG(QString, item.filePath),
                                      Q_ARG(CaptureMode, item.type),
                                      Q_ARG(QSize, targetSize));

            m_nextWorkerIndex = (m_nextWorkerIndex + 1) % WORKER_COUNT;
        }
    }

    /*
     * Progressive Loading:
     * Serve the existing lower-resolution image immediately to prevent
     * skeletal UI flickering during dynamic layout transitions (e.g., Pinch-to-Zoom).
     */
    if (cachedImg) {
        return *cachedImg;
    }

    return QImage();
}

void GalleryService::onWorkerImageReady(const QString& path, const QImage& img, QSize targetSize) {
    const QString reqKey = path + "_" + QString::number(targetSize.width());
    m_pendingRequests.remove(reqKey);

    if (img.isNull()) return;

    auto* cache = isFullResolutionRequest(targetSize) ? &m_fullCache : &m_thumbCache;

    /*
     * Race Condition Defense:
     * High-frequency UI layout changes can yield out-of-order decode completions.
     * Protect existing high-resolution cached segments from being overwritten by delayed low-resolution callbacks.
     */
    if (QImage* existingImg = cache->object(path)) {
        if (existingImg->width() >= img.width()) {
            return;
        }
    }

    cache->insert(path, new QImage(img), 1);

    for (int i = 0; i < m_fileList.size(); ++i) {
        if (m_fileList[i].filePath == path) {
            emit thumbnailUpdated(i);
            break;
        }
    }
}

// ==========================================
// Mutations
// ==========================================

void GalleryService::appendNewMedia(const QString& path, CaptureMode type) {
    MediaFileInfo item;
    item.filePath = path;
    item.type = type;
    item.timestamp = QDateTime::currentMSecsSinceEpoch();

    if (type == CaptureMode::Video) {
        item.durationStr = VideoProber::getDuration(path);
    }

    m_fileList.append(item);
    emit datasetChanged();
}

void GalleryService::deleteMedia(int index) {
    if (index < 0 || index >= m_fileList.size()) return;

    QString path = m_fileList.at(index).filePath;

    // 1. Delete physical file
    QFile::remove(path);

    // 2. Nuke specific cache instances
    m_thumbCache.remove(path);
    m_fullCache.remove(path);

    // 3. Remove from timeline
    m_fileList.removeAt(index);

    emit datasetChanged();
}

void GalleryService::preloadViewerTrio(int currentIndex, const QSize& targetSize) {
    if (currentIndex < 0 || currentIndex >= m_fileList.size()) return;

    requestImage(currentIndex, targetSize);

    if (currentIndex > 0) {
        requestImage(currentIndex - 1, targetSize);
    }

    if (currentIndex < m_fileList.size() - 1) {
        requestImage(currentIndex + 1, targetSize);
    }
}
