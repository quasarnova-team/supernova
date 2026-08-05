# Migrating from quasar

supernova is [quasar](https://github.com/quasar-team/quasar) plus a data plane.
The divergence is additive by construction: a server that never declares Pub/Sub
or FX builds and behaves identically here. Migration is therefore the framework's
own upgrade path, and adoption of the new features is a separate, later, optional
step.

## What actually diverges

The entire divergence hangs off one line in the build system — `PubSub` and `Fx`
appended to `NATIVE_SERVER_MODULES` in `CMakeLists.txt` — plus the seams that
line demands:

- **`PubSub/`** — the backend-neutral Pub/Sub engine (UADP over UDP), new module.
- **`Fx/`** — the Part 81 interaction pattern on top of it, new module.
- **Configuration templates** — the *generated* `Configuration.xsd` gains
  optional `PubSub` and `Fx` elements. These are `config.xml` features;
  `Design.xsd` is byte-identical to upstream's.
- **`BaseQuasarServer`** — engine start/stop hooks in the server bootstrap.
- Documentation, and a CI reworked to run token-free outside CERN.

Five modified code files; everything else supernova adds is a new file next to
untouched upstream ones. Fork point: upstream `v2.1.1+93` (2026-07-03); relevant
upstream fixes are ported as they land ([upstream's](https://github.com/quasar-team/quasar/releases)
current release is v2.1.2).

## What stays yours, unchanged

- **`Design.xml` and `Design.xsd`** — the contract is identical. A Design that
  validates upstream validates here, and generates the same address space.
- **Your device logic** — `D*` classes and their generation are untouched.
- **`config.xml`** — every valid upstream configuration remains valid; the new
  elements are optional.
- **Both backends** — open62541 (via open62541-compat) and the UA-SDK-compatible
  stack, same selection mechanics.
- **License** — LGPL-3.0, terms unchanged.

## Migrating an existing server

The standard framework upgrade, from a supernova checkout instead of a quasar one:

```bash
git clone --recursive https://github.com/quasarnova-team/supernova
cd supernova
./quasar.py upgrade_project /path/to/your/server
cd /path/to/your/server
./quasar.py build
```

Nothing to adopt on day one. Pub/Sub and FX activate only when you add their
elements to `config.xml` — start with the
[Pub/Sub documentation](Documentation/source/PubSub.rst), then the
[FX documentation](Documentation/source/Fx.rst).

## Going back

The same door, reversed: remove any `PubSub`/`Fx` elements from your
`config.xml`, then run `upgrade_project` from an upstream quasar checkout.
Nothing else to undo — your Design, device logic and the rest of your
configuration were never supernova-specific.
