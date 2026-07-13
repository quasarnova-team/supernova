# OPC UA FX in supernova — architecture decision record

This is the *evaluate/architect* record behind the `Fx` module: what OPC UA FX
is, what the two supported OPC UA backends actually offer for it (audited, with
evidence), the consequences, the chosen shape, and the alternatives that were
rejected. The user-facing feature documentation lives in
`Documentation/source/Fx.rst`; the spec-alignment matrix in `FX-PARITY.md`.

## 1. What OPC UA FX is, and what of it we build

OPC UA FX (Field eXchange, OPC 10000-80..84) layers a controller-to-controller
interaction pattern on top of OPC UA client/server and Pub/Sub:

- **Part 80** — concepts: AutomationComponents exposing FunctionalEntities
  that exchange preconfigured datasets over standard Pub/Sub, orchestrated by
  a connection manager.
- **Part 81** — the information model and services (what this module aligns
  to, at version 1.00.03 of the published nodesets):
  - `AutomationComponentType` hosts a `FunctionalEntities` folder and the
    connection methods `EstablishConnections` (§6.2.4) / `CloseConnections`
    (§6.2.5).
  - `FunctionalEntityType` (§6.4.2): `InputData`, `OutputData`,
    `ConnectionEndpoints`, capabilities — all optional per the modelling
    rules.
  - `ConnectionEndpointType` (§6.6): mandatory `Status` of
    `ConnectionEndpointStatusEnum` (§10.17: Initial=0, Ready=1,
    PreOperational=2, Operational=3, Error=4); the concrete subtype for
    Pub/Sub data planes is `PubSubConnectionEndpointType`.
  - Official nodesets: `http://opcfoundation.org/UA/FX/AC/` (with `/FX/Data/`
    and `/FX/CM/`), version 1.00.03, requiring UA 1.05.05 and DI 1.04.
- **Part 82** (TSN determinism), **Part 83** (offline descriptors) and the
  safety profiles — out of scope, stated as such.

v1 scope: the Part 81 interaction pattern in the spec's *preconfigured
datasets* discipline — a server declares in its configuration what it can
publish and what it can accept; any OPC UA client acting as connection
manager may activate exactly that, nothing else, between two supernova
servers (or between supernova and any Part 14 UADP peer).

## 2. Backend reality (audited 2026-07-13)

**Unified Automation C++ SDK 2.0.3.681** (the commercial backend):

- Delivery contains the base client/server SDK only: `uastack`, `uabasecpp`,
  `uapkicpp`, `xmlparsercpp`, `uaclientcpp`, `coremodule`, `uamodule`,
  `uamodels` (DI + PLCopen).
- **No Pub/Sub runtime**: `BUILD_PUBSUB` exists in the build system but is
  gated on `src/uapubsub/` which is not part of this delivery — the Pub/Sub
  engine is a separately licensed "PubSub SDK Bundle". Only the standard
  NS0 Pub/Sub *datatypes* are compiled in.
- **No FX**: `BUILD_FXAC` / `BUILD_FXCM` options exist, likewise gated on
  absent sources (`uabasefx{data,ac,cm}`, `uaserverfx{ac,cm}`). Zero FX
  headers or symbols in the delivered libraries. (The 2.x product line does
  offer FX — AC since 2.0.0, CM in beta since 2.0.2, FX 1.00.03 nodesets —
  as part of that separately licensed bundle.)
- Rich modelling toolkit present: runtime ObjectType/Object/Method creation,
  `UaPropertyMethodArgument`, a full NodeSet2 importer
  (`NodeManagerNodeSetXml`), custom structured DataTypes
  (`UaStructureDefinition`, `UaGenericStructureValue`).

**open62541 backend** (via open62541-compat, pin v1.5.8):

- The runtime stack is the amalgamated **open62541 v1.5.4** bundled inside
  open62541-compat (`extern/open62541`, `Git-Revision: v1.5.4`); the
  standalone open62541 mirror checkout is a 2020 v1.1-dev snapshot that the
  build never uses.
