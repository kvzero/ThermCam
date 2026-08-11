#ifndef KEY_MANAGER_H
#define KEY_MANAGER_H

#include <QObject>
#include <QElapsedTimer>

class QSocketNotifier;
class QTimer;

/**
 * @brief Reads the physical adc-keys device and emits semantic key events.
 */
class KeyManager : public QObject {
    Q_OBJECT
public:
    explicit KeyManager(QObject* parent = nullptr);
    ~KeyManager();

    /**
     * @brief Probes adc-keys and starts monitoring without blocking UI startup.
     * @return Always true so missing keys do not prevent rendering.
     */
    bool init();

private slots:
    void processKeyEvents();
    void emitLongPress();

private:
    void beginPress();
    void endPress();

    /* Releases between these thresholds are intentionally ignored. */
    static constexpr int SHORT_PRESS_MAX_MS = 500;
    static constexpr int LONG_PRESS_MS = 2000;

    int m_keyFd = -1;
    QSocketNotifier* m_keyNotifier = nullptr;
    QTimer* m_longPressTimer = nullptr;
    QElapsedTimer m_pressTimer;
    bool m_pressed = false;
    bool m_longPressEmitted = false;
};

#endif // KEY_MANAGER_H
