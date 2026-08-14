#!/usr/bin/env python3
"""
keyboard_forwarder.py

อ่านสัญญาณระดับ Hardware จากคีย์บอร์ดจริงทุกตัวที่เสียบเข้า Pi (ผ่าน evdev)
แล้วส่งต่อโดยตรงไปยัง X11 XTest extension (Hardware-level event) บน Xvfb :1
พร้อมระบบ Auto-Reconnect X11 Display ป้องกัน Broken Pipe เมื่อ Xvfb รีสตาร์ท
"""

import glob
import os
import select
import sys
import time
import evdev
from evdev import ecodes
import Xlib.display
import Xlib.X
import Xlib.ext.xtest

# ตรวจสอบและตั้งค่า DISPLAY ให้เป็น :1 หากไม่ได้ระบุ
if "DISPLAY" not in os.environ:
    os.environ["DISPLAY"] = ":1"


def connect_x11_display(max_retries=30, retry_delay=1.0):
    """เชื่อมต่อกับ X Server โดยรอให้ Xvfb พร้อมทำงาน"""
    disp_name = os.environ.get("DISPLAY", ":1")
    for attempt in range(1, max_retries + 1):
        try:
            display = Xlib.display.Display(disp_name)
            print(f"[*] Connected to X display {disp_name} (XTest ready)")
            return display
        except Exception as e:
            if attempt % 5 == 0 or attempt == 1:
                print(f"[.] Waiting for X display {disp_name} (attempt {attempt}/{max_retries}): {e}")
            time.sleep(retry_delay)
    print(f"[!] Error: Could not connect to X display {disp_name} after {max_retries} attempts.")
    return None


def send_x11_key(display, keycode, is_press):
    """ส่ง keycode ไปยัง X11 XTest พร้อมตรวจจับ Broken Pipe และ reconnect อัตโนมัติ"""
    try:
        Xlib.ext.xtest.fake_input(display, Xlib.X.KeyPress if is_press else Xlib.X.KeyRelease, keycode)
        display.sync()
        return display, True
    except Exception as e:
        print(f"[!] X11 error on keycode {keycode} ({e}). Reconnecting to display...")
        try:
            display.close()
        except Exception:
            pass
        new_disp = connect_x11_display(max_retries=10, retry_delay=0.5)
        if new_disp is not None:
            try:
                Xlib.ext.xtest.fake_input(new_disp, Xlib.X.KeyPress if is_press else Xlib.X.KeyRelease, keycode)
                new_disp.sync()
                return new_disp, True
            except Exception:
                pass
            return new_disp, False
        return display, False


def is_keyboard_device(dev):
    """ตรวจสอบว่าอุปกรณ์นี้เป็นคีย์บอร์ดหรือไม่"""
    try:
        caps = dev.capabilities()
        keys = caps.get(ecodes.EV_KEY, [])
        if ecodes.KEY_A in keys and (ecodes.KEY_SPACE in keys or ecodes.KEY_ENTER in keys):
            return True
    except Exception:
        pass
    return False


def scan_keyboards(current_devices):
    """สแกนหาคีย์บอร์ดทั้งหมดในระบบ และ grab อุปกรณ์ที่ยังไม่ได้ติดตาม"""
    new_devices = {}
    found_paths = set()

    for path in glob.glob("/dev/input/event*"):
        found_paths.add(path)
        already_open = False
        for fd, dev in current_devices.items():
            if dev.path == path:
                new_devices[fd] = dev
                already_open = True
                break
        if already_open:
            continue

        try:
            dev = evdev.InputDevice(path)
            if is_keyboard_device(dev):
                try:
                    dev.grab()
                    print(f"[+] Attached & Grabbed keyboard: {dev.name} ({dev.path})")
                except Exception as grab_err:
                    print(f"[+] Attached keyboard (un-grabbed): {dev.name} ({dev.path}) [{grab_err}]")
                new_devices[dev.fd] = dev
            else:
                dev.close()
        except Exception:
            pass

    # ตรวจสอบตัวที่หลุดไป
    for fd, dev in list(current_devices.items()):
        if dev.path not in found_paths:
            print(f"[-] Keyboard removed: {dev.name} ({dev.path})")
            try:
                dev.ungrab()
            except Exception:
                pass
            try:
                dev.close()
            except Exception:
                pass

    return new_devices


def main():
    print(f"[*] Starting CYD Hardware-Level Keyboard Forwarder (Target DISPLAY={os.environ.get('DISPLAY', ':1')})...")

    display = connect_x11_display()
    if display is None:
        sys.exit(1)

    devices = {}
    held_keycodes = set()
    last_scan_time = 0

    try:
        while True:
            now = time.time()
            if now - last_scan_time > 3.0 or not devices:
                devices = scan_keyboards(devices)
                last_scan_time = now
                if not devices:
                    print("[.] No keyboard found in /dev/input/. Please plug in a keyboard (will auto-detect)...")
                    time.sleep(2.0)
                    continue

            r, _, _ = select.select(list(devices.keys()), [], [], 2.0)
            if not r:
                continue

            for fd in r:
                dev = devices.get(fd)
                if dev is None:
                    continue

                try:
                    for event in dev.read():
                        if event.type != ecodes.EV_KEY:
                            continue

                        x11_keycode = event.code + 8
                        if x11_keycode < 8 or x11_keycode > 255:
                            continue

                        if event.value == 1:  # Press
                            display, ok = send_x11_key(display, x11_keycode, True)
                            if ok:
                                held_keycodes.add(x11_keycode)
                        elif event.value == 0:  # Release
                            display, ok = send_x11_key(display, x11_keycode, False)
                            held_keycodes.discard(x11_keycode)

                except (OSError, IOError) as e:
                    print(f"[-] Keyboard disconnected: {dev.name} ({dev.path}) [{e}]")
                    try:
                        dev.ungrab()
                    except Exception:
                        pass
                    try:
                        dev.close()
                    except Exception:
                        pass
                    devices.pop(fd, None)

    except KeyboardInterrupt:
        print("\n[*] Stopping Keyboard Forwarder...")
    finally:
        for kc in list(held_keycodes):
            try:
                Xlib.ext.xtest.fake_input(display, Xlib.X.KeyRelease, kc)
                display.sync()
            except Exception:
                pass
        for fd, dev in list(devices.items()):
            try:
                dev.ungrab()
            except Exception:
                pass
            try:
                dev.close()
            except Exception:
                pass


if __name__ == "__main__":
    main()