- **Native Pub/Sub is compiled in** (`UA_ENABLE_PUBSUB` defaulted ON in
  1.5.x) — but the compat C++ layer wraps none of it; using it would mean
  raw `UA_Server_*` C calls on one backend only.
- **No FX** anywhere (v1.5.4 predates any UAFX contribution; none exists
  upstream in-tree today either).
- The compat portability surface flattens the type system: objects are
  instances of `BaseObjectType` (no ObjectType nodes are created), one
  custom namespace hardcoded at ns=2, **no NodeSet2 import**, **no custom
  structured DataTypes**, and `addUaReference` (cross-references between
  existing nodes) throws "implement me" unconditionally.
- What it does support portably: adding objects/variables at runtime,
  methods with `UaPropertyMethodArgument` input/output signatures, and the
  `ChangeNotifyingVariable` cache-variable machinery quasar builds on.

**Consequences** — each one pins an architectural decision:

- **C1 — parity by construction, not by stack.** The only Pub/Sub the two
  backends share is the one supernova itself ships: the `PubSub` module's
  backend-neutral UADP-over-UDP engine (golden-tested, wire-compatible with
  open62541's native implementation). FX's data plane is that engine.
- **C2 — no portable custom structured DataTypes.** The spec's
  `ConnectionConfiguration*` structures cannot be registered as first-class
  DataTypes on the open62541 backend. The connection methods therefore take
  a documented **JSON projection** in a String argument, parsed by a strict
  codec that refuses everything outside the projection. Stated as a
  projection in FX-PARITY.md; a binary codec is a later, additive step that
  keeps the same one-argument shape.
- **C3 — no portable cross-node references.** Dataset views expose
  *descriptor* variables (field name → mapped instance address) instead of
  Organizes-references to the live variables; the live values stay at their
  canonical addresses (`ns=2;s=Instance.variable`).
- **C4 — no portable nodeset loading.** FX nodes carry spec BrowseNames in
  the server's namespace (ns=2) rather than being instances of the official
  FX types. An honest parity row; closable later by a namespace-aware
  address-space layer (the UA SDK side is already capable).

## 3. The shape of the feature

One new module, `Fx/`, exactly parallel to `PubSub/`:

```
Fx/
├── CMakeLists.txt          — OBJECT library, listed in NATIVE_SERVER_MODULES
├── include/
│   ├── FxConfiguration.h   — plain config structs (no backend types)
│   ├── FxXsdBinding.h      — generated-XSD → structs, fail-fast validation
│   ├── FxJson.h            — strict JSON subset codec (services argument)
│   └── FxEngine.h          — singleton: model builder + connection services
├── src/…
└── test/
    └── test_json.cpp       — stock-runner unit suite (like PubSub/test)
```

**Configuration entry** — `<Fx>` in the configuration file, schema generated
through the existing Jinja templates (`designToConfigurationXSD.jinja`,
`designToConfigurator.jinja`), validated by XSD like everything else:

```xml
<Fx automationComponent="ProcessCell" publisherId="91" publisherIdType="UInt16">
  <FunctionalEntity name="control">
    <OutputDataset name="env" writerGroupId="200" dataSetWriterId="1"
                   publishingIntervalMs="100">
      <Field source="FX1.temperature"/>          <!-- name defaults to the leaf -->
      <Field name="count" source="FX1.counter"/>
    </OutputDataset>
    <InputDataset name="setpoints">
      <Field target="FX1.setpoint"/>
    </InputDataset>
  </FunctionalEntity>
</Fx>
```

`Design.xml` stays untouched — the PubSub precedent: any cache variable of a
supported type can participate, on any existing design. The `publisherId`
sits at the `<Fx>` level exactly like on the `<PubSub>` element — one server,
one publisher identity — and wire coordinates
(`writerGroupId`/`dataSetWriterId`) are validated unique across the whole
component at parse time.

**Information model**, built at startup from the staged configuration:

```
Objects/
└── ProcessCell                          (the AutomationComponent)
    ├── FunctionalEntities/
    │   └── control/
    │       ├── OutputData/env/{temperature, count}    (descriptor variables)
    │       ├── InputData/setpoints/{target}
    │       └── ConnectionEndpoints/
    │           └── <one object per connection>/{Status, Address, Dataset}
    ├── EstablishConnections(connectionConfiguration: String)
    │        → (connectionId: String, detail: String)
    └── CloseConnections(connectionId: String) → (detail: String)
```

**Services** (the preconfigured discipline — a manager can only activate what
the configuration declared):

- *Establish, role=publisher*: activates the named OutputDataset on a
  caller-supplied `opc.udp://` address → a dynamic writer group in the
  PubSub engine; returns the wire coordinates in `detail` so a manager can
  hand them to the subscribing side.
- *Establish, role=subscriber*: wires the named InputDataset to the
  caller-supplied peer coordinates → a dynamic reader.
- *Close*: tears the dynamic entities down; the endpoint returns to Initial
  and its name is reusable. Address-space nodes are permanent on both
  backends, so endpoints are bounded by a per-component ceiling and reused
  by name.
- *Failure atomicity*: the step that can fail (dynamic data-plane wiring)
  runs before any permanent node creation — a refused establish leaves no
  trace.

**Data plane** — the PubSub engine grows a tagged dynamic-reconfiguration
API: `ensureStarted` (empty start when no `<PubSub>` section is present),
`addDynamic(tag, …)` / `removeDynamic(tag)`. Mutations are posted onto the
engine's io thread (preserving its single-thread confinement), built
fully before being committed (exception safety), and tagged so one owner's
entities can be removed together. Static `<PubSub>` and dynamic FX entities
coexist in one engine, one wire.

**Server wiring** — the generated Configurator stages both engines;
`BaseQuasarServer` starts Fx after PubSub and shuts Fx down *before* PubSub
(Fx teardown posts work onto the PubSub io thread — the order is
load-bearing).

## 4. Alternatives evaluated

| Decision | Chosen | Rejected because |
|---|---|---|
| Data plane | supernova PubSub engine + dynamic API | Native stack Pub/Sub: absent from the delivered UA SDK (licensed bundle), unwrapped raw C API on open62541 — either choice forks behaviour per backend and breaks parity-by-construction |
| Services argument | one String argument, documented JSON projection | Spec binary structures: not portable (C2), huge surface, and no third-party FX manager exists here to interoperate with yet. Flat typed argument lists: optionality (peer only for subscribers, per-call overrides) degenerates into sentinel values, and every evolution changes the method signature |
| Dataset views | descriptor variables | Organizes-references to the live variables: compat `addUaReference` throws (C3) |
| FX types | spec BrowseNames on plain objects in ns=2 | Loading the official nodesets: no portable importer (C4); UA SDK could, open62541-compat cannot |
| publisherId placement | on `<Fx>` (component level), like `<PubSub>` | Per-entity publisher ids: no v1 use case, invites cross-entity wire ambiguity, diverges from the sibling feature's UX |
| Design.xml entry | none required (config-only, the PubSub precedent) | A `<d:fx>` class-level declaration: dataset fields bind to *instance* addresses, so a class-level shape still needs per-instance configuration for ids and addresses — double declaration, no gain. Revisit if a class-level FX shape proves wanted in practice |
| AutomationComponent multiplicity | one per server (XSD maxOccurs=1) | Multiple components: no v1 use case; the `<Fx>` section *is* the server's FX identity |

## 5. Quality gates

Every aspect must earn 10/10 with evidence before release: architecture,
implementation, spec alignment, UX, testing, end-to-end proof, security
posture, documentation, review. Testing accompanies every increment:

1. JSON codec unit suite — stock runner, gcc + clang, C++11, `-Werror`;
2. `fx` smoke case in the CI manifest, both backends, local docker cells;
3. address-space oracle for the FX model;
4. end-to-end establish → data lands → close, across backend combinations;
5. hostile-input and endurance passes;
6. the full pre-existing matrix stays green.
