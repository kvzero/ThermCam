#ifndef GALLERY_SERVICE_H
#define GALLERY_SERVICE_H

#include <QObject>
#include <QList>
#include <QCache>
#include <QSet>
#include <QThread>

#include "core/types.h"

class GalleryWorker;

/**
 * @brief Central authority for gallery data, routing, and memory management.
 * Utilizes a dual-cache system with aggressive lifecycle purging to fit within 128MB RAM.
 */
class GalleryService : public QObject {
    Q_OBJECT
public:
    static GalleryService& instance();

    /* --- Lifecycle & Initialization --- */
    void scanDirectory();
    void clearMemory();       ///< Nuke everything. Call when exiting GalleryView.
    void clearViewerCache();  ///< Nuke full-res images only. Call when returning to Grid.

    /* --- Data Accessors --- */
    int getMediaCount() const;
    MediaFileInfo getMediaInfo(int index) const;

    /**
     * @brief Core API for UI rendering.
     * @return Valid QImage if cached, otherwise a null QImage while triggering a background decode.
     */
    QImage requestImage(int index, const QSize& targetSize);

    /**
     * @brief 专为 Viewer 模式设计的 OOM 防御预加载机制。
     * 只要求解码当前、上一张、下一张，填满 MAX_FULL_RES_CACHE_ITEMS (3) 的容量。
     */
    void preloadViewerTrio(int currentIndex, const QSize& targetSize);

    /* --- Mutations --- */
    void appendNewMedia(const QString& path, CaptureMode type);
    void deleteMedia(int index);

signals:
    void datasetChanged();        ///< Fired on complete scan, insertion, or deletion.
    void thumbnailUpdated(int index); ///< Fired when a specific background decode finishes.

private slots:
    void onWorkerImageReady(const QString& path, const QImage& img, QSize targetSize);

private:
    explicit GalleryService(QObject* parent = nullptr);
    ~GalleryService();

    GalleryService(const GalleryService&) = delete;
    GalleryService& operator=(const GalleryService&) = delete;

    bool isFullResolutionRequest(const QSize& targetSize) const;

    /* Architecture Components */
    static constexpr int WORKER_COUNT = 2; // Dedicate 2 CPU cores for decoding

    QThread* m_workerThreads[WORKER_COUNT] = {nullptr};
    GalleryWorker* m_workers[WORKER_COUNT] = {nullptr};
    int m_nextWorkerIndex = 0;

    /* Global Timeline */
    QList<MediaFileInfo> m_fileList;

    /* Dual Cache Engine */
    QCache<QString, QImage> m_thumbCache;
    QCache<QString, QImage> m_fullCache;

    /* Async Lock Mechanisms (Key: "path_width") */
    QSet<QString> m_pendingRequests;

    /* Configuration Limits */
    static constexpr int MAX_THUMBNAIL_CACHE_ITEMS = 40;
    static constexpr int MAX_FULL_RES_CACHE_ITEMS  = 3;
};

#endif // GALLERY_SERVICE_H
