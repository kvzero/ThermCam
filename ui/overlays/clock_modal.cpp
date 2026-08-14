#include "clock_modal.h"

#include "core/event_bus.h"

#include <QFont>
#include <QDebug>
#include <QHideEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QtGlobal>
#include <utility>

namespace {
constexpr int kYearMin = 2020;
constexpr int kYearMax = 2099;
constexpr int kRepeatDelayMs = 420;
constexpr int kRepeatIntervalMs = 115;

QString twoDigits(int value) {
    return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0'));
}

int wrapValue(int value, int minValue, int maxValue, int delta) {
    if (maxValue < minValue) return minValue;
    int next = value + delta;
    if (next < minValue) next = maxValue;
    if (next > maxValue) next = minValue;
    return next;
}

QDate clampedDate(int year, int month, int day) {
    year = qBound(kYearMin, year, kYearMax);
    month = qBound(1, month, 12);
    const int maxDay = QDate(year, month, 1).daysInMonth();
    day = qBound(1, day, maxDay);
    return QDate(year, month, day);
}
} // namespace

ClockModal::ClockModal(QWidget* parent) : ModalBase(parent) {
    m_repeatTimer = new QTimer(this);
    connect(m_repeatTimer, &QTimer::timeout, this, [this]() {
        if (m_pressedArrow == Arrow::None) return;
        if (!m_repeatFast) {
            m_repeatFast = true;
            m_repeatTimer->setInterval(kRepeatIntervalMs);
        }
        stepSelectedField(m_pressedArrow == Arrow::Up ? 1 : -1);
    });

    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(1000);
    connect(m_clockTimer, &QTimer::timeout, this, [this]() {
        // Natural time flow may carry into larger fields while the modal is open.
        m_value = m_value.addSecs(1);
        update();
    });

    setDateTime(QDateTime::currentDateTime());
}

void ClockModal::setDateTime(const QDateTime& dateTime) {
    m_value = dateTime.isValid() ? dateTime : QDateTime(QDate(kYearMin, 1, 1), QTime(0, 0, 0));
    normalizeDateTime();
    m_selectedField = Field::Year;
    stopArrowRepeat();
    update();
}

void ClockModal::setCommitHandler(CommitHandler handler) {
    m_commitHandler = std::move(handler);
}

ModalBase::ContentLayout ClockModal::contentLayoutHint(const QSize& viewportSize) const {
    Q_UNUSED(viewportSize);

    ContentLayout out;
    out.screenRatio = QSizeF(m_cfg.CONTENT_WIDTH_RATIO, m_cfg.CONTENT_HEIGHT_RATIO);
    return out;
}

