Import("env")
import configparser
import os


def quote_build_flag(value):
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'\\"{escaped}\\"'


project_dir = env.subst("$PROJECT_DIR")
config_file = os.path.join(project_dir, "user_config.ini")

if not os.path.exists(config_file):
    print("[M5Claw] No user_config.ini found, skipping build-time config")
else:
    print(f"[M5Claw] Loading config from {config_file}")
    cp = configparser.ConfigParser()
    cp.read(config_file, encoding="utf-8")

    mapping = {
        "wifi_ssid": "USER_WIFI_SSID",
        "wifi_pass": "USER_WIFI_PASS",
        "mimo_api_key": "USER_MIMO_KEY",
        "mimo_model": "USER_MIMO_MODEL",
        "city": "USER_CITY",
        "wechat_token": "USER_WECHAT_TOKEN",
        "wechat_api_host": "USER_WECHAT_HOST",
    }

    flags = []
    for ini_key, macro_name in mapping.items():
        val = cp.get("user", ini_key, fallback="").strip()
        if val:
            flags.append(f"-D{macro_name}={quote_build_flag(val)}")
            display = f"[{len(val)} chars]" if ("key" in ini_key or "token" in ini_key or "pass" in ini_key) else val
            print(f"  {ini_key} = {display}")

    if flags:
        env.Append(BUILD_FLAGS=flags)
        print(f"[M5Claw] Injected {len(flags)} config values")
