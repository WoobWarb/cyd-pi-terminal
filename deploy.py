#!/usr/bin/env python3
"""
deploy.py
Script สำหรับอัปโหลดไฟล์ที่แก้ไขไปยัง Raspberry Pi และรีสตาร์ท Service ผ่าน SSH/SFTP
"""

import os
import sys
import paramiko

PI_HOST = "192.168.100.99"
PI_USER = "deathwolf"
REMOTE_DIR = "/home/deathwolf/cyd-screen"

FILES_TO_UPLOAD = [
    "keyboard_forwarder.py",
    "start_cyd_terminal.sh",
    "screen_server.py",
    "cyd-keyboard.service",
    "cyd-screen.service",
]

def safe_print(text):
    try:
        print(text)
    except UnicodeEncodeError:
        print(text.encode('ascii', 'replace').decode('ascii'))

def deploy(password):
    safe_print(f"[*] Connecting to {PI_USER}@{PI_HOST}...")
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    
    try:
        ssh.connect(PI_HOST, username=PI_USER, password=password, timeout=10)
        safe_print("[+] Connected via SSH successfully!")
    except Exception as e:
        safe_print(f"[!] Connection failed: {e}")
        return False

    # ตรวจสอบและสร้างโฟลเดอร์ปลายทาง
    stdin, stdout, stderr = ssh.exec_command(f"mkdir -p {REMOTE_DIR}")
    stdout.channel.recv_exit_status()

    # Upload files ผ่าน SFTP
    sftp = ssh.open_sftp()
    for fname in FILES_TO_UPLOAD:
        if os.path.exists(fname):
            local_path = os.path.abspath(fname)
            remote_path = f"{REMOTE_DIR}/{fname}"
            safe_print(f"[+] Uploading {fname} -> {remote_path}")
            sftp.put(local_path, remote_path)
    sftp.close()

    # ตั้งค่า execute permission
    safe_print("[*] Setting file permissions...")
    ssh.exec_command(f"chmod +x {REMOTE_DIR}/start_cyd_terminal.sh")

    # ติดตั้ง packages ที่จำเป็น
    safe_print("[*] Ensuring required packages are installed (xvfb, xterm, matchbox-window-manager, xdotool)...")
    cmd_install = f"echo {password} | sudo -S apt update && echo {password} | sudo -S apt install -y xvfb xterm matchbox-window-manager xdotool"
    stdin, stdout, stderr = ssh.exec_command(cmd_install)
    stdout.channel.recv_exit_status()

    # อัปเดต systemd service
    safe_print("[*] Installing & Restarting systemd services...")
    cmd_service = f"""
    echo {password} | sudo -S cp {REMOTE_DIR}/cyd-screen.service /etc/systemd/system/
    echo {password} | sudo -S cp {REMOTE_DIR}/cyd-keyboard.service /etc/systemd/system/
    echo {password} | sudo -S systemctl daemon-reload
    echo {password} | sudo -S systemctl enable cyd-screen.service cyd-keyboard.service
    echo {password} | sudo -S systemctl restart cyd-screen.service
    echo {password} | sudo -S systemctl restart cyd-keyboard.service
    """
    stdin, stdout, stderr = ssh.exec_command(cmd_service)
    stdout.channel.recv_exit_status()

    # เช็คสถานะ service
    safe_print("\n" + "="*50)
    safe_print("[*] Service Status:")
    safe_print("="*50)
    stdin, stdout, stderr = ssh.exec_command(f"echo {password} | sudo -S systemctl status cyd-keyboard.service --no-pager")
    safe_print(stdout.read().decode('utf-8', errors='ignore'))

    safe_print("\n" + "="*50)
    safe_print("[*] Live Logs (cyd-keyboard.service):")
    safe_print("="*50)
    stdin, stdout, stderr = ssh.exec_command(f"echo {password} | sudo -S journalctl -u cyd-keyboard.service -n 15 --no-pager")
    safe_print(stdout.read().decode('utf-8', errors='ignore'))

    ssh.close()
    safe_print("[+] Deployment completed successfully!")
    return True

if __name__ == "__main__":
    if len(sys.argv) > 1:
        pwd = sys.argv[1]
    else:
        import getpass
        pwd = getpass.getpass(f"Enter SSH password for {PI_USER}@{PI_HOST}: ")
    deploy(pwd)
