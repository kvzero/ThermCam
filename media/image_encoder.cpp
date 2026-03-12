#include "image_encoder.h"

bool ImageEncoder::save(const QImage& img, const QString& path, const VisualFrame& metadata) {
    Q_UNUSED(metadata);

    // Relies on libjpeg-turbo under the Qt hood
    return img.save(path, "JPG", 90);
}