void ClockModal::paintContent(QPainter& p, const QRect& contentRect) {
    const Layout layout = buildLayout(contentRect.size());
    const QPoint origin = contentRect.topLeft();

    auto translated = [origin](const QRect& r) {
        return r.translated(origin);
    };

    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 26));
    p.drawRoundedRect(translated(layout.dateRowRect),
                      m_cfg.ROW_CORNER_RADIUS,
                      m_cfg.ROW_CORNER_RADIUS);
    p.drawRoundedRect(translated(layout.timeRowRect),
                      m_cfg.ROW_CORNER_RADIUS,
                      m_cfg.ROW_CORNER_RADIUS);

    p.setBrush(QColor(255, 255, 255, 34));
    p.drawRoundedRect(translated(layout.dividerRect), 2, 2);

    const QString labels[6] = {
        QStringLiteral("YEAR"),
        QStringLiteral("MON"),
        QStringLiteral("DAY"),
        QStringLiteral("HOUR"),
        QStringLiteral("MIN"),
        QStringLiteral("SEC")
    };
    const QString values[6] = {
        QString::number(m_value.date().year()),
        twoDigits(m_value.date().month()),
        twoDigits(m_value.date().day()),
        twoDigits(m_value.time().hour()),
        twoDigits(m_value.time().minute()),
        twoDigits(m_value.time().second())
    };

    const int selected = fieldIndex(m_selectedField);
    for (int i = 0; i < 6; ++i) {
        const QRect r = translated(layout.fieldRects[i]);
        const bool active = (i == selected);

        if (active) {
            QPainterPath path;
            path.addRoundedRect(r,
                                m_cfg.SELECTED_CORNER_RADIUS,
                                m_cfg.SELECTED_CORNER_RADIUS);
            p.fillPath(path, QColor(42, 126, 210, 155));
            p.setPen(QPen(QColor(105, 188, 255, 175), 1));
            p.drawPath(path);
        }

        QFont labelFont("Roboto");
        labelFont.setPixelSize(qMax(8, qRound(r.height() * m_cfg.LABEL_FONT_RATIO)));
        labelFont.setBold(true);
        p.setFont(labelFont);
        p.setPen(active ? QColor(214, 242, 255) : QColor(190, 194, 198));
        p.drawText(QRect(r.left(), r.top() + qRound(r.height() * 0.12),
                         r.width(), qRound(r.height() * 0.23)),
                   Qt::AlignCenter,
                   labels[i]);

        QFont valueFont("Roboto");
        valueFont.setPixelSize(qMax(15, qRound(r.height() * (i == 0
                                                                 ? m_cfg.YEAR_VALUE_FONT_RATIO
                                                                 : m_cfg.VALUE_FONT_RATIO))));
        valueFont.setBold(true);
        p.setFont(valueFont);
        p.setPen(Qt::white);
        p.drawText(QRect(r.left(), r.top() + qRound(r.height() * 0.37),
                         r.width(), qRound(r.height() * 0.48)),
                   Qt::AlignCenter,
                   values[i]);
    }

    auto drawArrow = [&](const QRect& localRect, Arrow arrow, const QString& glyph) {
        const QRect r = translated(localRect);
        const bool pressed = (m_pressedArrow == arrow && arrowAt(r.center() - origin) == arrow);

        QFont iconFont("tabler-icons");
        iconFont.setPixelSize(qRound(r.height() * m_cfg.ARROW_FONT_RATIO));
        p.setFont(iconFont);
        p.setPen(pressed ? QColor(95, 188, 255) : QColor(245, 247, 250));
        p.drawText(r, Qt::AlignCenter, glyph);
    };

    drawArrow(layout.upRect, Arrow::Up, QString(QChar(0xf6e3)));
    drawArrow(layout.downRect, Arrow::Down, QString(QChar(0xf6e0)));
    p.restore();
}

bool ClockModal::onPrimaryAction() {
    if (!m_commitHandler) return true;

    QString error;
    if (m_commitHandler(m_value, &error)) {
        return true;
    }

    if (!error.isEmpty()) {
        qWarning() << "[ClockModal] Commit failed:" << error;
    }
    emit EventBus::instance().toastRequested("SET TIME FAILED", ToastLevel::Error);
    return false;
}

bool ClockModal::contentPress(const QPoint& contentPos) {
    const Arrow arrow = arrowAt(contentPos);
    if (arrow != Arrow::None) {
        m_pressedArrow = arrow;
        m_repeatFast = false;
        stepSelectedField(arrow == Arrow::Up ? 1 : -1);
        m_repeatTimer->start(kRepeatDelayMs);
        update();
        return true;
    }

    const Field nextField = fieldAt(contentPos);
    if (nextField != m_selectedField) {
        m_selectedField = nextField;
        update();
    }

    stopArrowRepeat();
    return true;
}

bool ClockModal::contentMove(const QPoint& contentPos) {
    if (m_pressedArrow == Arrow::None) return true;

    if (arrowAt(contentPos) != m_pressedArrow) {
        stopArrowRepeat();
    }
    update();
    return true;
}

bool ClockModal::contentRelease(const QPoint& /*contentPos*/) {
    stopArrowRepeat();
    return true;
}

void ClockModal::contentCancel() {
    stopArrowRepeat();
}

void ClockModal::showEvent(QShowEvent* event) {
    ModalBase::showEvent(event);
    if (m_clockTimer) m_clockTimer->start();
}

void ClockModal::hideEvent(QHideEvent* event) {
    stopArrowRepeat();
    if (m_clockTimer) m_clockTimer->stop();
    ModalBase::hideEvent(event);
}

