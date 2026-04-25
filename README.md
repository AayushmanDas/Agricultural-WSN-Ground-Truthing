# Agricultural WSN Ground-Truthing
**Bridging the gap between orbital soil moisture observations and high-fidelity terrestrial in-situ data.**

[![Preprint](https://img.shields.io/badge/Preprint-TechRxiv-blue)]([Pending_TechRxiv_Transition])
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Affiliation](https://img.shields.io/badge/Affiliation-Jadavpur_University-red)](https://jadavpuruniversity.in/)

## 📡 Research Objective
This framework is an end-to-end Wireless Sensor Network (WSN) designed to provide a local baseline for the validation of NASA's SMAP (Soil Moisture Active Passive) satellite products. By deploying synchronized sensor nodes in tropical micro-climates, the system identifies localized discrepancies that 9km orbital estimates cannot resolve.

## 🚀 Key Research Findings
* **Urban Resolution Gap:** Identified a **39.30% RMSE** discrepancy between orbital data and ground truth, suggesting significant spatial averaging bias in regional satellite products.
* **Environmental Resilience:** Maintained 100% data continuity for critical reference nodes during a **severe hailstorm (March 26)** through a triple-redundant logging pipeline.
* **Hydrological Dynamics:** Captured distinct moisture drying slopes ($m \approx -4.2\%/day$) influenced by urban verticality and micro-climatic "pockets."

## 🏗️ Technical Architecture
* **Protocol:** Optimized **ESP-NOW** (1 Mbps Long Range) for robust peer-to-peer data acquisition through reinforced concrete environments.
* **Power Management:** Custom Li-ion harvesting with **HT7333-A LDOs** (4µA quiescent current) and deep-sleep optimization for maximum field longevity.
* **Triple-Redundancy Pipeline:** 1. **Local:** Real-time OLED telemetry.
    2. **Edge:** SPI-based SD card logging (DS3231 RTC synchronized).
    3. **Cloud:** PostgreSQL (Supabase) sync via asynchronous Wi-Fi handshakes.

## 📂 Repository Structure
* **/firmware** - Source code for Central Hub & Remote Nodes.
* **/hardware** - Fusion 360 CAD models, Fritzing schematics, and BOM.
* **/scripts** - Python scripts for heuristic noise reduction and NASA SMAP correlation.
* **/docs** - Full IEEE-format technical paper and assembly manuals.

## 🛠️ Hardware Specifications
| Component | Detail |
| :--- | :--- |
| **Microcontroller** | ESP32-WROOM-32 |
| **Atmospheric Sensor** | BME280 (Temperature, Humidity, Pressure) |
| **Soil Sensor** | Capacitive v1.2 (Potded/Ruggedized) |
| **Regulation** | HT7333-A Low-Dropout Regulator |
| **Casing** | Custom 3D-printed Hub + IP65 Field Enclosures |

## ⚖️ Citation
If you utilize this framework or dataset, please cite the preprint:
> Das, A. (2026). "Robustness Evaluation of a Low-Cost WSN for Satellite Ground-Truthing under Extreme Tropical Weather Events." TechRxiv. DOI: [Pending TechRxiv Transition]

---
**Maintained by Aayushman Das** *Department of Electronics and Telecommunication Engineering, Jadavpur University*