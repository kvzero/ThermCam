#ifndef IMAGE_DECODER_H
#define IMAGE_DECODER_H

#include <QImage>
#include <QString>
#include <QSize>

class ImageDecoder {
public:
    /**
     * @brief Decodes an image file and scales it using hardware acceleration.
     * Validates headers first to bypass RGA overhead if scaling is unnecessary.
     */
    static QImage decode(const QString& path, const QSize& targetSize = QSize());
};

#endif // IMAGE_DECODER_H
