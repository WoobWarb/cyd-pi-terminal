import asyncio
import logging
import traceback
import websockets
import mss
import cv2
import numpy as np
import struct
from pynput.mouse import Controller, Button

# ==========================================
# CONFIGURATION
# ==========================================
HOST = "0.0.0.0"
PORT = 8080
CYD_W, CYD_H = 320, 240
JPEG_QUALITY = 50
TARGET_FPS = 15

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)

mouse = Controller()

async def handle_client(websocket):
    path = getattr(websocket, "path", None)
    if path is None:
        request = getattr(websocket, "request", None)
        path = getattr(request, "path", None)

    if path != "/screen":
        print(f"[!] Rejected connection: path={path!r} from {websocket.remote_address}")
        return

    print(f"[+] Client Connected: {websocket.remote_address}")

    try:
        sct = mss.MSS()
        monitor = sct.monitors[1]  # หน้าจอหลัก
    except Exception:
        print("[!] Failed to initialize screen capture (mss.MSS()):")
        traceback.print_exc()
        return

    pi_screen_w = monitor['width']
    pi_screen_h = monitor['height']

    prev_gray = None
    force_refresh = True

    # ฟังก์ชันรับคำสั่ง Touch จาก CYD เพื่อคุมเมาส์และรองรับ Touch Swipe Scroll
    async def receive_touch():
        nonlocal force_refresh
        touch_start_y = None
        last_y = None
        is_drag_scrolling = False

        try:
            async for message in websocket:
                if isinstance(message, str):
                    if message == "REFRESH":
                        print(f"[*] Full screen refresh requested by client {websocket.remote_address}")
                        force_refresh = True
                        continue

                    if message.startswith("M:"):
                        try:
                            # รูปแบบข้อความ: M:x,y,state (เช่น M:150,120,1)
                            _, data = message.split(":")
                            tx, ty, state = map(int, data.split(","))

                            map_x = int((tx / CYD_W) * pi_screen_w)
                            map_y = int((ty / CYD_H) * pi_screen_h)

                            if state == 1:  # Touch Down / Drag
                                if touch_start_y is None:
                                    touch_start_y = ty
                                    last_y = ty
                                    is_drag_scrolling = False
                                    mouse.position = (map_x, map_y)
                                else:
                                    dy = ty - last_y
                                    total_dy = ty - touch_start_y

                                    if abs(total_dy) >= 8 or is_drag_scrolling:
                                        is_drag_scrolling = True
                                        if abs(dy) >= 6:
                                            scroll_amount = 1 if dy > 0 else -1
                                            mouse.scroll(0, scroll_amount * 2)
                                            last_y = ty
                                    else:
                                        mouse.position = (map_x, map_y)

                            else:  # Touch Up
                                if not is_drag_scrolling:
                                    mouse.position = (map_x, map_y)
                                    mouse.press(Button.left)
                                    mouse.release(Button.left)

                                touch_start_y = None
                                last_y = None
                                is_drag_scrolling = False

                        except Exception:
                            print(f"[!] Failed to handle touch message {message!r}:")
                            traceback.print_exc()
                else:
                    print(f"[?] Unexpected message from device: {message!r}")
        except websockets.exceptions.ConnectionClosed:
            pass
        except Exception:
            print("[!] receive_touch crashed:")
            traceback.print_exc()
            raise

    # ฟังก์ชันส่งภาพหน้าจอ (Delta Update + Force Full Frame on Connect)
    async def send_screen():
        nonlocal prev_gray, force_refresh
        initial_full_frames = 3  # ส่ง Full frame 3 เฟรมแรกเมื่อต่อใหม่เสมอ เพื่อให้แน่ใจว่าจอ CYD ได้ภาพครบ

        try:
            while True:
                with mss.MSS() as fresh_sct:
                    sct_img = fresh_sct.grab(monitor)
                frame = np.array(sct_img)
                
                frame_resized = cv2.resize(frame, (CYD_W, CYD_H), interpolation=cv2.INTER_NEAREST)
                gray = cv2.cvtColor(frame_resized, cv2.COLOR_BGRA2GRAY)
                
                x, y, w, h = 0, 0, CYD_W, CYD_H
                
                if initial_full_frames > 0 or force_refresh or prev_gray is None:
                    # ส่งภาพเต็มจอ
                    w, h = CYD_W, CYD_H
                    x, y = 0, 0
                    if initial_full_frames > 0:
                        initial_full_frames -= 1
                    force_refresh = False
                else:
                    # ตรวจหาเฉพาะจุดที่ภาพมีการเปลี่ยนแปลง (Image Diff)
                    diff = cv2.absdiff(prev_gray, gray)
                    _, thresh = cv2.threshold(diff, 10, 255, cv2.THRESH_BINARY)
                    
                    coords = cv2.findNonZero(thresh)
                    if coords is not None:
                        x, y, w, h = cv2.boundingRect(coords)
                    else:
                        w, h = 0, 0
                
                if w > 0 and h > 0:
                    crop_img = frame_resized[y:y+h, x:x+w]
                    crop_bgr = cv2.cvtColor(crop_img, cv2.COLOR_BGRA2BGR)
                    
                    encode_param = [int(cv2.IMWRITE_JPEG_QUALITY), JPEG_QUALITY]
                    ret, jpeg = cv2.imencode('.jpg', crop_bgr, encode_param)
                    
                    if ret:
                        header = struct.pack('>HH', x, y)
                        payload = header + jpeg.tobytes()
                        await websocket.send(payload)
                        
                        prev_gray = gray.copy()
                
                await asyncio.sleep(1.0 / TARGET_FPS)
                
        except websockets.exceptions.ConnectionClosed:
            print(f"[-] Client Disconnected: {websocket.remote_address}")
        except Exception:
            print("[!] send_screen crashed:")
            traceback.print_exc()
            raise

    try:
        await asyncio.gather(
            receive_touch(),
            send_screen()
        )
    except Exception:
        print("[!] handle_client crashed:")
        traceback.print_exc()

async def main():
    print(f"[*] Starting Delta Remote Server at ws://{HOST}:{PORT}/screen")
    async with websockets.serve(handle_client, HOST, PORT):
        await asyncio.Future()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[*] Server Stopped.")
