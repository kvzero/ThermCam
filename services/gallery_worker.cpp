#include "gallery_worker.h"
#include "media/image_decoder.h"
#include "media/video_prober.h"
#include <QDebug>

GalleryWorker::GalleryWorker(QObject* parent) : QObject(parent) {}

void GalleryWorker::processImageRequest(const QString& path, CaptureMode type, QSize targetSize) {
    QImage resultImage;

    if (type == CaptureMode::Photo) {
        resultImage = ImageDecoder::decode(path, targetSize);
    } else if (type == CaptureMode::Video) {
        resultImage = VideoProber::extractFirstFrame(path, targetSize);
    }

    // Always emit the signal, even if null, so the Service can clear the pending lock
    emit imageReady(path, resultImage, targetSize);
}
