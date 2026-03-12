#ifndef HAPTIC_PROVIDER_H
#define HAPTIC_PROVIDER_H

#include <QObject>
#include <vector>

/**
 * @brief Controller for LRA motor feedback using customized DRV260X driver.
 *
 * This class interfaces with a modified Linux Force Feedback (FF) driver.
 * It tunnels up to 4 waveform IDs through standard rumble magnitude fields
 * to leverage the hardware sequencer of the TI DRV2605L chip.
 */
class HapticProvider : public QObject {
    Q_OBJECT
public:
    static HapticProvider& instance();

    /**
     * @brief Bind to the haptic device node defined by udev.
     * @return true if the device /dev/input/event-haptics is ready.
     */
    bool init();

    /**
     * @brief Play a sequence of pre-stored waveforms (1 to 4 IDs).
     * @param ids Vector of Waveform IDs (Valid range: 1-123).
     * @note Data Mapping:
     *       ID1 -> strong_magnitude[0:7]
     *       ID2 -> strong_magnitude[8:15]
     *       ID3 -> weak_magnitude[0:7]
     *       ID4 -> weak_magnitude[8:15]
     */
    void playSequence(const std::vector<int>& ids);

    /**
     * @brief Convenience function to play a single effect ID.
     * @param id Waveform ID.
     */
    void playEffect(int id);

private:
    explicit HapticProvider(QObject* parent = nullptr);
    ~HapticProvider();

    HapticProvider(const HapticProvider&) = delete;
    HapticProvider& operator=(const HapticProvider&) = delete;

    int m_fd = -1;
    int m_effectId = -1;
};

#endif // HAPTIC_PROVIDER_H
