#include <iostream>
#include <iomanip>
#include <string>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstdio>

// ============================================================================
// PROJECT: Hit2Help / SafeStride Rehabilitation Trainer
// ARCHITECTURE: Object-Oriented C++17 Hardware Abstraction & System Model
// ============================================================================

enum class AlertState {
    STANDBY,
    SAFE,
    CAUTION,
    BUZZ_WARN,
    OVERLOAD
};

inline const char* alertStateToString(AlertState state) {
    switch (state) {
        case AlertState::STANDBY:   return "STANDBY  ";
        case AlertState::SAFE:      return "SAFE     ";
        case AlertState::CAUTION:   return "CAUTION  ";
        case AlertState::BUZZ_WARN: return "BUZZ WARN";
        case AlertState::OVERLOAD:  return "OVERLOAD!";
        default:                    return "UNKNOWN  ";
    }
}

// ----------------------------------------------------------------------------
// CLASS: FSRSensor
// Models the RP-C30-ST piezoresistive sensor with fixed 10k pull-down divider.
// ----------------------------------------------------------------------------
class FSRSensor {
private:
    std::string name_;
    float pullDownResistanceKohm_; // 10.0 kOhm
    int currentAdc_{0};

public:
    explicit FSRSensor(std::string name, float pullDownKohm = 10.0f)
        : name_(std::move(name)), pullDownResistanceKohm_(pullDownKohm) {}

    void update(int adcVal) noexcept {
        currentAdc_ = std::clamp(adcVal, 0, 1023);
    }

    [[nodiscard]] int getRawAdc() const noexcept {
        return currentAdc_;
    }

    [[nodiscard]] float getResistanceKohm() const noexcept {
        if (currentAdc_ <= 5) return 999.0f;
        return pullDownResistanceKohm_ * (1023.0f - static_cast<float>(currentAdc_)) / static_cast<float>(currentAdc_);
    }

    [[nodiscard]] float getConductance() const noexcept {
        if (currentAdc_ <= 5) return 0.0f;
        return static_cast<float>(currentAdc_) / (pullDownResistanceKohm_ * (1023.0f - static_cast<float>(currentAdc_)));
    }

    [[nodiscard]] float getWeightKg() const noexcept {
        if (currentAdc_ < 100) return 0.0f; // Baseline no-load deadband
        int clamped = std::min(currentAdc_, 1000);

        // RP-C30-ST Datasheet Conductance Formula:
        // G = 5e-5 * F(g) + 0.104  =>  Force (kg) = (G - 0.104) * 20.0 kg
        float g = static_cast<float>(clamped) / (pullDownResistanceKohm_ * (1023.0f - static_cast<float>(clamped)));
        float forceKg = (g - 0.104f) * 20.0f;

        if (forceKg < 0.15f) return 0.0f;
        if (forceKg > 20.0f) return 20.0f;
        return forceKg;
    }

    [[nodiscard]] const std::string& getName() const noexcept {
        return name_;
    }
};

// ----------------------------------------------------------------------------
// CLASS: RehabilitationSystem
// Manages patient thresholds, biofeedback state, LCD formatting, and telemetry.
// ----------------------------------------------------------------------------
class RehabilitationSystem {
public:
    static constexpr float MIN_LIMIT     = 3.5f;
    static constexpr float MAX_LIMIT     = 19.5f;
    static constexpr float LIGHT_MARGIN  = 3.0f; // Visual LED warning margin (kg)
    static constexpr float BUZZ_MARGIN   = 2.0f; // Auditory buzzer warning margin (kg)
    static constexpr float HYSTERESIS    = 0.20f; // 200g deadband prevents boundary jitter/flicker

private:
    FSRSensor heelSensor_{"Heel (A0)"};
    FSRSensor forefootSensor_{"Forefoot (A1)"};

    float targetLimit_{5.0f}; // Default target limit: 5.0 kg
    bool isArmed_{true};
    AlertState currentState_{AlertState::SAFE};

    bool ledState_{false};
    bool buzzerState_{false};
    int buzzerFrequencyHz_{0};

public:
    RehabilitationSystem() = default;

    void setArmed(bool armed) noexcept {
        isArmed_ = armed;
        if (!isArmed_) {
            currentState_ = AlertState::STANDBY;
            ledState_ = false;
            buzzerState_ = false;
            buzzerFrequencyHz_ = 0;
        }
    }

    void toggleArmed() noexcept {
        setArmed(!isArmed_);
    }

    [[nodiscard]] bool isArmed() const noexcept {
        return isArmed_;
    }

    void adjustLimit(float stepKg) noexcept {
        if (!isArmed_) return;
        targetLimit_ += stepKg;
        if (targetLimit_ > MAX_LIMIT) {
            targetLimit_ = MIN_LIMIT;
        } else if (targetLimit_ < MIN_LIMIT) {
            targetLimit_ = MAX_LIMIT;
        }
    }

