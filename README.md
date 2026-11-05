# SafeStride (Hit2Help)
### Intelligent Partial Weight-Bearing (PWB) Rehabilitation Trainer

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Arduino](https://img.shields.io/badge/Platform-Arduino%20Uno%20R3-00979C.svg)](https://www.arduino.cc/)
[![Team](https://img.shields.io/badge/Team-Nexora%20%7C%20Bennett%20University-blueviolet.svg)](https://www.bennett.edu.in/)

**SafeStride** is an embedded biofeedback assistive device designed for orthopedic and post-surgical lower-limb rehabilitation. It enables patients recovering from lower-limb fractures, reconstructive surgery, or stroke to strictly follow physician-prescribed **Partial Weight-Bearing (PWB)** regimens through real-time tactile force monitoring and progressive multi-stage biofeedback.

---

## System Architecture

```
[ Dual RP-C30-ST FSR Sensors ]
    Heel (A0) & Forefoot (A1)
               │
               ▼
   [ Fixed 10 kΩ Pull-Down ]
               │
               ▼
[ Arduino Uno R3 (ATmega328P) ] ◄── [ Dual Pushbuttons (D8/D9) ]
   - 50 Hz Bare-Metal Polling
   - Conductance Transfer Model
               │
       ┌───────┴────────────────────────┐
       ▼                                ▼
[ 16x2 HD44780 LCD ]        [ Multi-Stage Biofeedback ]
 Live Weight & Status        - Visual Warning LED (D11)
                             - Active/Passive Buzzer (D10)
```

---

## Progressive Biofeedback State Machine

SafeStride delivers progressive sensory cues to prevent patient overload:

| State | Condition | Visual Indicator (LED) | Auditory Feedback (Buzzer) |
|---|---|---|---|
| **SAFE** | $\text{Weight} < (\text{Limit} - 3.0\text{ kg})$ | OFF | OFF |
| **CAUTION** | $\text{Weight} \ge (\text{Limit} - 3.0\text{ kg})$ | ON | OFF |
| **BUZZ WARN** | $\text{Weight} \ge (\text{Limit} - 2.0\text{ kg})$ | ON | Pulsing Beep (2.2 kHz, 80 ms cycle) |
| **OVERLOAD!** | $\text{Weight} \ge \text{Target Limit}$ | ON | Continuous Alarm (2.8 kHz) |
| **STANDBY** | Dual Button Press (OY + BR) | OFF | OFF (System Paused) |

---

## Sensor Physics & Conductance Calibration

SafeStride uses **RP-C30-ST** force-sensitive resistors configured in a voltage divider circuit with $R_0 = 10.0\text{ k}\Omega$ pull-down resistors connected to GND:

1. **ADC to Sensor Resistance ($R_s$)**:
   $$R_s = R_0 \cdot \frac{1023 - \text{ADC}}{\text{ADC}} = 10.0 \cdot \frac{1023 - \text{ADC}}{\text{ADC}} \quad (\text{k}\Omega)$$

2. **Conductance Linearity ($G$)**:
   Piezoresistive polymer sensors exhibit a hyperbolic non-linear resistance curve, but electrical conductance ($G = \frac{1}{R_s}$) is linear with applied force:
   $$G = \frac{\text{ADC}}{10.0 \cdot (1023 - \text{ADC})} \quad (\text{k}\Omega^{-1})$$

3. **Datasheet Calibrated Force Function**:
   $$\text{Weight (kg)} = (G - 0.104) \times 20.0 \text{ kg}$$
   *(Includes baseline noise filtering below 0.15 kg and 20.0 kg saturation clamping)*

---

## Hardware Pinout Map

| Component | Pin / Channel | Arduino Uno R3 Pin | Function / Description |
|---|---|---|---|
| **LCD 16x2** | RS | **D2** | Register Select |
| **LCD 16x2** | EN | **D3** | Enable Signal |
| **LCD 16x2** | DB4 – DB7 | **D7, D6, D5, D4** | 4-bit Parallel Data Bus |
| **FSR Heel** | Signal (Divider) | **A0** | Heel impact load cell (10 kΩ pull-down) |
| **FSR Forefoot** | Signal (Divider) | **A1** | Metatarsal load cell (10 kΩ pull-down) |
| **Button OY** | Signal | **D8** | Threshold Increment (+1.0 kg step, `INPUT_PULLUP`) |
| **Button BR** | Signal | **D9** | Threshold Increment (+0.5 kg step, `INPUT_PULLUP`) |
| **Piezo Buzzer** | Signal (+) | **D10** | Universal Buzzer Driver (`tone` & `digitalWrite`) |
| **Indicator LED**| Anode (+) | **D11** | Warning LED indicator (via 220 Ω / 330 Ω resistor) |

---

## Repository Structure

```
safestride/
├── README.md                          # Project documentation
├── .gitignore                         # Git exclusion rules
├── Hit2Help_Firmware/
│   └── Hit2Help_Firmware.ino          # Arduino Uno R3 production firmware
├── cpp/
│   ├── hit2help_sim.cpp               # Modern C++17 object-oriented system simulation
│   └── Makefile                       # Build system for C++ simulation
├── Hit2Help_Circuit_Schematic.png     # Hardware schematic diagram
├── Hit2Help_Block_Diagram.png         # System architecture block diagram
├── Hit2Help_Methodology_Flowchart.png # Clinical workflow and logic flowchart
└── Hit2Help_System_Characterization.png # Empirical characterization graphs
```

---

## Getting Started

### 1. Running the C++ System Simulation
The C++ implementation provides a modular simulation bench that verifies sensor conductance math, state transitions, and LCD formatting without hardware:

```bash
cd cpp
make
make run
```

### 2. Flashing the Arduino Firmware
1. Open [`Hit2Help_Firmware/Hit2Help_Firmware.ino`](Hit2Help_Firmware/Hit2Help_Firmware.ino) in the **Arduino IDE**.
2. Select Board: **Arduino Uno** and your corresponding serial port.
3. Verify that the `LiquidCrystal` library is installed (standard with Arduino).
4. Click **Upload** (Ctrl+U / Cmd+U).
5. Open the **Serial Monitor** at **9600 baud** for live telemetry logging.

---

## Authors & Attribution
* **Team Nexora**: Department of Engineering, Bennett University (ENGG101)
* Project Mentors & Evaluators: School of Engineering and Applied Sciences
