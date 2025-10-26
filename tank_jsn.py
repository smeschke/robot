#!/usr/bin/env python3
from http.server import BaseHTTPRequestHandler, HTTPServer
import os, json, csv, time, uuid, signal, threading
from pathlib import Path

# ====== Serial (kept identical to your current setup) ======
import serial
SER_DEV = os.environ.get("SER_DEV", "/dev/ttyUSB0")
BAUD = 115200
ser = serial.Serial(SER_DEV, BAUD, timeout=0.2)
latest_ultrasonic = {"L": None, "C": None, "R": None}
# ====== Session + logging setup ======
START_MONO = time.monotonic()
START_TS = time.time()
SESSION_ID = uuid.uuid4().hex[:12]
RUN_DIR = Path("runlogs") / time.strftime("%Y-%m-%d") / f"{int(START_TS)}_{SESSION_ID}"
RUN_DIR.mkdir(parents=True, exist_ok=True)

EVENTS_PATH   = RUN_DIR / "events.jsonl"   # all events (requests, commands, errors, heartbeats)
COMMANDS_CSV  = RUN_DIR / "commands.csv"   # only sent drive/speed commands
SESSION_META  = RUN_DIR / "session.json"   # static info about this run
HTML = """<!doctype html>
<title>Motor Test</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:system-ui;margin:24px;text-align:center}
.grid{display:inline-grid;grid-template-columns:repeat(3,120px);grid-gap:14px;align-items:center;justify-items:center}
button{width:120px;height:70px;font-size:1.1rem;border:none;border-radius:12px;color:#fff;cursor:pointer}
.fwd{background:#2e7d32}
.back{background:#b23a48}
.speedbtn{width:60px;height:60px;background:#555;border:none;border-radius:50%;color:#fff;font-size:1.1rem;margin:6px}
.left{background:#1976d2}.right{background:#6a1b9a}
.small{font-size:.9rem;margin-top:10px}
pre{display:inline-block;text-align:left;background:#111;color:#eee;padding:12px;border-radius:10px}
</style>
<h1>Motor Test (Hold to Move)</h1>
<div class="grid">
  <div></div><button id="F" class="fwd">Forward</button><div></div>
  <button id="L" class="left">Left</button><div></div><button id="R" class="right">Right</button>
  <div></div><button id="B" class="back">Backup</button><div></div>
</div>
<div class="small">Speed keys: 3..7 (30?70%)</div>
<h2>Speed</h2>
<div>
  <button class="speedbtn" onclick="send('3')">3</button>
  <button class="speedbtn" onclick="send('4')">4</button>
  <button class="speedbtn" onclick="send('5')">5</button>
  <button class="speedbtn" onclick="send('6')">6</button>
  <button class="speedbtn" onclick="send('7')">7</button>
</div>
<p><a href="/metrics">Metrics</a> | <a href="/events.tail">Tail events</a></p>
<script>
async function send(p){ fetch("/"+p, {cache:"no-store"}); }

// Multi-press/multi-touch support
const pressed = new Set();
const ids = ["F","L","R","B"];

function recompute(){
  let code = "S";
  const hasF = pressed.has("F");
  const hasL = pressed.has("L");
  const hasR = pressed.has("R");
  const hasB = pressed.has("B");

  if (!hasF && hasB) { code = "B"; }
  else if (hasF && hasL && !hasR) code = "X";        // U-turn left
  else if (hasF && hasR && !hasL) code = "Y";        // U-turn right
  else if (hasF && !hasL && !hasR) code = "F";
  else if (!hasF && hasL && !hasR) code = "L";       // spin left
  else if (!hasF && hasR && !hasL) code = "R";       // spin right
  else if (!hasF && hasL && hasR) code = "S";        // L+R -> Stop
  else if (hasF && hasL && hasR) code = "F";         // fallback
  send(code);
}
function bindButton(id){
  const b = document.getElementById(id);
  const down = ()=>{ pressed.add(id); recompute(); };
  const up   = ()=>{ pressed.delete(id); recompute(); };
  b.addEventListener("mousedown", down);
  b.addEventListener("mouseup", up);
  b.addEventListener("mouseleave", up);
  b.addEventListener("touchstart", e=>{e.preventDefault(); down();}, {passive:false});
  b.addEventListener("touchend",   e=>{e.preventDefault(); up();},   {passive:false});
}
ids.forEach(bindButton);

// keyboard speed keys unchanged
document.addEventListener("keydown", e=>{ if(e.key>='0'&&e.key<='9') send(e.key); });
</script>
"""

ROUTES = {
  "/F":"F","/L":"L","/R":"R","/B":"B","/S":"S","/X":"X","/Y":"Y",
  "/0":"0","/1":"1","/2":"2","/3":"3","/4":"4",
  "/5":"5","/6":"6","/7":"7","/8":"8","/9":"9"
}

# ---------- tiny logging helpers ----------
_lock = threading.Lock()

def uptime_s():
    return round(time.monotonic() - START_MONO, 3)

def now_iso():
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime()) + f".{int((time.time()%1)*1000):03d}Z"

def log_event(kind, **payload):
    rec = {
        "ts": now_iso(),
        "uptime_s": uptime_s(),
        "session": SESSION_ID,
        "kind": kind,
        **payload
    }
    with _lock:
        with EVENTS_PATH.open("a", encoding="utf-8") as f:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")


