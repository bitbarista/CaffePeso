#!/usr/bin/env python3
"""
CaffePeso Smart Switch Timing Logger
=====================================
Runs on the CaffeRitmo Pi. Registers webhooks on the real Shelly so it is
notified the instant CaffePeso fires a relay command. Simultaneously subscribes
to CaffeRitmo's SocketIO to track live weight data.

Logs per shot:
  - When the Shelly relay goes OFF (CaffePeso's trigger) — time + weight
  - When CaffeRitmo's pump actually stops (ssrS → False) — time + weight
  - The delta between the two (time and grams)

Setup:
  1. Copy this script to the Pi:
       scp tools/smartswitch_test.py cafferitmo@192.168.8.191:~/
  2. Restart the service (or run directly):
       sudo systemctl restart smartswitch-test
       # or: python3 ~/smartswitch_test.py

Output:
  Console + smartswitch_timing.log (written alongside this script)

Requirements:
  python-socketio[client] — usually already present as a flask-socketio dependency.
  If not: pip3 install "python-socketio[client]"
"""

import json
import os
import sys
import time
import threading
import urllib.request
import urllib.parse
from datetime import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler

try:
    import socketio
except ImportError:
    print("ERROR: python-socketio not found. Run: pip3 install 'python-socketio[client]'")
    sys.exit(1)

# ── Config ──────────────────────────────────────────────────────────────────

CAFFE_RITMO_URL = "http://127.0.0.1:5000"
SHELLY_IP       = "192.168.8.197"
MY_IP           = "192.168.8.191"   # This Pi's IP — Shelly webhooks call here
WEBHOOK_PORT    = 8081
LOG_FILE        = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "smartswitch_timing.log")

# Resolve log file relative to this script's location when run from home dir
_script_dir = os.path.dirname(os.path.abspath(sys.argv[0]))
LOG_FILE = os.path.join(_script_dir, "smartswitch_timing.log")

# ── Shared state ─────────────────────────────────────────────────────────────

_lock           = threading.Lock()
_weight         = 0.0
_flow           = 0.0
_pump_was_on    = False   # tracks previous pump state for edge detection
_shot_count     = 0

# Per-shot: set when Shelly relay goes OFF
_off_time       = None
_off_weight     = None


# ── Logging ──────────────────────────────────────────────────────────────────

def _ts():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]

def log(msg):
    line = f"[{_ts()}] {msg}"
    print(line, flush=True)
    with open(LOG_FILE, "a") as f:
        f.write(line + "\n")


# ── Shelly helpers ────────────────────────────────────────────────────────────

def _shelly_get(method, params=None):
    url = f"http://{SHELLY_IP}/rpc/{method}"
    if params:
        url += "?" + urllib.parse.urlencode(params)
    with urllib.request.urlopen(url, timeout=5) as r:
        return json.loads(r.read())

def _shelly_post(method, params):
    url = f"http://{SHELLY_IP}/rpc/{method}"
    data = json.dumps(params).encode()
    req = urllib.request.Request(url, data=data,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=5) as r:
        return json.loads(r.read())


# ── Shelly webhook registration ───────────────────────────────────────────────

def setup_shelly_webhooks():
    """Delete any existing webhooks and register fresh switch.on/off ones."""
    try:
        existing = _shelly_get("Webhook.List").get("hooks", [])
        for hook in existing:
            _shelly_get("Webhook.Delete", {"id": hook["id"]})
            log(f"Deleted old webhook id={hook['id']}")
    except Exception as e:
        log(f"Warning: could not clear old webhooks: {e}")

    for hook_id, event, tag in [(1, "switch.on", "on"), (2, "switch.off", "off")]:
        url = f"http://{MY_IP}:{WEBHOOK_PORT}/relay?e={tag}"
        try:
            result = _shelly_post("Webhook.Create", {
                "id":     hook_id,
                "enable": True,
                "event":  event,
                "cid":    0,
                "urls":   [url],
            })
            log(f"Registered {event} webhook → {url}  ({result})")
        except Exception as e:
            log(f"ERROR registering {event} webhook: {e}")


