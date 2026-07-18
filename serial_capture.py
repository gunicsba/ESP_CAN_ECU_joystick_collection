import serial
import time
import sys

print("Monitoring COM6 for any output...")
all_data = b""
cycle = 0
while True:
    try:
        ser = serial.Serial('COM6', 115200, timeout=0.5)
        cycle += 1
        data = b""
        start = time.time()
        while time.time() - start < 2:
            try:
                b = ser.read(256)
                if b:
                    data += b
            except:
                break
        if data:
            text = data.decode('utf-8', 'replace')
            print(f"\n=== Cycle {cycle} ({len(data)} bytes) ===")
            print(text)
            all_data += data
        ser.close()
    except serial.SerialException:
        pass
    except Exception as e:
        pass
    
    if cycle > 0 and cycle % 20 == 0:
        print(f"  ... {cycle} cycles monitored, {len(all_data)} total bytes")
    
    # Stop after enough cycles if we got data
    if len(all_data) > 0 and cycle > 30:
        print("\n=== All captured data ===")
        print(all_data.decode('utf-8', 'replace'))
        break
    
    time.sleep(0.05)
