#ifndef IMAGE_ENCODER_H
#define IMAGE_ENCODER_H

#include <QString>
#include <QImage>
#include "core/types.h"

class ImageEncoder {
public:
    /**
     * @brief Persists an image to disk.
     * Architecture placeholder for future EXIF/MakerNote injection.
     */
    static bool save(const QImage& img, const QString& path, const VisualFrame& metadata);
};

#endif // IMAGE_ENCODER_H
