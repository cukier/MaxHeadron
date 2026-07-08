"""
Minimal MQTT-over-WebSocket broker for Max Headron, meant to run on
Render's free tier (which only exposes HTTP/WebSocket, not raw TCP - see
mqtt-broker/README.md for why this shape).

Auth: a single username/password pair from environment variables. Not
meant to scale past "one device, one or two people publishing to it" -
if you need more than that, put a real broker (Mosquitto, EMQX) behind
this instead.
"""
import asyncio
import logging
import os
import tempfile
from pathlib import Path

from amqtt.broker import Broker
from passlib.apps import custom_app_context as pwd_context

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
logger = logging.getLogger("maxheadron.broker")


def build_password_file() -> Path:
    username = os.environ.get("MQTT_USERNAME", "maxheadron")
    password = os.environ.get("MQTT_PASSWORD")
    if not password:
        raise SystemExit("MQTT_PASSWORD environment variable is required")

    fd, path = tempfile.mkstemp(prefix="amqtt_passwd_")
    with os.fdopen(fd, "w") as f:
        f.write(f"{username}:{pwd_context.hash(password)}\n")
    return Path(path)


async def main() -> None:
    port = int(os.environ.get("PORT", "8080"))
    password_file = build_password_file()

    config = {
        # amqtt requires a listener literally named "default" - other
        # listeners (there are none here) would inherit its settings.
        "listeners": {
            "default": {
                "type": "ws",
                "bind": f"0.0.0.0:{port}",
            },
        },
        "plugins": {
            "amqtt.plugins.logging_amqtt.EventLoggerPlugin": {},
            "amqtt.plugins.authentication.FileAuthPlugin": {
                "password_file": str(password_file),
            },
        },
    }

    broker = Broker(config=config)
    await broker.start()
    logger.info("Max Headron MQTT broker listening on ws 0.0.0.0:%d", port)

    try:
        await asyncio.Event().wait()
    finally:    
        await broker.shutdown()


if __name__ == "__main__":
    asyncio.run(main())
