# CLAUDE.md

Guidance for Claude Code (or any AI assistant) working in this repository.

## Project overview

This project turns an ESP32 "Cheap Yellow Display" (CYD) board into a wireless
remote screen/touchpad/terminal for a PC or Raspberry Pi. A Python server captures
the desktop (or a virtual Xvfb display), sends only the changed screen region
(delta updates) as JPEG frames over a WebSocket to the CYD, and the CYD sends
touch coordinates back to drive the mouse.

A companion keyboard forwarder (`keyboard_forwarder.py`) allows a physical USB
keyboard plugged into the Raspberry Pi to type directly into the virtual terminal
shown on the CYD screen.

### Components & Architecture

```
Physical Keyboard (USB/Wireless on Pi)
   | (evdev /dev/input/event*)
   v
keyboard_forwarder.py (press/release)
   |
   +--> Xvfb :1 (Virtual Display 320x240) <-- matchbox-wm + xterm
           ^
           |
screen_server.py (mss grab + delta diff + cv2 jpeg)
   |
   +--- ws://<PI_IP>:8080/screen (Binary JPEG deltas) ---> CYD (TFT_eSPI + TJpgDec)
   <--- ws://<PI_IP>:8080/screen ("M:x,y,state" Touch) --- CYD (Touch Panel)
```

1. **PC / Pi Screen Server:** `screen_server.py` — captures display, calculates delta bounding boxes, compresses with OpenCV JPEG, and streams to CYD. Receives touch events and controls the mouse with `pynput`.
2. **Keyboard Forwarder:** `keyboard_forwarder.py` — monitors all keyboard devices in `/dev/input/event*`, handles hotplug auto-detection, and translates physical key press/release events into X11 input for `DISPLAY=:1`.
3. **Headless Terminal Runner:** `start_cyd_terminal.sh` — starts `Xvfb :1` (with `-ac` multi-user access), `matchbox-window-manager`, auto-respawning `xterm`, and `screen_server.py`.
4. **Device side (ESP32 CYD):** `sketch_aug13a_copy_20260813152125/sketch_aug13a_copy_20260813152125.ino` — Arduino firmware for ESP32-2432S028. Connects to Wi-Fi, WebSocket, decodes JPEG deltas to TFT, and reports touch events.

---

## Raspberry Pi Headless Terminal Setup

### 1. Prerequisites (Install on Pi)
```bash
sudo apt update
sudo apt install -y xvfb xterm matchbox-window-manager xdotool python3-pip python3-venv
```

### 2. Python Environment Setup
```bash
cd ~/cyd-screen
python3 -m venv venv
source venv/bin/activate
pip install websockets mss opencv-python numpy pynput evdev
```

### 3. Run Manually for Testing
In Terminal 1 (Start virtual screen & server):
```bash
chmod +x start_cyd_terminal.sh
./start_cyd_terminal.sh
```

In Terminal 2 (Start keyboard forwarder as root):
```bash
sudo DISPLAY=:1 ~/cyd-screen/venv/bin/python ~/cyd-screen/keyboard_forwarder.py
```

### 4. Enable Systemd Services (Auto-start on Boot)
```bash
sudo cp cyd-screen.service /etc/systemd/system/
sudo cp cyd-keyboard.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now cyd-screen.service
sudo systemctl enable --now cyd-keyboard.service
```

To check service status and logs:
```bash
sudo systemctl status cyd-screen.service
sudo systemctl status cyd-keyboard.service
journalctl -u cyd-keyboard.service -f
```

---

## Protocol & Configuration

- **WebSocket endpoint:** `ws://<PI_IP>:8080/screen` (path `/screen`).
- **Screen -> CYD frame format:** 4-byte header (`uint16 x`, `uint16 y` big-endian) followed by JPEG bytes.
- **CYD -> Server touch format:** `M:x,y,state` (`1` = pressed, `0` = released).
- **CYD Resolution:** 320x240.