def log_command_csv(ch):
    is_speed = ch.isdigit()
    row = [now_iso(), uptime_s(), SESSION_ID, ("speed" if is_speed else "drive"), ch]
    header = ["ts","uptime_s","session","type","value"]
    with _lock:
        new = not COMMANDS_CSV.exists()
        with COMMANDS_CSV.open("a", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            if new: w.writerow(header)
            w.writerow(row)

def tx(ch):
    ser.write(ch.encode())
    log_event("tx", command=ch)
    log_command_csv(ch)
import re

_stop_serial = threading.Event()

def _serial_listener():
    pattern = re.compile(r'([LCR]):\s*(\d+)\s*cm')
    latest = {"L": None, "C": None, "R": None}

    while not _stop_serial.is_set():
        try:
            if ser.in_waiting:
                line = ser.readline().decode(errors="ignore").strip()
                m = pattern.findall(line)
                if m:
                    for label, value in m:
                        latest[label] = int(value)
                    global latest_ultrasonic
                    latest_ultrasonic.update(latest)
                    log_event("ultrasonic", data=latest.copy())
        except Exception as e:
            log_event("error", where="serial_listener", msg=str(e))
        time.sleep(0.05)
# ---------- heartbeat thread ----------
_stop_hb = threading.Event()
def _heartbeat():
    while not _stop_hb.is_set():
        log_event("heartbeat", ser_dev=SER_DEV, baud=BAUD)
        _stop_hb.wait(5.0)  # every 5s

# ---------- HTTP handler ----------
class H(BaseHTTPRequestHandler):
    def do_GET(self):
        
        log_event("http_get", path=self.path, client=self.client_address[0])
        
        if self.path in ("/", "/index.html"):
            return self._send(200, HTML, "text/html")
        if self.path == "/metrics":
            body = {
                "session": SESSION_ID,
                "uptime_s": uptime_s(),
                "ser_dev": SER_DEV,
                "baud": BAUD,
                "run_dir": str(RUN_DIR),
                "events_path": str(EVENTS_PATH),
                "commands_csv": str(COMMANDS_CSV),
                "ultrasonic_cm": latest_ultrasonic,
                "start_ts": START_TS
                
            }
            
            pretty = "<h1>Metrics</h1><pre>"+json.dumps(body, indent=2)+"</pre>"
            return self._send(200, pretty, "text/html")
        if self.path == "/metrics.json":
            body = {
                "session": SESSION_ID,
                "uptime_s": uptime_s(),
                "ser_dev": SER_DEV,
                "baud": BAUD,
                "run_dir": str(RUN_DIR),
                "ultrasonic_cm": latest_ultrasonic,
                "start_ts": START_TS
            }
            return self._send(200, json.dumps(body), "application/json")
        if self.path == "/events.tail":
            # Return last ~2KB of the events file for quick peeks in browser
            try:
                data = ""
                with EVENTS_PATH.open("rb") as f:
                    f.seek(0, os.SEEK_END)
                    size = f.tell()
                    f.seek(max(size-2048,0), os.SEEK_SET)
                    data = f.read().decode("utf-8","ignore")
                return self._send(200, "<pre>"+data+"</pre>", "text/html")
            except Exception as e:
                log_event("error", where="events.tail", msg=str(e))
                return self._send(500, "error", "text/plain")
        if self.path in ROUTES:
            ch = ROUTES[self.path]
            tx(ch)
            return self._send(200, "OK", "text/plain")
        return self._send(404, "Not found", "text/plain")


    def do_POST(self):
        # Ingest sensor data: POST /ingest/<topic>  with JSON body
        if self.path.startswith("/ingest/"):
            topic = self.path.split("/", 2)[-1]
            length = int(self.headers.get("Content-Length","0") or 0)
            body = self.rfile.read(length) if length>0 else b"{}"
            try:
                payload = json.loads(body.decode("utf-8") or "{}")
            except Exception:
                payload = {"_raw": body.decode("utf-8","ignore")}
            log_event("ingest", topic=topic, data=payload)
            return self._send(200, "OK", "text/plain")
        return self._send(404, "Not found", "text/plain")

    def log_message(self, *args): 
        # Silence default stdout logs (we log ourselves)
        return

    def _send(self, code, body, ctype):
        if not isinstance(body, (bytes, bytearray)):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

# ---------- graceful startup/shutdown ----------
def _write_session_meta():
    meta = {
        "session": SESSION_ID,
        "start_ts": START_TS,
        "ser_dev": SER_DEV,
        "baud": BAUD,
        "run_dir": str(RUN_DIR),
        "hostname": os.uname().nodename if hasattr(os, "uname") else ""
    }
    with SESSION_META.open("w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)
    log_event("session_start", **meta)

def _shutdown(*_):
    log_event("session_end", uptime_s=uptime_s())
    _stop_hb.set()
    _stop_serial.set()
    try: ser.close()
    except: pass
    os._exit(0)

if __name__=="__main__":
    threading.Thread(target=_serial_listener, daemon=True).start()
    _write_session_meta()
    signal.signal(signal.SIGINT, _shutdown)
    signal.signal(signal.SIGTERM, _shutdown)
    threading.Thread(target=_heartbeat, daemon=True).start()
    print(f"Serving on :8000, talking to {SER_DEV}\nLogs in {RUN_DIR}")
    log_event("http_start", host="0.0.0.0", port=8000)
    HTTPServer(("0.0.0.0", 8000), H).serve_forever()
