# ESP32-C5 DualBand Deauther

**Educational Use Only - Network Testing Tool**

## ⚡ Quick Flash

1. **Install ESP-IDF (master branch):**
```bash
2. git clone -b master https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh
source export.sh
```

**Flash Only (NO BUILD):**

```bash
git clone https://github.com/yourusername/esp32-c5-dualband-deauther
cd esp32-c5-dualband-deauther
idf.py -p /your/port flash monitor
```

🚨 CRITICAL
DO NOT RUN idf.py build - this will erase pre-applied patches

Flash directly using pre-compiled binaries with patches included

Project is ready-to-use, no compilation needed


🛡️ Built-in Features
Dual-band 2.4GHz/5GHz support

Management frame testing

SDK restrictions bypass (pre-patched)

Serial control interface


⚠️ Important
FOR AUTHORIZED TESTING ONLY

Use only on networks you own

Illegal use is strictly prohibited

You are responsible for your actions
