#!/usr/bin/env python3
"""
Publish a message to the Max Headron OLED over MQTT-over-WebSocket.

Credentials come from the MQTT_USERNAME / MQTT_PASSWORD environment
variables (never CLI args - those end up in shell history / ps output).
The host comes from --host or the MAXHEADRON_MQTT_HOST env var.

Examples:
    MQTT_USERNAME=maxheadron MQTT_PASSWORD=secret \\
        python3 tools/say.py --host maxheadron-mqtt.onrender.com "HELLO RICHARD"

    # against a broker started locally per mqtt-broker/README.md:
    MQTT_USERNAME=maxheadron MQTT_PASSWORD=changeme \\
        python3 tools/say.py --host 127.0.0.1 --port 8765 --no-tls "HELLO" --spark
"""
import argparse
import json
import os
import sys
import time

import paho.mqtt.client as mqtt


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("message", nargs="?", default=None, help="text to show on the ticker")
    parser.add_argument("--host", default=os.environ.get("MAXHEADRON_MQTT_HOST"),
                         help="broker host, e.g. maxheadron-mqtt.onrender.com (or MAXHEADRON_MQTT_HOST)")
    parser.add_argument("--port", type=int, default=None, help="default 443 with TLS, 80 without")
    parser.add_argument("--path", default="/", help="WebSocket path (default: /)")
    parser.add_argument("--topic", default="maxheadron/express")
    parser.add_argument("--hold-ms", type=int, default=4000, help="how long the message stays up")
    parser.add_argument("--spark", action="store_true", help="also trigger a spark flash")
    parser.add_argument("--no-tls", action="store_true", help="use ws:// instead of wss:// (local testing only)")
    args = parser.parse_args()

    if not args.host:
        parser.error("no broker host: pass --host or set MAXHEADRON_MQTT_HOST")
    username = os.environ.get("MQTT_USERNAME")
    password = os.environ.get("MQTT_PASSWORD")
    if not username or not password:
        parser.error("set MQTT_USERNAME and MQTT_PASSWORD in the environment")
    if args.message is None and not args.spark:
        parser.error("nothing to do: give a message and/or --spark")

    port = args.port or (80 if args.no_tls else 443)
    payload = {}
    if args.message is not None:
        payload["say"] = args.message
        payload["hold_ms"] = args.hold_ms
    if args.spark:
        payload["spark"] = True

    client = mqtt.Client(client_id="say-py", transport="websockets",
                          callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(username, password)
    client.ws_set_options(path=args.path)
    if not args.no_tls:
        client.tls_set()

    # Publish from the main thread, not from an on_connect callback: that
    # callback runs on the same network thread that has to process the
    # incoming PUBACK, so blocking there on wait_for_publish() deadlocks.
    connect_result = {}

    def on_connect(client, userdata, flags, reason_code, properties=None):
        connect_result["reason_code"] = reason_code

    client.on_connect = on_connect
    client.connect(args.host, port, keepalive=30)
    client.loop_start()
    try:
        for _ in range(50):  # up to ~5s, Render free tier can cold-start slowly
            if "reason_code" in connect_result:
                break
            time.sleep(0.1)
        else:
            print("timed out waiting to connect", file=sys.stderr)
            return 1

        if connect_result["reason_code"] != 0:
            print(f"connect failed: {connect_result['reason_code']}", file=sys.stderr)
            return 1

        info = client.publish(args.topic, json.dumps(payload), qos=1)
        info.wait_for_publish(timeout=5)
        if not info.is_published():
            print("publish did not complete", file=sys.stderr)
            return 1
    finally:
        client.disconnect()
        client.loop_stop()

    print(f"sent to {args.topic}: {json.dumps(payload)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
