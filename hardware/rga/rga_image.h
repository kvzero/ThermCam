#ifndef RGA_IMAGE_H
#define RGA_IMAGE_H

#include <QImage>

/**
 * @brief RGA Hardware Acceleration Wrapper
 * Value-based semantics: RgaImage(src).op().toQImage()
 */
class RgaImage {
public:
    // Initialize backend resources
    static void globalInit();

    RgaImage() = default;
    explicit RgaImage(const QImage& image);

    QImage toQImage() const;
    bool isValid() const;

    // Hardware Operations (Chainable)
    RgaImage scaled(int w, int h) const;
    RgaImage scaled(const QSize& size) const;
    RgaImage blurred(int radius) const;
    RgaImage rotated(int angle) const;

private:
    QImage m_image;
};

#endif // RGA_IMAGE_H
