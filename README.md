<h1 align="center">✦ supernova</h1>
<p align="center"><b>The <a href="https://quasarnova-team.github.io/">quasarnova</a> family's C++ engine — model-driven OPC UA servers, extended with OPC UA Pub/Sub and OPC UA FX.</b></p>

<p align="center">
  <a href="https://github.com/quasarnova-team/supernova/releases/latest"><img src="https://img.shields.io/github/v/release/quasarnova-team/supernova?label=release&color=3ecf7a" alt="Latest release"></a>
  <a href="https://github.com/quasarnova-team/supernova/actions/workflows/pubsub-tests.yml"><img src="https://github.com/quasarnova-team/supernova/actions/workflows/pubsub-tests.yml/badge.svg" alt="Wire and codec unit tests"></a>
  <a href="https://www.gnu.org/licenses/lgpl-3.0"><img src="https://img.shields.io/badge/License-LGPL_v3-blue.svg" alt="License: LGPL v3"></a>
</p>

> **Status: released — early.** [supernova v1.2.0](https://github.com/quasarnova-team/supernova/releases/latest)
> ships OPC UA FX on top of v1.1.0's Pub/Sub: declare your automation component's
> datasets in the configuration file and any OPC UA client — acting as the FX
> connection manager — wires your servers together at runtime, on either stack,
> across stacks. Young releases — the remaining hardening gates are tracked in the
> [hardening milestone](https://github.com/quasarnova-team/supernova/milestone/1).
> The family's Python engine is
> [kilonova](https://github.com/quasarnova-team/kilonova) (`pip install kilonova`) —
> the same Design file runs on both.

What is this?
-------------

Describe your device once in a declarative XML Design file and supernova generates a
complete, standards-compliant OPC UA server in C++: address space classes, device-logic
scaffolding, configuration parsing and a CMake build system — you write only your device
logic. It builds against either of two OPC UA stacks (the open-source
[open62541](https://open62541.org/) via a compatibility layer, or a commercial
UA-SDK-compatible stack).

What supernova adds
-------------------

supernova is a fork of the quasar framework extended with **OPC UA Pub/Sub**
(publisher *and* subscriber) and **OPC UA FX** (Field eXchange), kept in parity
across both supported stacks — see the
[Pub/Sub documentation](Documentation/source/PubSub.rst) and the
[FX documentation](Documentation/source/Fx.rst). Client/server OPC UA covers
request/response; Pub/Sub adds the many-to-many, brokerless data plane the standard
defines for high-rate telemetry and controller-to-controller traffic; FX adds the
interaction pattern on top — self-describing automation components whose
preconfigured datasets any OPC UA client can wire together at runtime
(`EstablishConnections`/`CloseConnections`), with browsable, live-status
connection endpoints.

Shipped in [v1.0.0 / v1.1.0](https://github.com/quasarnova-team/supernova/releases):
a backend-neutral Pub/Sub engine (UADP over UDP), Pub/Sub declared in the generated
server-configuration schema, a wire-codec unit suite running in CI, and
one-dimensional array support. Shipped in v1.2.0: the `Fx` module — Part 81
interaction pattern in the preconfigured-datasets discipline, dynamic Pub/Sub
reconfiguration, cross-backend FX connections proven end-to-end in all four
backend combinations ([spec-alignment matrix](FX-PARITY.md),
[architecture record](FX-ARCHITECTURE.md)). Remaining hardening gates live in the
[milestone](https://github.com/quasarnova-team/supernova/milestone/1).

Quick start
-----------

```bash
git clone --recursive https://github.com/quasarnova-team/supernova
cd supernova
./quasar.py set_build_config <path/to/config.cmake>
./quasar.py build
```

The `--recursive` flag is required (LogIt is a git submodule). Full build documentation
lives in-repo under `Documentation/`.

Heritage and license
--------------------

quasarnova builds on the lineage of the open-source
[quasar framework](https://github.com/quasar-team/quasar), developed at CERN and running
large-scale control systems for more than a decade. quasarnova is an independent project
and is not affiliated with or endorsed by CERN.

supernova is a fork of quasar: it is licensed **LGPL-3.0** and preserves the upstream
copyright and license notices in full. For the inherited feature set (Design language,
code generation, build system), the upstream framework documentation applies:
*upstream reference* — https://quasar.docs.cern.ch.

Contact
-------

[GitHub Issues](https://github.com/quasarnova-team/supernova/issues) ·
[Discussions](https://github.com/quasarnova-team/supernova/discussions)

© the quasarnova team, and the upstream quasar authors for the inherited codebase.
