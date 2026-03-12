#include "image_decoder.h"
#include "hardware/rga/rga_image.h"
#include <QImageReader>

QImage ImageDecoder::decode(const QString& path, const QSize& targetSize) {
    QImageReader reader(path);
    if (!reader.canRead()) {
        return QImage();
    }

    const QSize originalSize = reader.size();

    if (targetSize.isEmpty() || originalSize == targetSize) {
        return reader.read();
    }

    /*
     * DCT Downscaling Optimization:
     * Instructs libjpeg-turbo to drop high-frequency DCT coefficients during decoding.
     * This bypasses full-resolution memory allocation and slashes CPU time by up to 80%.
     * Note: libjpeg only scales by fractions (1/2, 1/4, 1/8), so the resulting image
     * might still be slightly larger than targetSize.
     */
    reader.setScaledSize(targetSize);

    QImage sourceImage = reader.read();
    if (sourceImage.isNull()) {
        return QImage();
    }

    /*
     * Hardware fine-scaling:
     * Use RGA to exactly match the requested targetSize from the roughly-scaled DCT image.
     */
    if (sourceImage.size() != targetSize) {
        return RgaImage(sourceImage).scaled(targetSize).toQImage();
    }

    return sourceImage;
}
