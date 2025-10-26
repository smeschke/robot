import serial
import pynmea2
import smbus2
import math
import time

# -------------------------------
# GPS SETUP
# -------------------------------
GPS_PORT = '/dev/serial0'
GPS_BAUD = 115200

ser = serial.Serial(GPS_PORT, GPS_BAUD, timeout=1)

# -------------------------------
# COMPASS SETUP (QMC5883L)
# -------------------------------
I2C_ADDR = 0x0D  # I2C address for QMC5883L
bus = smbus2.SMBus(1)

# Configure the compass: Continuous mode, 200Hz, 2G range
bus.write_byte_data(I2C_ADDR, 0x0B, 0x01)  # Set reset period
bus.write_byte_data(I2C_ADDR, 0x09, 0x1D)  # 0x1D = 200Hz, continuous mode, 2G

def read_compass_heading():
    """Reads magnetometer data and returns heading in degrees."""
    try:
        data = bus.read_i2c_block_data(I2C_ADDR, 0x00, 6)
        x = (data[1] << 8) | data[0]
        y = (data[3] << 8) | data[2]
        z = (data[5] << 8) | data[4]

        # Handle two's complement
        if x > 32767:
            x -= 65536
        if y > 32767:
            y -= 65536
        if z > 32767:
            z -= 65536

        heading_rad = math.atan2(y, x)
        heading_deg = math.degrees(heading_rad)
        if heading_deg < 0:
            heading_deg += 360
        return heading_deg
    except Exception:
        return None

# -------------------------------
# MAIN LOOP
# -------------------------------
print("GPS + Compass reader started...\n")

while True:
    line = ser.readline().decode(errors="ignore").strip()
    if line.startswith("$GNGGA"):
        try:
            msg = pynmea2.parse(line)
            heading = read_compass_heading()
            heading_str = f"{heading:.1f} deg" if heading is not None else "N/A"

            print(
                f"Lat: {msg.latitude:.6f}, Lon: {msg.longitude:.6f}, "
                f"Alt: {msg.altitude} m, Sats: {msg.num_sats}, "
                f"Heading: {heading_str}"
            )
        except pynmea2.ParseError:
            pass
    time.sleep(0.2)
