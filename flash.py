# -*- coding: utf-8 -*-
"""M5Claw flash: scan port -> select -> erase -> build -> upload"""
import subprocess
import sys
import os
import re

def main():
    project_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(project_dir)

    print()
    print("========================================")
    print("  M5Claw 一键刷机")
    print("========================================")
    print()

    # 1. Scan ports
    print("[1/4] 扫描串口...")
    try:
        raw = subprocess.check_output(["pio", "device", "list"])
        try:
            out = raw.decode("gbk")
        except UnicodeDecodeError:
            out = raw.decode("utf-8", errors="replace")
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print("错误: 无法执行 pio，请确保已安装 PlatformIO")
        sys.exit(1)

    ports = sorted(set(re.findall(r"^COM\d+", out, re.MULTILINE)))
    if not ports:
        print("未检测到 COM 端口，请连接设备")
        sys.exit(1)

    # 2. User select
    print()
    print("检测到以下串口:")
    for i, p in enumerate(ports, 1):
        print(f"  [{i}] {p}")
    print("  [0] 退出")
    print()

    while True:
        try:
            sel = input(f"请选择端口 (1-{len(ports)}): ").strip()
        except (EOFError, KeyboardInterrupt):
            print("已取消")
            sys.exit(0)
        if sel == "0":
            print("已取消")
            sys.exit(0)
        try:
            idx = int(sel)
            if 1 <= idx <= len(ports):
                port = ports[idx - 1]
                break
        except ValueError:
            pass
        print("无效输入，请重试")

    print()
    print(f"已选择: {port}")
    print()

    # 3. Erase
    print("[2/4] 清除旧固件...")
    if subprocess.run(["pio", "pkg", "exec", "--", "esptool.py", "--port", port, "erase_flash"]).returncode != 0:
        print("错误: 清除失败。按住 G0，按 RST，松开 G0 进入下载模式")
        sys.exit(1)
    print("清除完成")
    print()

    # 4. Build
    print("[3/4] 编译中...")
    if subprocess.run(["pio", "run"]).returncode != 0:
        print("错误: 编译失败")
        sys.exit(1)
    print("编译完成")
    print()

    # 5. Upload
    print(f"[4/4] 刷入固件到 {port} ...")
    if subprocess.run(["pio", "run", "-t", "upload", "--upload-port", port]).returncode != 0:
        print("错误: 固件上传失败")
        sys.exit(1)
    print("固件刷入完成")

    print()
    print("刷入 SPIFFS 数据...")
    if subprocess.run(["pio", "run", "-t", "uploadfs", "--upload-port", port]).returncode != 0:
        print("警告: SPIFFS 上传失败")
    else:
        print("SPIFFS 刷入完成")

    print()
    print("========================================")
    print("  刷机完成！请拔掉 USB 重新上电或按 RST 重启")
    print("========================================")
    print()

if __name__ == "__main__":
    main()
