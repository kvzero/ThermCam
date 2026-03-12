#ifndef GALLERY_WORKER_H
#define GALLERY_WORKER_H

#include <QObject>
#include <QImage>
#include <QString>
#include <QSize>
#include "core/types.h"

/**
 * @brief Heavy-lifting thread for I/O and media decoding.
 * Operates strictly asynchronously to prevent UI stalling.
 */
class GalleryWorker : public QObject {
    Q_OBJECT
public:
    explicit GalleryWorker(QObject* parent = nullptr);

public slots:
    /**
     * @brief Process an image/video decode request and emit the result.
     * @param path The absolute path of the file (Unique Key).
     * @param type Differentiates routing to ImageDecoder or VideoDecoder.
     * @param targetSize The exact resolution required by the UI.
     */
    void processImageRequest(const QString& path, CaptureMode type, QSize targetSize);

signals:
    /**
     * @brief Emitted when a decode job completes.
     * @param path The file path matching the request.
     * @param img The resulting decoded and hardware-scaled image.
     * @param targetSize Echoed back to help the Service route it to the correct cache.
     */
    void imageReady(const QString& path, const QImage& img, QSize targetSize);
};

#endif // GALLERY_WORKER_H
