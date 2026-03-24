Import("env")
import configparser, os

config_file = os.path.join(env.subst("$PROJECT_DIR"), "user_config.ini")
if not os.path.exists(config_file):
    print("[M5Claw] No user_config.ini found, using defaults")
else:
    print(f"[M5Claw] Loading config from {config_file}")
    cp = configparser.ConfigParser()
    cp.read(config_file, encoding="utf-8")

    mapping = {
        "wifi_ssid":        "USER_WIFI_SSID",
        "wifi_pass":        "USER_WIFI_PASS",
        "llm_api_key":      "USER_LLM_KEY",
        "llm_provider":     "USER_LLM_PROVIDER",
        "llm_model":        "USER_LLM_MODEL",
        "llm_host":         "USER_LLM_HOST",
        "llm_path":         "USER_LLM_PATH",
        "dashscope_key":    "USER_DS_KEY",
        "city":             "USER_CITY",
        "wechat_token":     "USER_WECHAT_TOKEN",
        "wechat_api_host":  "USER_WECHAT_API_HOST",
        "glm_search_key":   "USER_GLM_SEARCH_KEY",
    }

    flags = []
    for ini_key, macro_name in mapping.items():
        val = cp.get("user", ini_key, fallback="").strip()
        if val:
            escaped = val.replace("\\", "\\\\").replace('"', '\\"')
            flags.append(f'-D{macro_name}=\\"{escaped}\\"')
            display = val[:8] + "..." if len(val) > 12 else val
            print(f"  {ini_key} = {display}")

    if flags:
        env.Append(BUILD_FLAGS=flags)
        print(f"[M5Claw] Injected {len(flags)} config values")
