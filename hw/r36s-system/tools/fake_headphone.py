import struct
import time
import os

# /dev/input/event1 (podle tvého výpisu)
DEV_PATH = "/dev/input/event1"

# Konstanty z linux/input.h
EV_SW = 0x05
SW_HEADPHONE_INSERT = 0x02

# Funkce pro zápis události
def write_event(fd, type, code, value):
    # struct input_event: time (16b), type (2b), code (2b), value (4b)
    # Na 64-bit arm (aarch64) může být time delší (24b), zkusíme standardní "llHHi" formát
    # l = long (8 bytes), H = unsigned short (2 bytes), i = int (4 bytes)
    # timestamp, type, code, value
    data = struct.pack('llHHi', 0, 0, type, code, value)
    os.write(fd, data)

try:
    fd = os.open(DEV_PATH, os.O_WRONLY)
    print(f"Injecting HEADPHONE INSERT into {DEV_PATH}...")
    
    # SW_HEADPHONE_INSERT = 1 (Inserted)
    write_event(fd, EV_SW, SW_HEADPHONE_INSERT, 1)
    
    # SYN_REPORT (Aby to systém zpracoval)
    write_event(fd, 0x00, 0x00, 0)
    
    print("Done. Speaker should be mute.")
    os.close(fd)
except Exception as e:
    print(f"Error: {e}")
