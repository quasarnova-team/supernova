# The 60-second demo

One 16-line [Design.xml](Design.xml) is the entire application. One command
runs it on the whole quasarnova family:

```bash
docker compose up
```

Within a minute your terminal shows live values arriving by name:

```
14:03:07  demo/ps1/env  seq=412  counter=412  temperature=29.20553815192969  label='supernova'
```

## What is running

| Service | Product | Role |
|---------|---------|------|
| `supernova` | the C++ engine | serves the Design at `opc.tcp://localhost:4841` and publishes `PS1.counter/temperature/label` over OPC UA Pub/Sub (UADP multicast, 10 Hz) — and subscribes to its own counter feed back into `PS1.mirror`, so one server demonstrates both roles |
| `kilonova` | the Python engine | serves the **identical** `Design.xml` and `config.xml` (volume-mounted from this directory) at `opc.tcp://localhost:4842` — no code generation, no compiler. Its behavior is [kilonova_logic.py](kilonova_logic.py): the same tick in ten lines of Python, and its `PS1.mirror` tracks the **C++ engine's** counter — decoded off the multicast wire by a hypernova `Subscriber` and written through a generated setter. The wire crosses engines |
| `registry` | hypernova | gives the publication its name; live browser at <http://localhost:4850> |
| `seed` | hypernova | one-shot: registers `demo/ps1/env` (a supernova server publishes; the registry only needs to know the name) |
| `subscriber` | hypernova | `hypernova sub demo/ps1/env` — decodes the wire and prints live values |

Every container runs a shipped product; there is no demo-only code. The values
tick in [QuasarServer.cpp](QuasarServer.cpp) — the one user file quasar asks
you to write. The Design is byte-identical to the `pubsub` case the CI oracle
runs on every platform.

Point any OPC UA client (UaExpert, ...) at either engine; the address spaces
match because they come from the same Design.

## The 60-second claim, enforced

[verify.sh](verify.sh) is the demo's CI gate (`family-demo` workflow): it runs
this compose on a clean runner and asserts changing values within 60 seconds
of `up`, that the supernova image was built from this exact `Design.xml`, and
that kilonova is serving. If the demo rots, the build reds.

## Teardown

```bash
docker compose down
```
