Import("env")
import configparser, os

config_file = os.path.join(env.subst("$PROJECT_DIR"), "user_config.ini")
if not os.path.exists(config_file):
    print("[M5Claw] No user_config.ini found, using environment variables or empty defaults")
else:
    print(f"[M5Claw] Loading config from {config_file}")
    cp = configparser.ConfigParser()
    cp.read(config_file, encoding="utf-8")

    mapping = {
        "wifi_ssid":     "M5CLAW_WIFI_SSID",
        "wifi_pass":     "M5CLAW_WIFI_PASS",
        "llm_api_key":   "M5CLAW_LLM_KEY",
        "llm_provider":  "M5CLAW_LLM_PROVIDER",
        "llm_model":     "M5CLAW_LLM_MODEL",
        "llm_host":      "M5CLAW_LLM_HOST",
        "llm_path":      "M5CLAW_LLM_PATH",
        "dashscope_key": "M5CLAW_DS_KEY",
        "city":          "M5CLAW_CITY",
    }

    for ini_key, env_var in mapping.items():
        val = cp.get("user", ini_key, fallback="").strip()
        if val:
            os.environ[env_var] = val
            display = val[:8] + "..." if len(val) > 12 else val
            print(f"  {ini_key} = {display}")
