#ifndef SETTINGS_ROW_H
#define SETTINGS_ROW_H

#include <QWidget>
#include "core/settings_types.h"

/**
 * @brief Shared press-owner contract for settings rows under InteractionArbiter delegation.
 *
 * Owns per-row gesture intent gating (press highlight, vertical-scroll cancel,
 * and release-time activation) while a touch stream is captured by this row.
 */
class SettingsBaseRow : public QWidget {
    Q_OBJECT
public:
    /* --- Lifecycle --- */
    explicit SettingsBaseRow(QWidget* parent = nullptr);

    /* --- UIController Protocol --- */
    Q_INVOKABLE bool handleInteractionUpdate(QPoint localPos);
    Q_INVOKABLE void finalizeGesture(int dy);
    Q_INVOKABLE void cancelGesture();

signals:
    /* --- Cross-Module Signals --- */
    void activated();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

    /* --- Visual State Hooks --- */
    bool isPressed() const { return m_isPressed; }
    virtual void onPressStateChanged();

private:
    /* --- Private State --- */
    bool m_isPressed = false;
    bool m_clickCanceled = false;
    QPoint m_pressPos;
    QPoint m_lastPos;
};

/**
 * @brief Primary-list row renderer owning icon/title/divider/compress visual logic.
 *
 * Receives immutable menu descriptors from SettingsView and renders both single-
 * column and expanded-left-panel states, including selected-block appearance.
 */
class SettingsPrimaryRow : public SettingsBaseRow {
    Q_OBJECT
public:
    /* --- Lifecycle --- */
    explicit SettingsPrimaryRow(QWidget* parent = nullptr);

    /* --- UIController Inputs --- */
    void setData(const PrimaryItemData& data);
    void setSplitProgress(qreal p);
    void setSelected(bool selected);
    void setTargetLayoutWidth(int width);
    void setBottomDividerVisible(bool visible);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /* --- Private State --- */
    PrimaryItemData m_data;
    qreal m_splitProgress = 0.0;
    bool m_isSelected = false;
    int m_targetLayoutWidth = 0;
    bool m_showBottomDivider = true;
};

/**
 * @brief Secondary-list row renderer owning title/trailing-affordance visual logic.
 *
 * Represents a single actionable/value/toggle placeholder entry in the expanded
 * right panel and follows the same press-highlight cancel policy as primary rows.
 */
class SettingsSecondaryRow : public SettingsBaseRow {
    Q_OBJECT
public:
    /* --- Lifecycle --- */
    explicit SettingsSecondaryRow(QWidget* parent = nullptr);

    /* --- UIController Inputs --- */
    void setData(const SecondaryItemData& data);
    void setBottomDividerVisible(bool visible);
    void toggleVisualState();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QRect toggleTrackRect() const;
    QRect toggleHitRect() const;

    /* --- Private State --- */
    SecondaryItemData m_data;
    bool m_showBottomDivider = true;
    bool m_toggleOn = false;
};

#endif // SETTINGS_ROW_H
