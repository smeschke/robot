#!/usr/bin/env python3
from http.server import BaseHTTPRequestHandler, HTTPServer
import serial, os

SER_DEV = os.environ.get("SER_DEV", "/dev/ttyUSB0")
BAUD = 115200
ser = serial.Serial(SER_DEV, BAUD, timeout=0.2)

HTML = """<!doctype html>
<title>Motor Test</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:system-ui;margin:24px;text-align:center}
.grid{display:inline-grid;grid-template-columns:repeat(3,120px);grid-gap:14px;align-items:center;justify-items:center}
button{width:120px;height:70px;font-size:1.1rem;border:none;border-radius:12px;color:#fff;cursor:pointer}
.fwd{background:#2e7d32}.back{background:#c62828}
.left{background:#1976d2}.right{background:#6a1b9a}.stop{background:#444}
.small{font-size:.9rem;margin-top:10px}
</style>
<h1>Motor Test (Hold to Move)</h1>
<div class="grid">
  <div></div><button id="F" class="fwd">Forward</button><div></div>
  <button id="L" class="left">Left</button><button id="S" class="stop">Stop</button><button id="R" class="right">Right</button>
  <div></div><button id="B" class="back">Back</button><div></div>
</div>
<div class="small">Speed keys: 3..7 (30–70%)</div>
<script>
async function send(p){ fetch("/"+p, {cache:"no-store"}); }
["F","B","L","R"].forEach(id=>{
  const b=document.getElementById(id);
  const down=()=>send(id), up=()=>send("S");
  b.addEventListener("mousedown", down);
  b.addEventListener("mouseup", up);
  b.addEventListener("mouseleave", up);
  b.addEventListener("touchstart", e=>{e.preventDefault(); down();}, {passive:false});
  b.addEventListener("touchend",   e=>{e.preventDefault(); up();},   {passive:false});
});
document.getElementById("S").onclick = ()=>send("S");
document.addEventListener("keydown", e=>{ if(e.key>='0'&&e.key<='9') send(e.key); });
</script>
"""

ROUTES = {
  "/F":"F","/B":"B","/L":"L","/R":"R","/S":"S",
  "/0":"0","/1":"1","/2":"2","/3":"3","/4":"4",
  "/5":"5","/6":"6","/7":"7","/8":"8","/9":"9"
}

def tx(ch): ser.write(ch.encode()); print("sent:", ch)

class H(BaseHTTPRequestHandler):
    def do_GET(self):
        # simple request log so you can see paths even if unmapped
        print("req:", self.path)
        if self.path in ("/", "/index.html"):
            return self._send(200, HTML, "text/html")
        if self.path in ROUTES:
            tx(ROUTES[self.path]); return self._send(200, "OK", "text/plain")
        self._send(404, "Not found", "text/plain")
    def log_message(self, *args): return
    def _send(self, code, body, ctype):
        body=body.encode(); self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers(); self.wfile.write(body)

if __name__=="__main__":
    print("Serving on :8000, talking to", SER_DEV)
    HTTPServer(("0.0.0.0", 8000), H).serve_forever()

