#!/bin/zsh
# Catch the clock during a brief wake, hold it in download mode, flash the current build.
cd "$(dirname "$0")/../firmware"; . ~/esp/esp-idf/export.sh >/dev/null 2>&1
python - <<'PY'
import time, subprocess, sys
from serial.tools.list_ports import comports
t=time.time()
while time.time()-t < 200:
    p=[c.device for c in comports() if c.vid==0x303A]
    if p:
        r=subprocess.run([sys.executable,"-m","esptool","--chip","esp32c6","-p",p[0],"--after","no_reset","--connect-attempts","2","chip_id"],capture_output=True,text=True,timeout=40)
        if "Chip is" in r.stdout: print("caught at %d s"%(time.time()-t)); sys.exit(0)
    time.sleep(0.05)
print("never caught the clock awake"); sys.exit(1)
PY
[ $? -eq 0 ] || exit 1
P=$(python -c "from serial.tools.list_ports import comports;print(next((c.device for c in comports() if c.vid==0x303A),''))")
idf.py -p "$P" flash 2>&1 | grep -E "Hard resetting|rror" | tail -2
