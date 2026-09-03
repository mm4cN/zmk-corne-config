# ZMK Corne Configuration

My personal [ZMK](https://zmk.dev/) configuration for a wireless **Corne (CRKBD)** split keyboard.

What started as a keyboard configuration eventually acquired a trackpad driver, custom display code and enough Zephyr plumbing to stop pretending this is just a keymap.

## Hardware

- Corne / CRKBD split keyboard
- Wireless nRF52-based controllers
- 680 mAh battery per half
- Kailh Deep Whale Choc switches
- Cirque GlidePoint 23 mm trackpad
- Custom status display on the left half

## Cirque trackpad

The right half integrates a **Cirque Pinnacle / GlidePoint 23 mm** circular trackpad.

Instead of relying on the standard ZMK input path, the repository contains a custom polling-based Zephyr input driver:

```text
drivers/input/input_pinnacle_polling.c
dts/bindings/input/cirque,pinnacle-polling.yaml
```

The driver communicates with the Cirque controller over I²C and exposes pointer movement and tap events through the Zephyr input subsystem.

Current configuration:

```text
I²C address:       0x2a
Polling interval:  2 ms
Movement scale:    2
Primary tap:       enabled
Orientation:       swap XY + invert X
```

The orientation settings compensate for the physical placement of the trackpad on the keyboard.

## Display

The left half uses a custom ZMK status screen implementation:

```text
src/display/my_status_screen.c
```

It provides keyboard status information directly on the device instead of using the default ZMK status widget.

## Configuration

The keyboard configuration lives under:

```text
config/
├── corne.conf
├── corne.keymap
├── corne_left.conf
├── corne_left.overlay
├── corne_pointer.dtsi
├── corne_right.conf
└── corne_right.overlay
```

The split configuration keeps hardware-specific settings for each half separate while sharing the main keymap.

## Building

This repository is structured as a ZMK/Zephyr module and uses `west` for dependency management.

Initialize the workspace and fetch dependencies using the standard ZMK development workflow, then build the appropriate Corne shield for each half.

See the [ZMK documentation](https://zmk.dev/docs/development/setup) for the current development environment setup and build instructions.

## Why?

Because apparently putting a trackpad on a wireless split keyboard was not sufficiently complicated until it involved writing a Zephyr driver.

---

Personal configuration first, reference implementation second.

Feel free to borrow whatever is useful for your own build.
