"""Checks a Narrow Road Navigation server accepts the hardware's messages.

It does, from a laptop, exactly what the units do, message for message:
  main unit:    log in, link a device, connect to /vehicle with the token,
                register, send telemetry and headings, send a suggestion
  display unit: log in, join Socket.IO with the token, identify, and read
                session:assigned and vehicles:update
It also checks the server's refusal messages are the ones the firmware
looks for.

Needs:  pip install websocket-client

Against your own server, with an account made in the app:
  python protocol_check.py --server https://your-server.onrender.com \
      --username you --password yourpassword
  (Linking moves that account to a test device: log in on the app or
  restart the main unit afterwards.)

Against a test server with ALLOW_TEST_SIGNUP=true, it makes its own accounts:
  python protocol_check.py --server http://localhost:5000 --create
"""

from __future__ import annotations

import argparse
import json
import math
import ssl
import sys
import time
import urllib.error
import urllib.request

import websocket  # websocket-client

results = []


def check(ok: bool, what: str, detail: str = "") -> bool:
    results.append(ok)
    print(f"  {'ok  ' if ok else 'FAIL'} {what}{(': ' + detail) if detail else ''}")
    return ok


def http(server: str, method: str, path: str, body=None, token: str | None = None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(server + path, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    try:
        with urllib.request.urlopen(req, timeout=70) as r:
            return r.status, json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read() or b"{}")
        except json.JSONDecodeError:
            return e.code, {}


def ws_url(server: str, path: str) -> str:
    return server.replace("https://", "wss://").replace("http://", "ws://") + path


# The exact text the main unit's sendTelemetry() builds with snprintf
def telemetry(lat, lon, heading, speed, bearing, accuracy=5.0) -> str:
    return (
        '{"lat":%.7f,"lon":%.7f,"heading":%.1f,'
        '"accelX":%.3f,"accelY":%.3f,"accelZ":%.3f,'
        '"gyroX":%.4f,"gyroY":%.4f,"gyroZ":%.4f,'
        '"speed":%.2f,"accuracy":%.1f,"bearing":%.1f}'
        % (lat, lon, heading, 0.12, -0.05, 9.81, 0.001, -0.002, 0.0, speed, accuracy, bearing)
    )


class MainUnit:
    """The main unit's connection to /vehicle."""

    def __init__(self, server, token, device_id):
        self.ws = websocket.create_connection(
            ws_url(server, "/vehicle"),
            header=[f"Authorization: Bearer {token}"],
            timeout=15,
            sslopt={"cert_reqs": ssl.CERT_REQUIRED},
        )
        self.ws.send(json.dumps({"type": "register", "deviceId": device_id}))
        self.reply = json.loads(self.ws.recv())

    def send(self, text):
        self.ws.send(text)

    def close(self):
        self.ws.close()


class Screen:
    """The display unit's Socket.IO session, spoken directly as the firmware does."""

    def __init__(self, server, token):
        self.ws = websocket.create_connection(
            ws_url(server, "/socket.io/?EIO=4&transport=websocket"), timeout=15
        )
        self.token = token
        self.vehicle_id = None
        self.updates = []
        self.errors = []
        self.joined = False
        self.refused = None

    def pump(self, seconds: float):
        end = time.time() + seconds
        self.ws.settimeout(0.5)
        while time.time() < end:
            try:
                text = self.ws.recv()
            except websocket.WebSocketTimeoutException:
                continue
            if not text:
                continue
            if text[0] == "0":  # Engine.IO open: join with the login
                self.ws.send("40" + json.dumps({"token": self.token}))
            elif text == "2":
                self.ws.send("3")
            elif text.startswith("40"):
                self.joined = True
                self.ws.send('42["session:identify"]')
            elif text.startswith("44"):
                self.refused = json.loads(text[2:]).get("message")
                return
            elif text.startswith("42"):
                event, *data = json.loads(text[2:])
                if event == "session:assigned":
                    self.vehicle_id = data[0]["vehicleId"]
                elif event == "vehicles:update":
                    self.updates.append(data[0])
                elif event == "session:error":
                    self.errors.append(data[0]["message"])

    def own(self):
        for update in reversed(self.updates):
            for v in update.get("vehicles", []):
                if v["vehicleId"] == self.vehicle_id:
                    return v
        return None

    def close(self):
        self.ws.close()


def inside(lat, lon, polygon) -> bool:
    hit = False
    for i in range(len(polygon)):
        (y1, x1), (y2, x2) = polygon[i], polygon[i - 1]
        if (y1 > lat) != (y2 > lat) and lon < x1 + (lat - y1) * (x2 - x1) / (y2 - y1):
            hit = not hit
    return hit


def point_on_road(polygon):
    """A point inside the road. Road outlines are one side then the other,
    so a point halfway between matching vertices is on the centre line; the
    plain average can fall outside a curved road."""
    n = len(polygon)
    for i in [n // 4, n // 3, n // 2 - 1, 1, 2]:
        a, b = polygon[i], polygon[n - 1 - i]
        lat, lon = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
        if inside(lat, lon, polygon):
            return lat, lon
    return sum(q[0] for q in polygon) / n, sum(q[1] for q in polygon) / n


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--server", required=True)
    p.add_argument("--username")
    p.add_argument("--password")
    p.add_argument("--create", action="store_true", help="make test accounts (test servers only)")
    p.add_argument("--save-updates", help="write the vehicles:update messages to this file")
    p.add_argument("--no-suggestion", action="store_true", help="don't send a test suggestion")
    args = p.parse_args()
    server = args.server.rstrip("/")
    device_id = "esp32-0000test0001"

    print("Account")
    if args.create:
        username, password = f"hwcheck{int(time.time()) % 100000}", "hardware-check-pw"
        status, body = http(server, "POST", "/api/auth/register",
                            {"username": username, "email": f"{username}@example.com",
                             "password": password, "vehicleType": "car"})
        if not check(status in (200, 201), "test account created", f"HTTP {status} {body.get('message', '')}"):
            sys.exit(1)
    else:
        if not (args.username and args.password):
            sys.exit("Give --username and --password, or --create on a test server")
        username, password = args.username, args.password

    print("Main unit")
    status, body = http(server, "POST", "/api/auth/login", {"username": username, "password": password})
    check(status == 200 and "token" in body, "POST /api/auth/login gives a token", f"HTTP {status}")
    token = body.get("token")
    if not token:
        sys.exit(1)

    status, body = http(server, "POST", "/api/auth/device", {"deviceId": device_id}, token)
    check(status == 200, "POST /api/auth/device links the device", f"HTTP {status}")

    unit = MainUnit(server, token, device_id)
    check(unit.reply.get("type") == "registered" and unit.reply.get("vehicleId", "").startswith("VEHICLE_"),
          "/vehicle register is answered", json.dumps(unit.reply))

    status, roads = http(server, "GET", "/api/narrow-roads", token=token)
    road = None
    if status == 200:
        items = roads if isinstance(roads, list) else roads.get("roads", roads.get("data", []))
        road = items[0] if items else None
    check(road is not None, "a narrow road to drive into", road["name"] if road else f"HTTP {status}")

    # Drive into the road at walking pace, a packet a second like the firmware
    lat, lon = point_on_road(road["polygon"])
    for i in range(4):
        unit.send(telemetry(lat, lon, 90.0, 1.2, 90.0))
        time.sleep(1.05)
    unit.send(json.dumps({"type": "heading", "heading": 95.0}))

    print("Display unit")
    screen = Screen(server, token)
    screen.pump(4)
    check(screen.joined, "Socket.IO accepts the login (40)")
    check(screen.vehicle_id == unit.reply.get("vehicleId"), "session:assigned names the main unit's vehicle",
          str(screen.vehicle_id))
    for _ in range(3):
        unit.send(telemetry(lat, lon, 95.0, 1.2, 90.0))
        screen.pump(1.05)
    own = screen.own()
    check(own is not None and own.get("active"), "vehicles:update shows the car as active")
    if own:
        check(abs(own["lat"] - lat) < 1e-6 and abs(own["lon"] - lon) < 1e-6,
              "position arrives with full precision")
        kind = (own.get("narrowRoadAlert") or {}).get("kind")
        check(kind == "IN_NARROW_ROAD", "inside the road, the alert is IN_NARROW_ROAD", str(kind))
        check(len(own.get("currentRoads", [])) == 1, "the road's outline is sent to the screen")
        size = len(json.dumps(screen.updates[-1]))
        check(size < 262144, "vehicles:update fits the screen's message limit", f"{size} bytes")
    if args.save_updates:
        with open(args.save_updates, "w") as f:
            json.dump({"vehicleId": screen.vehicle_id, "updates": screen.updates}, f)

    if not args.no_suggestion:
        print("Camera suggestion (main unit)")
        # The shape NarrowDetector makes, sent as server_link.cpp builds it
        clat, clon = lat + 0.001, lon + 0.001
        m_lat, m_lon = 1 / 110540, 1 / (111320 * math.cos(math.radians(clat)))
        poly = [[clat + i * 3 * m_lat, clon - 1.5 * m_lon] for i in range(10)] + \
               [[clat + i * 3 * m_lat, clon + 1.5 * m_lon] for i in reversed(range(10))]
        body = ('{"name":"Narrow road (car camera)","polygon":['
                + ",".join("[%.7f,%.7f]" % (a, b) for a, b in poly)
                + '],"reason":"Found by the car camera: about 3.0 m wide along 27 m (100% of readings narrow)"}')
        req = urllib.request.Request(server + "/api/narrow-road-suggestions", data=body.encode(), method="POST")
        req.add_header("Content-Type", "application/json")
        req.add_header("Authorization", f"Bearer {token}")
        try:
            with urllib.request.urlopen(req, timeout=70) as r:
                check(r.status == 201, "suggestion accepted (201)", "review it on the admin page")
        except urllib.error.HTTPError as e:
            check(False, "suggestion accepted", f"HTTP {e.code} {e.read()[:120]!r}")

    print("Refusals the firmware recognises")
    bad = websocket.create_connection(ws_url(server, "/vehicle"),
                                      header=["Authorization: Bearer not-a-real-token"], timeout=15)
    bad.send(json.dumps({"type": "register", "deviceId": device_id}))
    reply = json.loads(bad.recv())
    check(reply.get("message", "").startswith("Not authorized"),
          "main unit: a bad login gives \"Not authorized...\"", reply.get("message", ""))
    bad.close()
    s2 = Screen(server, "not-a-real-token")
    s2.pump(3)
    check(s2.refused == "Not authorized", "screen: a bad login gives 44 \"Not authorized\"", str(s2.refused))
    s2.close()

    screen.close()
    unit.close()
    print(f"\n{sum(results)}/{len(results)} checks passed")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
