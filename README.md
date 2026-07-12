<h1 align="center">✦ supernova</h1>
<p align="center"><b>The <a href="https://quasarnova-team.github.io/">quasarnova</a> family's C++ engine — model-driven OPC UA servers, extended with OPC UA Pub/Sub.</b></p>

<p align="center">
  <img src="https://img.shields.io/badge/status-in%20development-orange" alt="Status: in development">
  <a href="https://www.gnu.org/licenses/lgpl-3.0"><img src="https://img.shields.io/badge/License-LGPL_v3-blue.svg" alt="License: LGPL v3"></a>
  <a href="https://github.com/quasarnova-team/supernova/milestone/1"><img src="https://img.shields.io/badge/first%20release-milestone-informational" alt="First release milestone"></a>
</p>

> **Status: in development — not released.** There is no tagged release yet and the
> Pub/Sub layer is under active construction. The family's shipping engine today is
> [kilonova](https://github.com/quasarnova-team/kilonova) (pure Python,
> `pip install kilonova`) — and the same Design file will run on both.

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
(publisher *and* subscriber), kept in parity across both supported stacks — see the
[Pub/Sub documentation](Documentation/source/PubSub.rst). Client/server OPC UA covers
request/response; Pub/Sub adds the many-to-many, brokerless data plane the standard
defines for high-rate telemetry and controller-to-controller traffic.

Progress toward the first tagged release is tracked in the
[v0.1 milestone](https://github.com/quasarnova-team/supernova/milestone/1).

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
