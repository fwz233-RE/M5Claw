Import("env")
import configparser
import json
import os

project_dir = env.subst("$PROJECT_DIR")
config_file = os.path.join(env.subst("$PROJECT_DIR"), "user_config.ini")
bootstrap_file = os.path.join(project_dir, "data", "config", "BOOTSTRAP.json")

mapping = {
    "wifi_ssid": "wifi_ssid",
    "wifi_pass": "wifi_pass",
    "mimo_api_key": "mimo_api_key",
    "mimo_model": "mimo_model",
    "city": "city",
    "wechat_token": "wechat_token",
    "wechat_api_host": "wechat_api_host",
}

if not os.path.exists(config_file):
    if os.path.exists(bootstrap_file):
        os.remove(bootstrap_file)
        print("[M5Claw] Removed stale bootstrap config")
    print("[M5Claw] No user_config.ini found, skipping bootstrap config")
else:
    print(f"[M5Claw] Loading config from {config_file}")
    cp = configparser.ConfigParser()
    cp.read(config_file, encoding="utf-8")

    payload = {}
    for ini_key, json_key in mapping.items():
        val = cp.get("user", ini_key, fallback="").strip()
        if val:
            payload[json_key] = val
            print(f"  prepared {ini_key}")

    if "mimo_api_key" not in payload:
        legacy_key = cp.get("user", "llm_api_key", fallback="").strip()
        if legacy_key:
            payload["mimo_api_key"] = legacy_key
            print("  prepared llm_api_key -> mimo_api_key")

    if "mimo_model" not in payload:
        legacy_model = cp.get("user", "llm_model", fallback="").strip()
        if legacy_model:
            payload["mimo_model"] = legacy_model
            print("  prepared llm_model -> mimo_model")

    if payload:
        os.makedirs(os.path.dirname(bootstrap_file), exist_ok=True)
        with open(bootstrap_file, "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print(f"[M5Claw] Wrote bootstrap config with {len(payload)} values")
    elif os.path.exists(bootstrap_file):
        os.remove(bootstrap_file)
        print("[M5Claw] Removed empty bootstrap config")
