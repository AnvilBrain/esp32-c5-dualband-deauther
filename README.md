# ESP32-C5 DualBand Deauther

**⚠️ Educational Use Only - Network Security Testing Tool**

A dual-band (2.4GHz/5GHz) network testing tool for the ESP32-C5 microcontroller. This project is intended strictly for authorized security testing and educational purposes.

---

## 🚀 Quick Start

### Prerequisites

- ESP-IDF v5.5.1
- ESP32-C5 development board
- USB cable for flashing

### Installation

**1. Install ESP-IDF (v5.5.1)**

```bash
git clone --recursive --branch v5.5.1 https://github.com/espressif/esp-idf.git
cd esp-idf
install.bat
export.bat
```

**2. Patch WiFi Library**

Navigate to the ESP32-C5 WiFi library directory:

```bash
cd components\esp_wifi\lib\esp32c5
```

- Delete the existing `libnet80211.a` file
- Replace it with the patched version from the `patched_libnet` folder in this repository
- go back to esp-idf folder

**3. Build and Flash**

```bash
git clone https://github.com/AnvilBrain/esp32-c5-dualband-deauther
cd esp32-c5-dualband-deauther
cd safe_hack_demo_esp32c5
rmdir /s /q C:\Users\user\esp-idf\esp32-c5-dualband-deauther\safe_hack_demo_esp32c5\managed_components\espressif__cjson
idf.py fullclean
idf.py build
idf.py -p COM3 flash monitor
```

> **Note:** Replace `COM3` with your actual serial port (e.g., `/dev/ttyUSB0` on Linux, `/dev/cu.usbserial` on macOS)

password:2444666668888888

---

## ✨ Features

- 🌐 **Dual-band Support** - Operates on both 2.4GHz and 5GHz frequencies
- 📡 **Management Frame Testing** - Analyze and test 802.11 management frames
- 🔓 **SDK Restrictions Bypass** - Pre-patched libraries included
- 💻 **Serial Control Interface** - Easy interaction via terminal
- 🎯 **Ready-to-use** - No additional compilation required

---

## ⚠️ Legal Disclaimer

**READ CAREFULLY BEFORE USE:**

- ✅ **Authorized testing ONLY** - Use exclusively on networks you own or have explicit written permission to test
- 🚫 **Illegal use is prohibited** - Unauthorized network interference is a crime in most jurisdictions
- ⚖️ **You are solely responsible** - The developers assume no liability for misuse of this tool
- 📚 **Educational purpose** - This tool is designed for learning network security concepts

---

## 📦 Project Status

**🔒 PROJECT NO LONGER MAINTAINED**

This project is provided "as-is" and is no longer actively maintained:

- ❌ **No issue support** - Issues will not be addressed or closed
- 🤖 **Self-help approach** - If something breaks, please consult AI assistants (ChatGPT, Claude, etc.) for troubleshooting
- ✅ **Stable foundation** - The project should work as documented; if issues arise, they're likely environment-specific
- 🚀 **New horizons** - The developer is working on more advanced projects

**If you encounter problems:**
1. Double-check your setup matches the prerequisites
2. Ask an AI assistant for help with error messages
3. Search existing issues (though they won't be answered)
4. Fork and fix it yourself - PRs may still be considered

Thank you for your understanding!

---

## 📝 License

This project is provided for educational and authorized testing purposes only.

---

## 🤝 Contributing

Contributions are welcome! Please ensure all contributions maintain the educational focus and legal compliance of this project.

---

**Remember: With great power comes great responsibility. Use wisely and legally.**