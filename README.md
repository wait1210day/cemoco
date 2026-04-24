
# Cemoco

Cemoco is a digital peak-current mode buck converter firmware built for the **STM32G474** microcontroller. 

Designed with modern embedded software engineering principles, it utilizes a real-time operating system (FreeRTOS) and an internal publish-subscribe message bus, bridging the gap between high-frequency digital control theory and robust system management.

## Key Features

* **Digital Peak-Current Mode Control (PCMC):** Implements fast cycle-by-cycle current limiting and control using the STM32G4's analog comparators and DACs.
* **Dual-Loop Regulation:** Seamless transition between Constant Voltage (CV) and Constant Current (CC) regulation using digital PI controllers.
* **Advanced Efficiency Modes:** Features automatic transitions between Continuous Conduction Mode (CCM) / Forced PWM and Burst Mode to maximize efficiency under light load conditions.
* **Slope Compensation:** Hardware-assisted dynamic slope compensation driven by the DAC sawtooth wave generator.
* **Hermes Message Bus:** A lightweight, internal publish/subscribe message broker built on top of FreeRTOS queues for decoupled inter-task communication.
* **FDCAN Host Interface:** Reliable telemetry and control command handling via FDCAN.
* **Active Thermal Management:** Dynamic, temperature-based fan curve control.

## Architecture

### Software Stack
The firmware is built on top of **FreeRTOS** to ensure deterministic task scheduling for non-critical system management, while the fast power control loops operate safely inside ISR which is not managed by FreeRTOS.

* `app_main.c`: System initialization, task spawning.
* `ctrloop.c`: The core control theory implementation. Handles outer CV/CC loops, slope compensation updates, and CCM/Burst mode transitions.
* `hermes.c`: Internal topic-based Pub/Sub message routing system for cross-task telemetry (e.g., passing `ctrloop` measures to the host interface or debug tasks).
* `hostif.c`: Manages external communications over FDCAN.

### Peripheral Configuration
Cemoco takes full advantage of G474's analog peripherals:

* **HRTIM:** Drives the main buck MOSFETs and schedules precise ADC injection triggers.
* **ADC1 & ADC2:** Configured for fast, injected conversions of critical voltages and currents, triggering the `ctrloop_isr_on_adc_fastpath` exactly when data is ready.
* **DAC & COMP:** The DAC generates a sawtooth wave for slope compensation, feeding directly into the high-speed analog comparator to trip the HRTIM on peak current.

## Building

### Prerequisites
* **Toolchain:** `arm-none-eabi-gcc` or `clang`
* **Build System:** CMake (v3.22+)
* **Hardware:** An STM32G474-based custom buck converter board with FDCAN interface.
* **Debugger:** OpenOCD or equivalent GDB server.

### Building the Firmware

Build the firmware using CMake:
```bash
# Our you can build a debug version
$ cmake --preset Release
$ ninja -C build/Release
```

Then flash it using OpenOCD with STLink:
```bash
$ openocd -f openocd/board.cfg -c "program build/Release/cemoco.elf verify reset exit"
```

## License

This project is free software; you can redistribute it and/or modify it under the terms of the **GNU General Public License v3.0** (GPLv3) as published by the Free Software Foundation.

See the [LICENSE](LICENSE) file for more details.
