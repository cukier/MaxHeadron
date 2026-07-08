# Max Headron MQTT broker

A tiny MQTT broker so a running conversation (or anyone with the
credentials) can push a line of text or a "spark" trigger to the OLED in
real time, over the internet.

## Why WebSocket instead of plain MQTT

Render's free tier only exposes HTTP/HTTPS (WebSocket upgrades included)
on the public URL - no raw TCP port for classic MQTT on 1883/8883. So
this broker ([amqtt](https://github.com/Yakifo/amqtt)) speaks
**MQTT-over-WebSocket** only, and the ESP32 firmware connects with
`esp-mqtt`'s `ws://`/`wss://` transport instead of the usual `mqtt://`.
Verified locally end-to-end (pub/sub round trip, and wrong-password
connections get dropped) before writing this README.

Free tier also spins the service down after 15 minutes with no inbound
traffic - the first message after a cold start takes about a minute to
land. A device that keeps a persistent WS connection with MQTT keepalive
pings avoids this entirely, since those pings count as inbound traffic.

## Deploying

1. Push this repository to a GitHub repo you control (this project
   isn't pushed anywhere by default - see the main README for why).
2. In the Render dashboard: **New > Blueprint**, point it at the repo.
   Render will read `render.yaml` and create a free Python web service
   rooted at `mqtt-broker/`.
3. Render will prompt for `MQTT_USERNAME` and `MQTT_PASSWORD` (marked
   `sync: false` in the blueprint, so they're never committed to git).
   Pick anything; you'll enter the same values into the ESP32's
   `idf.py menuconfig` later.
4. Once deployed, your broker is reachable at
   `wss://<service-name>.onrender.com/` (Render terminates TLS for you;
   the app itself just speaks plain `ws://` on `$PORT`).

## Testing it yourself

```
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
MQTT_USERNAME=maxheadron MQTT_PASSWORD=changeme PORT=8765 .venv/bin/python3 server.py
```

Then, from another terminal (using the venv under `../tools/.venv` set
up for `tools/say.py`, or any MQTT-over-WS capable client):

```
../tools/.venv/bin/python3 ../tools/say.py --host 127.0.0.1 --port 8765 --no-tls "HELLO"
```

## Wire protocol

Topic: `maxheadron/express` (configurable via the ESP32's Kconfig).
Payload: JSON, e.g. `{"say": "HELLO RICHARD", "hold_ms": 4000}` or
`{"spark": true}`. See `tools/say.py` for the publishing side and
`main/mqtt.c` for how the firmware parses it.