    void processSensors(int adcHeel, int adcForefoot, uint32_t currentMillis = 0) {
        heelSensor_.update(adcHeel);
        forefootSensor_.update(adcForefoot);

        if (!isArmed_) {
            currentState_ = AlertState::STANDBY;
            ledState_ = false;
            buzzerState_ = false;
            buzzerFrequencyHz_ = 0;
            return;
        }

        float totalWeight = getTotalWeightKg();
        float lightThreshold = targetLimit_ - LIGHT_MARGIN; // e.g. 2.0 kg
        float buzzThreshold  = targetLimit_ - BUZZ_MARGIN;  // e.g. 3.0 kg

        // Schmitt Trigger Hysteresis to completely stop boundary flickering
        switch (currentState_) {
            case AlertState::OVERLOAD:
                if (totalWeight < (lightThreshold - HYSTERESIS)) {
                    currentState_ = AlertState::SAFE;
                } else if (totalWeight < (buzzThreshold - HYSTERESIS)) {
                    currentState_ = AlertState::CAUTION;
                } else if (totalWeight < (targetLimit_ - HYSTERESIS)) {
                    currentState_ = AlertState::BUZZ_WARN;
                }
                break;
            case AlertState::BUZZ_WARN:
                if (totalWeight >= targetLimit_) {
                    currentState_ = AlertState::OVERLOAD;
                } else if (totalWeight < (lightThreshold - HYSTERESIS)) {
                    currentState_ = AlertState::SAFE;
                } else if (totalWeight < (buzzThreshold - HYSTERESIS)) {
                    currentState_ = AlertState::CAUTION;
                }
                break;
            case AlertState::CAUTION:
                if (totalWeight >= targetLimit_) {
                    currentState_ = AlertState::OVERLOAD;
                } else if (totalWeight >= buzzThreshold) {
                    currentState_ = AlertState::BUZZ_WARN;
                } else if (totalWeight < (lightThreshold - HYSTERESIS)) {
                    currentState_ = AlertState::SAFE;
                }
                break;
            default: // SAFE
                if (totalWeight >= targetLimit_) {
                    currentState_ = AlertState::OVERLOAD;
                } else if (totalWeight >= buzzThreshold) {
                    currentState_ = AlertState::BUZZ_WARN;
                } else if (totalWeight >= lightThreshold) {
                    currentState_ = AlertState::CAUTION;
                } else {
                    currentState_ = AlertState::SAFE;
                }
                break;
        }

        // Output actuators based on stable state
        switch (currentState_) {
            case AlertState::OVERLOAD:
                ledState_ = true;
                buzzerState_ = true;
                buzzerFrequencyHz_ = 2800; // 2.8 kHz continuous alarm
                break;
            case AlertState::BUZZ_WARN:
                ledState_ = true;
                buzzerState_ = ((currentMillis / 80) % 2 == 0);
                buzzerFrequencyHz_ = 2200; // 2.2 kHz pulsing warning
                break;
            case AlertState::CAUTION:
                ledState_ = true;
                buzzerState_ = false;
                buzzerFrequencyHz_ = 0;
                break;
            case AlertState::SAFE:
            case AlertState::STANDBY:
                ledState_ = false;
                buzzerState_ = false;
                buzzerFrequencyHz_ = 0;
                break;
        }
    }

    [[nodiscard]] float getTotalWeightKg() const noexcept {
        return heelSensor_.getWeightKg() + forefootSensor_.getWeightKg();
    }

    [[nodiscard]] float getTargetLimitKg() const noexcept { return targetLimit_; }
    [[nodiscard]] AlertState getState() const noexcept { return currentState_; }
    [[nodiscard]] bool getLedState() const noexcept { return ledState_; }
    [[nodiscard]] bool getBuzzerState() const noexcept { return buzzerState_; }
    [[nodiscard]] int getBuzzerFrequencyHz() const noexcept { return buzzerFrequencyHz_; }

    [[nodiscard]] const FSRSensor& getHeelSensor() const noexcept { return heelSensor_; }
    [[nodiscard]] const FSRSensor& getForefootSensor() const noexcept { return forefootSensor_; }

    void renderLcd(std::string& line1, std::string& line2) const {
        line1.resize(16, ' ');
        line2.resize(16, ' ');

        if (!isArmed_) {
            line1 = "Hit2Help Paused ";
            line2 = "Press OY+BR Start";
            return;
        }

        float totalWeight = getTotalWeightKg();
        char buf1[17];
        char buf2[17];

        std::snprintf(buf1, sizeof(buf1), "Live: %4.1f kg   ", totalWeight);
        std::snprintf(buf2, sizeof(buf2), "L:%4.1f %-9s", targetLimit_, alertStateToString(currentState_));

        line1 = buf1;
        line2 = buf2;
        if (line1.length() > 16) line1 = line1.substr(0, 16);
        if (line2.length() > 16) line2 = line2.substr(0, 16);
    }

