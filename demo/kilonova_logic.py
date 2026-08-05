"""The Python engine's side of the demo: same Design, same config, ten lines
of behavior — the kilonova equivalent of QuasarServer.cpp's tick loop.

The mirror is fed across engines: a hypernova Subscriber decodes the C++
server's multicast feed and writes the counter it hears into this server's
PS1.mirror. kilonova serves, hypernova carries the wire — composition instead
of an in-engine Pub/Sub stack.
"""

import asyncio
import math
import threading
import time

from kilonova import Server
from hypernova import Subscriber


def mirror_feed(ps1, loop):
    while True:
        try:
            with Subscriber("demo/ps1/env", registry="http://registry:4850") as sub:
                for update in sub.updates():
                    value = update.values["counter"].value
                    asyncio.run_coroutine_threadsafe(ps1.setMirror(value), loop)
        except Exception:
            time.sleep(2)


async def main():
    server = Server("/demo/Design.xml", config_path="/demo/config.xml",
                    endpoint="opc.tcp://0.0.0.0:4842")
    async with server:
        ps1 = server.objects["PS1"]
        loop = asyncio.get_running_loop()
        threading.Thread(target=mirror_feed, args=(ps1, loop), daemon=True).start()
        counter = 0
        while True:
            await asyncio.sleep(0.1)
            counter += 1
            await ps1.setCounter(counter)
            await ps1.setTemperature(25.0 + 5.0 * math.sin(counter / 20.0))


asyncio.run(main())
