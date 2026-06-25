import serial
import time
import matplotlib.pyplot as plt
from serial.tools import list_ports

# =========================
# AUTO DETECT STM32
# =========================
def find_stm32_port():
    ports = list_ports.comports()

    for p in ports:
        desc = p.description.lower()
        if any(x in desc for x in ["stm", "stmicro", "usb serial", "virtual com"]):
            print("Found STM32:", p.device)
            return p.device
    return None


port = find_stm32_port()
if port is None:
    raise Exception("STM32 not found")

ser = serial.Serial(port, 115200, timeout=1)

# =========================
# STORAGE
# =========================
data = {
    "t1": [],
    "t2": [],
    "v": [],
    "p": [],
    "t": []
}

last_values = {}
last_console_update = 0

THRESH = {
    "t1": 1.0,   # 1°C filter (important)
    "t2": 1.0,
    "v": 50,
    "p": 0.1,
    "t": 1.0
}

CONSOLE_REFRESH = 0.5  # seconds

# =========================
# PLOT (2 PANELS)
# =========================
plt.ion()
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

# =========================
# PARSER
# =========================
def parse(line):
    line = line.strip()

    if not line.startswith("DATA"):
        return None

    try:
        _, t1, t2, v, p, t = line.split(",")

        return {
            "t1": float(t1),
            "t2": float(t2),
            "v": float(v),
            "p": float(p),
            "t": float(t)
        }
    except:
        return None


# =========================
# LOOP
# =========================
print("Running dashboard...")

while True:
    line = ser.readline().decode(errors="ignore")
    values = parse(line)

    if values is None:
        continue

    updated = False
    now = time.time()

    # =========================
    # FILTER + STORE DATA
    # =========================
    for k, v in values.items():

        if k not in last_values or abs(v - last_values[k]) >= THRESH[k]:
            last_values[k] = v
            updated = True

        data[k].append(v)

        if len(data[k]) > 200:
            data[k].pop(0)

    # =========================
    # LEFT = GRAPHS
    # =========================
    ax1.clear()
    ax1.set_title("STM32 Signals")

    ax1.plot(data["t1"], label="Temp1")
    ax1.plot(data["t2"], label="Temp2")
    ax1.plot(data["t"], label="Tube")

    ax1.legend()

    ax2.clear()
    ax2.set_title("Electrical")

    ax2.plot(data["v"], label="Voltage")
    ax2.plot(data["p"], label="Pressure")

    ax2.legend()

    # =========================
    # RIGHT = CONSOLE (THROTTLED)
    # =========================
    if updated and (now - last_console_update) > CONSOLE_REFRESH:

        last_console_update = now

        print("\033[2J\033[H")  # clear terminal
        print("=== STM32 LIVE DASHBOARD ===\n")

        for k, v in last_values.items():
            print(f"{k:>6}: {v}")

    # =========================
    # REFRESH PLOT
    # =========================
    plt.pause(0.01)