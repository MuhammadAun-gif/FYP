import serial
import csv
import time

port = "COM6"
baud = 115200
file_name = "lora_jamming_dataset.csv"

JAM_LABEL = 0

EXPERT_ACTIONS = {
    0: [0, 0, 0],  # Clean     → do nothing
    1: [1, 0, 0],  # Single    → hop frequency
    2: [1, 1, 0],  # Hopping   → hop + change SF
    3: [0, 1, 1],  # Reactive  → change SF + randomize interval
}

HEADER = [
    "timestamp",
    "rssi",
    "pktRSSI",
    "snr",
    "pdr",
    "freqError",
    "interArrival",
    "rssiVariance",
    "snrVariance",
    "jam_label",
    "action_freq_hop",
    "action_sf_change",
    "action_interval_randomize"
]

def is_valid_row(data):
    if len(data) != 9:  
        return False
    try:
        float(data[0])
        return True
    except ValueError:
        return False

def start_logger():
    actions = EXPERT_ACTIONS[JAM_LABEL]
    print(f"Searching for ESP32 on {port}...")
    print(f"JAM_LABEL = {JAM_LABEL} | Actions = {actions}")

    while True:
        try:
            with serial.Serial(port, baud, timeout=1) as ser, \
                 open(file_name, mode='a', newline='') as f:

                writer = csv.writer(f)
                print(f"Connected to {port}! Logging data...")

                while True:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if not line:
                        continue

                    data = line.split(',')

                    if not is_valid_row(data):
                        print(f"Skipped: {data}")
                        continue

                    full_row = data + [JAM_LABEL] + actions

                    writer.writerow(full_row)
                    f.flush()
                    print(f"Saved: {full_row}")

        except (serial.SerialException, FileNotFoundError):
            print("ESP32 disconnected. Retrying in 2 seconds...")
            time.sleep(2)
        except KeyboardInterrupt:
            print("\nStopping logger.")
            break

if __name__ == "__main__":
    try:
        with open(file_name, 'x', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(HEADER)
            print("New file created with header.")
    except FileExistsError:
        print("Appending to existing file.")

    start_logger()
