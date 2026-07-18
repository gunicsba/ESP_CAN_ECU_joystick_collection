import serial
import time
import sys

while True:
    try:
        ser = serial.Serial('COM6', 115200, timeout=1)
        print("--- Connected ---", flush=True)
        start = time.time()
        while time.time() - start < 20:
            try:
                data = ser.read(512)
                if data:
                    sys.stdout.buffer.write(data)
                    sys.stdout.flush()
            except Exception as e:
                print(f"--- Read error: {e} ---", flush=True)
                break
        ser.close()
    except Exception as e:
        print(f"--- Waiting for port: {e} ---", flush=True)
    time.sleep(1)