ClockModal::Layout ClockModal::buildLayout(const QSize& contentSize) const {
    Layout layout;

    const int W = contentSize.width();
    const int H = contentSize.height();
    const int verticalPad = qRound(H * m_cfg.ROW_VERTICAL_PAD_RATIO);
    const int rowGap = qRound(H * m_cfg.ROW_GAP_RATIO);
    const int rowH = (H - (verticalPad * 2) - rowGap) / 2;
    const int controlW = rowH;
    const int gap = qRound(W * m_cfg.CONTROL_GAP_RATIO);
    const int controlX = W - controlW;
    const int fieldsW = controlX - gap;
    const int rowX = 0;
    const int rowTop = verticalPad;
    const int rowBottom = rowTop + rowH + rowGap;

    layout.dateRowRect = QRect(rowX, rowTop, fieldsW, rowH);
    layout.timeRowRect = QRect(rowX, rowBottom, fieldsW, rowH);

    const int fieldGap = qRound(fieldsW * m_cfg.FIELD_GAP_RATIO);
    const int fieldW = (fieldsW - fieldGap * 2) / 3;
    for (int row = 0; row < 2; ++row) {
        const int y = (row == 0) ? rowTop : rowBottom;
        for (int col = 0; col < 3; ++col) {
            const int index = row * 3 + col;
            layout.fieldRects[index] = QRect(rowX + col * (fieldW + fieldGap),
                                             y,
                                             fieldW,
                                             rowH);
        }
    }

    layout.upRect = QRect(controlX, rowTop, controlW, rowH);
    layout.downRect = QRect(controlX, rowBottom, controlW, rowH);
    layout.dividerRect = QRect(rowX + fieldsW + (gap / 2) + m_cfg.DIVIDER_OPTICAL_OFFSET_PX,
                               rowTop + m_cfg.DIVIDER_INSET_PX,
                               2,
                               rowBottom + rowH - rowTop - (m_cfg.DIVIDER_INSET_PX * 2));

    return layout;
}

int ClockModal::fieldIndex(Field field) const {
    switch (field) {
    case Field::Year: return 0;
    case Field::Month: return 1;
    case Field::Day: return 2;
    case Field::Hour: return 3;
    case Field::Minute: return 4;
    case Field::Second: return 5;
    }
    return 0;
}

ClockModal::Field ClockModal::fieldAt(const QPoint& contentPos) const {
    const Layout layout = buildLayout(contentGeometry().size());
    for (int i = 0; i < 6; ++i) {
        if (!layout.fieldRects[i].contains(contentPos)) continue;
        switch (i) {
        case 0: return Field::Year;
        case 1: return Field::Month;
        case 2: return Field::Day;
        case 3: return Field::Hour;
        case 4: return Field::Minute;
        case 5: return Field::Second;
        default: break;
        }
    }
    return m_selectedField;
}

ClockModal::Arrow ClockModal::arrowAt(const QPoint& contentPos) const {
    const Layout layout = buildLayout(contentGeometry().size());
    if (layout.upRect.contains(contentPos)) return Arrow::Up;
    if (layout.downRect.contains(contentPos)) return Arrow::Down;
    return Arrow::None;
}

void ClockModal::stepSelectedField(int delta) {
    QDate date = m_value.date();
    QTime time = m_value.time();
    int year = date.year();
    int month = date.month();
    int day = date.day();

    // Manual arrow edits stay within the selected field. They deliberately do
    // not borrow or carry into neighboring fields like a running clock tick.
    switch (m_selectedField) {
    case Field::Year:
        year = qBound(kYearMin, year + delta, kYearMax);
        break;
    case Field::Month:
        month = wrapValue(month, 1, 12, delta);
        break;
    case Field::Day:
        day = wrapValue(day, 1, QDate(year, month, 1).daysInMonth(), delta);
        break;
    case Field::Hour:
        time = QTime(wrapValue(time.hour(), 0, 23, delta), time.minute(), time.second());
        break;
    case Field::Minute:
        time = QTime(time.hour(), wrapValue(time.minute(), 0, 59, delta), time.second());
        break;
    case Field::Second:
        time = QTime(time.hour(), time.minute(), wrapValue(time.second(), 0, 59, delta));
        break;
    }

    date = clampedDate(year, month, day);
    m_value = QDateTime(date, time);
    update();
}

void ClockModal::stopArrowRepeat() {
    if (m_repeatTimer) m_repeatTimer->stop();
    m_repeatFast = false;
    m_pressedArrow = Arrow::None;
    update();
}

void ClockModal::normalizeDateTime() {
    QDate date = m_value.date();
    QTime time = m_value.time();
    if (!date.isValid()) date = QDate(kYearMin, 1, 1);
    if (!time.isValid()) time = QTime(0, 0, 0);
    m_value = QDateTime(clampedDate(date.year(), date.month(), date.day()), time);
}
