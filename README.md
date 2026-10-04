# ESPHome Notification Screen (ELEGOO ESP-32 2.8" Display)

This project turns an **ELEGOO ESP-32 2.8 Inch Touch Screen Display** (also known as the **E32R28T** / Cheap Yellow Display CYD family) into a dedicated, interactive notification monitor.

It periodically polls `https://rehearsexr.com/notify.json`, which delivers an array of notification items:
```json
[
  {
    "r": 255,
    "g": 0,
    "b": 0,
    "delay": 1000,
    "text": "Check your phone",
    "timestamp": "2026-10-03T15:00:00Z"
  }
]
```

---

## Key Features

- **Solid Color Fill**: Dynamically fills the entire 320x240 display with the RGB color specified by each notification payload (`r`, `g`, `b`).
- **Dynamic High-Contrast Typography**: Computes ITU-R BT.601 relative luminance on the background color to automatically select crisp white text on dark backgrounds and black text on bright backgrounds.
- **Word Wrapping & Dual-Axis Centering**: Text wraps cleanly within margins (respecting explicit `\n` breaks as well as long sentences) and is centered both horizontally and vertically above the bottom bar.
- **Timestamp Display**: Parses the `"timestamp"` field from the payload and renders it directly above the bottom DISMISS button.
- **Single vs. Multi-Notification Logic**:
  - **Single Notification**: If only 1 notification is in the queue, its `delay` value is ignored and it stays on screen indefinitely until dismissed or updated.
  - **Multiple Notifications**: If 2+ notifications exist, the display cycles through each notification for its specified `delay` in milliseconds.
- **Interactive Touchscreen Dismiss (XPT2046)**:
  - Tapping the bottom **DISMISS** bar immediately clears the currently displayed notification.
  - If multiple notifications exist, only the current item is removed, immediately advancing to the next queued notification.
  - If only 1 notification remains after dismissing, it automatically switches to persistent hold mode.
- **Queue Appending & Intelligent Deduplication**:
  - When new notifications arrive on subsequent polling cycles, they are **appended** to the internal queue rather than overwriting existing un-dismissed items.
  - Prevents auto-dismissing earlier messages when fresh ones arrive.
  - Intelligent deduplication ensures that already-queued notifications and previously dismissed notifications are not re-added on periodic polls.
- **Standby Screen**:
  - When all notifications have been dismissed, the display transitions to a sleek dark grey background with black text reading **"No active notifications"**.
- **Wi-Fi Portability & Captive Portal**:
  - Automatically reconnects using saved NVS credentials.
  - If the network is unavailable or the device is moved to a new location, after 10 seconds (`ap_timeout: 10s`) it launches its own fallback hotspot **`NotificationScreen-AP`**. Connect from your phone or laptop and navigate to `http://192.168.4.1` to configure new Wi-Fi credentials.
- **Physical BOOT Button (GPIO0) Wi-Fi Reset**:
  - Press and hold the physical **BOOT button** on the back of the ESP32 for **3 seconds** to trigger an on-demand Wi-Fi reset.
  - Displays a red confirmation screen (*"Resetting Wi-Fi... Rebooting into setup mode"*) while wiping flash preferences, then automatically reboots into `NotificationScreen-AP` mode.
- **Internal RAM Optimization**:
  - Uses `color_palette: 8BIT` to ensure the 320x240 framebuffer fits comfortably in internal ESP32 DRAM (~76.8 KB vs. 153.6 KB for 16-bit), rendering reliably at ~77ms without requiring external PSRAM.

---

## Hardware Pinout (ELEGOO 2.8" ESP32 / E32R28T)

| Peripheral | Function | ESP32 GPIO | Notes |
| :--- | :--- | :--- | :--- |
| **Display (ILI9341)** | TFT_CS | GPIO 15 | Chip Select (Active Low) |
| | TFT_DC | GPIO 2 | Data / Command |
| | TFT_CLK | GPIO 14 | SPI Clock (`tft_spi`) |
| | TFT_MOSI | GPIO 13 | SPI MOSI (`tft_spi`) |
| | TFT_MISO | GPIO 12 | SPI MISO (`tft_spi`) |
| | TFT_RST | EN | Connected to ESP32 reset line |
| **Backlight** | BL_PWM | GPIO 21 | PWM Brightness via `ledc` |
| **Touch (XPT2046)** | TOUCH_CLK | GPIO 25 | SPI Clock (`touch_spi`) |
| | TOUCH_MOSI | GPIO 32 | SPI MOSI (`touch_spi`) |
| | TOUCH_MISO | GPIO 39 | SPI MISO (`touch_spi`) |
| | TOUCH_CS | GPIO 33 | Touch Chip Select |
| | TOUCH_IRQ | GPIO 36 | Touch Interrupt (`mode: INPUT`) |
| **Buttons** | BOOT | GPIO 0 | Hold for 3s to reset Wi-Fi & preferences |

---

## Project Structure

- [`notification_screen.yaml`](notification_screen.yaml): Primary ESPHome configuration specifying board hardware, display, SPI buses, touch, Wi-Fi captive portal, and polling intervals.
- [`notification_manager.h`](notification_manager.h): C++ component managing the notification queue, timestamp parsing, JSON deserialization, word wrapping, luminance contrast, delay cycling, deduplication, and dismiss logic.
- [`secrets.yaml.example`](secrets.yaml.example): Template for local Wi-Fi credentials.

---

## Getting Started

### 1. Initial Wi-Fi Provisioning
You can either:
- **Provision via Captive Portal (No code edits needed)**: Flash the firmware directly. On first boot (or if no network is reachable within 10s), connect your phone to `NotificationScreen-AP` and enter your Wi-Fi details at `http://192.168.4.1`.
- **Preconfigure in YAML**: Copy `secrets.yaml.example` to `secrets.yaml` and enter your network credentials:
  ```yaml
  wifi_ssid: "Your_WiFi_Network_Name"
  wifi_password: "Your_WiFi_Password"
  ```

### 2. Customizing Substitutions
In [`notification_screen.yaml`](notification_screen.yaml), you can configure:
```yaml
substitutions:
  device_name: "notification-screen"
  friendly_name: "Notification Screen"
  notify_url: "https://rehearsexr.com/notify.json"
  poll_interval: "30s"
```

### 3. Compile and Flash
Connect the board to your computer via USB and flash:
```bash
esphome run --device /dev/cu.usbserial-11340 notification_screen.yaml
```

---

## User Interactions

- **Dismiss a Notification**: Tap the **DISMISS** bar at the bottom of the screen.
- **Reset Wi-Fi**: Hold the **BOOT button** on the back of the board for **3 seconds**.
- **Standby Mode**: Once all notifications are dismissed, the screen stays on dark grey with black text (*"No active notifications"*) until new notifications are posted to the endpoint.
