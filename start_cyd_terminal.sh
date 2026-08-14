#!/bin/bash
# start_cyd_terminal.sh
# รันบน Raspberry Pi (Lite, ไม่มี GUI) เพื่อสร้างจอเสมือน (Xvfb) ขนาด 320x240
# เปิด xterm เต็มจอไว้ในนั้น แล้วรัน screen_server.py ให้จับภาพจากจอเสมือนนี้
#
# การติดตั้งแพ็กเกจที่จำเป็น (รันครั้งเดียวบน Pi):
#   sudo apt update && sudo apt install -y xvfb xterm matchbox-window-manager xdotool
#
# วิธีใช้งาน:
#   chmod +x start_cyd_terminal.sh
#   ./start_cyd_terminal.sh

set -e

VDISPLAY=":1"
RES="320x240x24"

# ดึง directory ของโปรเจกต์อัตโนมัติ (fallback ไปที่ $HOME/cyd-screen)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" 2>/dev/null && pwd || true)"
PROJECT_DIR="${SCRIPT_DIR:-$HOME/cyd-screen}"

XVFB_PID=""
XTERM_PID=""
WM_PID=""
FOCUS_PID=""

cleanup() {
    echo "[*] Cleaning up processes on $VDISPLAY..."
    [ -n "$FOCUS_PID" ] && kill "$FOCUS_PID" 2>/dev/null || true
    [ -n "$XTERM_PID" ] && kill "$XTERM_PID" 2>/dev/null || true
    [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null || true
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "[*] Killing any previous Xvfb/xterm/wm on display $VDISPLAY..."
pkill -f "Xvfb $VDISPLAY" 2>/dev/null || true
pkill -f "xterm.*$VDISPLAY" 2>/dev/null || true
pkill -f "matchbox-window-manager" 2>/dev/null || true
sleep 1

echo "[*] Starting virtual display $VDISPLAY at $RES (with -ac for multi-user access)..."
# ใส่ -ac เพื่อให้ keyboard_forwarder.py (รันด้วย root) สามารถพิมพ์เข้ามาใน Xvfb ได้โดยไม่ติด Permission
Xvfb $VDISPLAY -screen 0 $RES -ac +extension RANDR +extension RENDER +extension GLX -nolisten tcp &
XVFB_PID=$!
sleep 1

# อนุญาตให้ทุก user ส่ง input เข้ามาที่ display นี้ได้
DISPLAY=$VDISPLAY xhost + 2>/dev/null || true

# ตรวจสอบและรัน Window Manager
if command -v matchbox-window-manager >/dev/null 2>&1; then
    echo "[*] Starting matchbox-window-manager (fullscreen & auto-focus)..."
    DISPLAY=$VDISPLAY matchbox-window-manager -use_titlebar no &
    WM_PID=$!
else
    echo "[!] Warning: matchbox-window-manager not found! Starting focus keeper fallback..."
    # Fallback: ใช้ xdotool คอยดึง focus ให้ xterm ตลอดเวลา
    (
        while true; do
            if command -v xdotool >/dev/null 2>&1; then
                DISPLAY=$VDISPLAY xdotool search --class xterm windowactivate windowfocus 2>/dev/null || true
            fi
            sleep 2
        done
    ) &
    FOCUS_PID=$!
fi
sleep 1

# สร้างไฟล์ config สำหรับ bash ให้แสดง prompt แบบ CYD-USERNAME (ตัวพิมพ์ใหญ่ทั้งหมด)
CYD_BASHRC="/tmp/cyd_bash.rc"
cat << 'EOF' > "$CYD_BASHRC"
[ -f ~/.bashrc ] && source ~/.bashrc
# Format: CYD-DEATHWOLF > (ตัวพิมพ์ใหญ่ทั้งหมด)
UPPER_USER=$(echo "${USER:-DEATHWOLF}" | tr '[:lower:]' '[:upper:]')
export PS1="\[\033[1;33m\]CYD-\[\033[1;36m\]${UPPER_USER}\[\033[1;32m\] >\[\033[0m\] "
clear
EOF

echo "[*] Starting xterm in virtual display (with CYD-UPPERCASE prompt & auto-respawn)..."
(
    while true; do
        DISPLAY=$VDISPLAY xterm \
            -fa 'DejaVu Sans Mono' \
            -fs 8 \
            -geometry 53x15+0+0 \
            -bg black \
            -fg green \
            -cr white \
            -bc \
            -b 0 \
            -bw 0 \
            +sb \
            -xrm 'XTerm*allowSendEvents: true' \
            -e /bin/bash --rcfile "$CYD_BASHRC" 2>/dev/null || true
        echo "[*] xterm exited. Respawning in 1s..."
        sleep 1
    done
) &
XTERM_PID=$!
sleep 1

# โฟกัส xterm ให้พร้อมรับ input ทันที
if command -v xdotool >/dev/null 2>&1; then
    DISPLAY=$VDISPLAY xdotool search --class xterm windowactivate windowfocus 2>/dev/null || true
fi

echo "[*] Starting screen_server.py pointed at $VDISPLAY..."
cd "$PROJECT_DIR"
if [ -f "venv/bin/activate" ]; then
    source venv/bin/activate
fi

DISPLAY=$VDISPLAY python screen_server.py