# ── Webhook receiver ──────────────────────────────────────────────────────────

class WebhookHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        qs = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        event = qs.get("e", ["?"])[0]

        with _lock:
            w = _weight
            f = _flow

        if event == "off":
            global _off_time, _off_weight
            with _lock:
                _off_time   = time.monotonic()
                _off_weight = w
            log(f"*** Shelly relay OFF  weight={w:.1f}g  flow={f:.2f}g/s")
        elif event == "on":
            with _lock:
                _off_time   = None
                _off_weight = None
            log(f"Shelly relay ON  weight={w:.1f}g")

        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"ok")

    def log_message(self, *args):
        pass  # suppress default access log


def _run_webhook_server():
    server = HTTPServer(("0.0.0.0", WEBHOOK_PORT), WebhookHandler)
    log(f"Webhook receiver listening on :{WEBHOOK_PORT}")
    server.serve_forever()


# ── SocketIO client ───────────────────────────────────────────────────────────

def _run_sio():
    global _weight, _flow, _pump_was_on, _shot_count

    sio = socketio.Client(reconnection=True, reconnection_delay=5,
                          logger=False, engineio_logger=False)

    @sio.event
    def connect():
        log("Connected to CaffeRitmo SocketIO")

    @sio.event
    def disconnect():
        log("Disconnected from CaffeRitmo SocketIO")

    @sio.on("timer_update")
    def on_timer(data):
        global _weight, _flow, _pump_was_on

        w    = data.get("weight") or 0.0
        f    = data.get("flow_rate") or 0.0
        pump = data.get("ssrS", False)

        with _lock:
            _weight = w
            _flow   = f
            was     = _pump_was_on
            _pump_was_on = pump

        # Detect CaffeRitmo pump turning OFF
        if was and not pump:
            now = time.monotonic()
            log(f"*** CaffeRitmo pump OFF  weight={w:.1f}g  flow={f:.2f}g/s")

            with _lock:
                off_t = _off_time
                off_w = _off_weight

            if off_t is not None:
                delta_t = now - off_t
                delta_w = w - off_w
                early   = "early" if delta_w > 0 else "late"
                log(f"--- Comparison:")
                log(f"      Shelly OFF at    : {off_w:.1f}g")
                log(f"      CaffeRitmo OFF at: {w:.1f}g")
                log(f"      Weight delta     : {delta_w:+.1f}g  "
                    f"(CaffePeso {early} by {abs(delta_w):.1f}g)")
                log(f"      Time delta       : {delta_t:.2f}s")
            else:
                log("--- (no Shelly OFF recorded this shot)")
            log("-" * 60)

    @sio.on("brew_started")
    def on_brew_started(data):
        global _shot_count, _pump_was_on
        with _lock:
            _shot_count += 1
            count = _shot_count
            # Reset per-shot state
            global _off_time, _off_weight
            _off_time    = None
            _off_weight  = None
            _pump_was_on = True  # pump is on at brew start
        log(f"=== SHOT #{count} STARTED ===")

    @sio.on("brew_stopped")
    def on_brew_stopped(data):
        log("=== BREW STOPPED ===")

    while True:
        try:
            sio.connect(CAFFE_RITMO_URL, transports=["websocket", "polling"])
            sio.wait()
        except Exception as e:
            print(f"[{_ts()}] SocketIO error: {e} — retrying in 5s", flush=True)
            time.sleep(5)


# ── Entry point ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    print("CaffePeso Smart Switch Timing Logger")
    print(f"  Shelly IP     : {SHELLY_IP}")
    print(f"  Webhook port  : {WEBHOOK_PORT}")
    print(f"  CaffeRitmo    : {CAFFE_RITMO_URL}")
    print(f"  Log file      : {LOG_FILE}")
    print()

    setup_shelly_webhooks()
    threading.Thread(target=_run_webhook_server, daemon=True).start()
    _run_sio()   # blocks; reconnects automatically