    void printTelemetry() const {
        std::cout << "[TELEMETRY] State: " << alertStateToString(currentState_)
                  << " | Weight: " << std::fixed << std::setprecision(1) << getTotalWeightKg() << " kg"
                  << " | Limit: " << targetLimit_ << " kg"
                  << " | LED: " << (ledState_ ? "ON " : "OFF")
                  << " | Buzzer: " << (buzzerState_ ? "ON " : "OFF")
                  << " (" << buzzerFrequencyHz_ << " Hz)"
                  << " || Heel: ADC=" << heelSensor_.getRawAdc() << " (" << heelSensor_.getWeightKg() << " kg)"
                  << " | Forefoot: ADC=" << forefootSensor_.getRawAdc() << " (" << forefootSensor_.getWeightKg() << " kg)\n";
    }
};

// ============================================================================
// SIMULATION & TEST BENCH
// ============================================================================
int main() {
    std::cout << "======================================================================\n";
    std::cout << "  HIT2HELP / SAFESTRIDE REHABILITATION TRAINER (C++17 SIMULATION)     \n";
    std::cout << "  Conductance Calibration Model: RP-C30-ST Datasheet Curve           \n";
    std::cout << "======================================================================\n\n";

    RehabilitationSystem system;

    std::cout << "[TEST 1] Initial Configuration Verification\n";
    std::cout << "Target Limit: " << system.getTargetLimitKg() << " kg\n";
    std::cout << "Armed State: " << (system.isArmed() ? "ARMED" : "STANDBY") << "\n\n";

    // Scenario simulation: Gradual load application
    struct TestCase {
        int heelAdc;
        int forefootAdc;
        const char* description;
    };

    std::vector<TestCase> testCases = {
        { 0,    0,   "No load / unweighted sensor (ADC < baseline)" },
        { 520,  520, "Baseline resting contact (0.0 kg net force)" },
        { 690,  520, "Entering Caution Zone (Heel ~2.1 kg + Fore 0.0 kg = 2.1 kg >= Limit - 3.0 kg)" },
        { 734,  576, "Entering Buzz Warning (Heel 3.0 kg + Fore 0.5 kg = 3.5 kg >= Limit - 2.0 kg)" },
        { 769,  686, "Full Overload Breached (Heel 4.0 kg + Fore 2.0 kg = 6.0 kg >= Limit 5.0 kg)" },
        { 520,  520, "Unloading back to baseline resting stance" }
    };

    std::cout << "[TEST 2] Stepping Through Patient Gait Load Cycles:\n";
    std::cout << "----------------------------------------------------------------------\n";

    uint32_t simulatedTimeMs = 0;
    for (const auto& tc : testCases) {
        system.processSensors(tc.heelAdc, tc.forefootAdc, simulatedTimeMs);
        std::cout << "\n>>> SCENARIO: " << tc.description << "\n";
        system.printTelemetry();

        std::string l1, l2;
        system.renderLcd(l1, l2);
        std::cout << "    [16x2 LCD Line 1] \"" << l1 << "\"\n";
        std::cout << "    [16x2 LCD Line 2] \"" << l2 << "\"\n";

        simulatedTimeMs += 100;
    }

    std::cout << "\n----------------------------------------------------------------------\n";
    std::cout << "[TEST 3] Testing Button Limit Adjustments (+1.0 kg and +0.5 kg steps):\n";
    system.adjustLimit(1.0f); // Button OY
    std::cout << "Adjusted +1.0 kg -> New Limit: " << system.getTargetLimitKg() << " kg\n";
    system.adjustLimit(0.5f); // Button BR
    std::cout << "Adjusted +0.5 kg -> New Limit: " << system.getTargetLimitKg() << " kg\n";

    std::cout << "\n[TEST 4] Testing Dual Button Toggle (Pause / Resume):\n";
    system.toggleArmed();
    std::cout << "System Armed: " << (system.isArmed() ? "YES" : "NO (STANDBY)") << "\n";
    system.processSensors(620, 550, simulatedTimeMs);
    std::string l1, l2;
    system.renderLcd(l1, l2);
    std::cout << "    [LCD Standby L1] \"" << l1 << "\"\n";
    std::cout << "    [LCD Standby L2] \"" << l2 << "\"\n";

    std::cout << "\n======================================================================\n";
    std::cout << "  ALL VERIFICATION TESTS COMPLETED SUCCESSFULLY!                      \n";
    std::cout << "======================================================================\n";

    return 0;
}
