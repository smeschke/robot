import vosk
import sounddevice as sd
import json
import serial
import threading
import time
import re
from word2number import w2n

MODEL_PATH  = "/home/ubuntu/vosk-model-small-en-us-0.15"
DEVICE      = 0
SAMPLERATE  = 16000
SERIAL_PORT = "/dev/ttyUSB0"
BAUD_RATE   = 115200

# x,y,b values  (2048 = neutral center)
NEUTRAL    = "2048,2048,0"

# Speed level: 0.0 (stopped) to 1.0 (full speed)
SPEED_LEVEL = 1.0

def make_cmd(x_offset: int, y_offset: int) -> str:
    """Build a joystick command string scaled by SPEED_LEVEL.
    x_offset / y_offset are the signed deviation from neutral (2048)
    at full speed, e.g. +2047 or -2048.
    """
    x = int(2048 + x_offset * SPEED_LEVEL)
    y = int(2048 + y_offset * SPEED_LEVEL)
    x = max(0, min(4095, x))
    y = max(0, min(4095, y))
    return f"{x},{y},0"

def TURN_LEFT()  -> str: return make_cmd(-2048,     0)
def TURN_RIGHT() -> str: return make_cmd(+2047,     0)
def FORWARD()    -> str: return make_cmd(    0, +2047)
def BACKWARD()   -> str: return make_cmd(    0, -2048)

print("Opening serial port...")
ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
time.sleep(2)
print(f"Serial open: {SERIAL_PORT} @ {BAUD_RATE}")

def send_cmd(cmd: str):
    ser.write((cmd + "\n").encode())

def send_neutral():
    send_cmd(NEUTRAL)

stop_event = threading.Event()
hold_thread = None

def hold_then_stop(cmd_fn, duration_ms: int):
    """Repeatedly send cmd_fn() every 100ms for duration_ms, then send neutral.
    cmd_fn is called each iteration so speed changes apply mid-move."""
    global hold_thread
    # stop any running hold
    stop_event.set()
    if hold_thread is not None:
        hold_thread.join()
    stop_event.clear()

    def _run():
        end = time.time() + duration_ms / 1000.0
        while not stop_event.is_set() and time.time() < end:
            send_cmd(cmd_fn())
            time.sleep(0.1)
        send_neutral()

    hold_thread = threading.Thread(target=_run, daemon=True)
    hold_thread.start()

SECONDS_PER_45_DEG = 1.0   # calibrate this if turns are over/under
MS_PER_FOOT        = 250   # 1000ms = 4 feet

def degrees_to_ms(degrees: float) -> int:
    return int((degrees / 45.0) * SECONDS_PER_45_DEG * 1000)

def feet_to_ms(feet: float) -> int:
    return int(feet * MS_PER_FOOT)

def parse_degrees(text_after_command: str):
    """Extract a degree value from the trailing words/digits in text."""
    text = text_after_command.strip()
    # try plain digits first  e.g. "90"
    m = re.search(r'\b(\d+)\b', text)
    if m:
        return float(m.group(1))
    # try word numbers  e.g. "ninety"
    try:
        return float(w2n.word_to_num(text))
    except ValueError:
        return None

# Turn commands (degrees): phrase -> (cmd_fn, label)
TURN_COMMANDS = {
    "left":  (TURN_LEFT,  "LEFT"),
    "right": (TURN_RIGHT, "RIGHT"),
}

# Distance commands (feet): phrase -> (cmd_fn, label)
DISTANCE_COMMANDS = {
    "forward":   (FORWARD,  "FORWARD"),
    "backwards": (BACKWARD, "BACKWARDS"),
    "backward":  (BACKWARD, "BACKWARD"),
}

print("Loading voice model...")
model      = vosk.Model(MODEL_PATH)
recognizer = vosk.KaldiRecognizer(model, SAMPLERATE)

print("Ready — speak now!")

with sd.RawInputStream(samplerate=SAMPLERATE, blocksize=4000,
                       device=DEVICE, dtype='int16',
                       channels=1) as stream:
    while True:
        data, _ = stream.read(4000)

        if recognizer.AcceptWaveform(bytes(data)):
            result = json.loads(recognizer.Result())
            text   = result.get("text", "").strip()

            if not text:
                continue

            print(f"heard: {text}")

            matched = False

            # Speed command: "speed <0-100>" or "speed slow/medium/fast"
            if "speed" in text:
                after = text[text.index("speed") + len("speed"):].strip()
                speed_words = {"slow": 0.25, "low": 0.25,
                               "medium": 0.5, "half": 0.5,
                               "fast": 0.75, "high": 0.75,
                               "full": 1.0,  "max": 1.0}
                new_speed = None
                for word, val in speed_words.items():
                    if word in after:
                        new_speed = val
                        break
                if new_speed is None:
                    num = parse_degrees(after)
                    if num is not None:
                        new_speed = max(0.0, min(1.0, num / 100.0))
                if new_speed is not None:
                    SPEED_LEVEL = new_speed
                    print(f"\n>>> SPEED set to {int(SPEED_LEVEL * 100)}% <<<\n")
                    matched = True

            if not matched:
                for phrase, (cmd_fn, label) in TURN_COMMANDS.items():
                    if phrase in text:
                        after   = text[text.index(phrase) + len(phrase):]
                        degrees = parse_degrees(after)
                        if degrees is not None:
                            duration_ms = degrees_to_ms(degrees)
                            print(f"\n>>> {label} {degrees:.0f} deg ({duration_ms}ms) <<<\n")
                            hold_then_stop(cmd_fn, duration_ms)
                        else:
                            print(f"\n>>> {label} (no degrees heard) <<<\n")
                        matched = True
                        break

            if not matched:
                for phrase, (cmd_fn, label) in DISTANCE_COMMANDS.items():
                    if phrase in text:
                        after = text[text.index(phrase) + len(phrase):]
                        feet  = parse_degrees(after)   # same number parser
                        if feet is not None:
                            duration_ms = feet_to_ms(feet)
                            print(f"\n>>> {label} {feet:.0f} ft ({duration_ms}ms) <<<\n")
                            hold_then_stop(cmd_fn, duration_ms)
                        else:
                            print(f"\n>>> {label} (no distance heard) <<<\n")
                        matched = True
                        break
