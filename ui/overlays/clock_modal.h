#ifndef CLOCK_MODAL_H
#define CLOCK_MODAL_H

#include "ui/overlays/modal_dialog.h"

#include <QDateTime>
#include <QRect>
#include <QTimer>
#include <functional>

/**
 * @brief Modal editor for the camera system clock.
 *
 * The widget keeps a local candidate time while it is open; the caller commits
 * that value to Linux/RTC only when the primary action succeeds.
 */
class ClockModal : public ModalBase {
    Q_OBJECT
public:
    using CommitHandler = std::function<bool(const QDateTime&, QString*)>;

    explicit ClockModal(QWidget* parent = nullptr);
    ~ClockModal() override = default;

    void setDateTime(const QDateTime& dateTime);
    void setCommitHandler(CommitHandler handler);

protected:
    ContentLayout contentLayoutHint(const QSize& viewportSize) const override;
    void paintContent(QPainter& p, const QRect& contentRect) override;
    bool onPrimaryAction() override;
    bool contentPress(const QPoint& contentPos) override;
    bool contentMove(const QPoint& contentPos) override;
    bool contentRelease(const QPoint& contentPos) override;
    void contentCancel() override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    enum class Field {
        Year,
        Month,
        Day,
        Hour,
        Minute,
        Second
    };

    enum class Arrow {
        None,
        Up,
        Down
    };

    struct Layout {
        QRect fieldRects[6];
        QRect upRect;
        QRect downRect;
        QRect dateRowRect;
        QRect timeRowRect;
        QRect dividerRect;
    };

    struct Config {
        /* Layout */
        const qreal CONTENT_WIDTH_RATIO = 0.72;
        const qreal CONTENT_HEIGHT_RATIO = 0.51;
        const qreal ROW_VERTICAL_PAD_RATIO = 0.12;
        const qreal ROW_GAP_RATIO = 0.08;
        const qreal CONTROL_GAP_RATIO = 0.055;
        const qreal FIELD_GAP_RATIO = 0.018;

        /* Typography */
        const qreal LABEL_FONT_RATIO = 0.18;
        const qreal YEAR_VALUE_FONT_RATIO = 0.40;
        const qreal VALUE_FONT_RATIO = 0.47;
        const qreal ARROW_FONT_RATIO = 0.78;

        /* Paint */
        const int ROW_CORNER_RADIUS = 24;
        const int SELECTED_CORNER_RADIUS = 22;
        const int DIVIDER_INSET_PX = 3;
        const int DIVIDER_OPTICAL_OFFSET_PX = 7;
    } m_cfg;

    Layout buildLayout(const QSize& contentSize) const;
    int fieldIndex(Field field) const;
    Field fieldAt(const QPoint& contentPos) const;
    Arrow arrowAt(const QPoint& contentPos) const;
    void stepSelectedField(int delta);
    void stopArrowRepeat();
    void normalizeDateTime();

    QDateTime m_value;
    Field m_selectedField = Field::Year;
    Arrow m_pressedArrow = Arrow::None;
    CommitHandler m_commitHandler;
    QTimer* m_repeatTimer = nullptr;
    QTimer* m_clockTimer = nullptr;
    bool m_repeatFast = false;
};

#endif // CLOCK_MODAL_H
