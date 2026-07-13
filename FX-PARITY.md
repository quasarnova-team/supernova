# OPC UA FX — what this module implements, projects, and leaves out

The `Fx` module implements the OPC UA FX (Field eXchange, OPC 10000-80/-81)
interaction pattern in the specification's *preconfigured datasets*
discipline. This matrix states the alignment honestly, line by line.
Section references are to OPC 10000-81 (UAFX Connecting Devices and
Information Model); the published nodeset version aligned to is 1.00.03
(`http://opcfoundation.org/UA/FX/AC/`, 2025-07-17). The architecture
decisions and the backend audit that constrain the projection rows are
recorded in [FX-ARCHITECTURE.md](FX-ARCHITECTURE.md).

| FX concept (Part 80/81) | This module | Status |
|---|---|---|
| AutomationComponent hosting the connection services (§6.2) | Object created from `<Fx automationComponent=...>`; hosts `EstablishConnections`/`CloseConnections` | ✅ implemented |
| FunctionalEntity, FunctionalEntities folder (§6.2.2, §6.4) | Per `<FunctionalEntity>`; spec BrowseNames | ✅ implemented |
| InputData / OutputData (§6.4.2) | Per-dataset descriptor views whose fields name the mapped cache variables (resolved fail-fast at startup); live values stay at their canonical addresses | ✅ implemented (descriptor projection — see C3) |
| Preconfigured datasets discipline | A connection manager can only activate datasets the configuration declared — nothing else | ✅ implemented |
| `EstablishConnections` / `CloseConnections` (§6.2.4/§6.2.5) | On the automation component, callable by any OPC UA client acting as connection manager | ✅ implemented |
| ConnectionEndpoints folder + endpoint objects (§6.6) | Created per connection; persist and are reused by name across close/re-establish; bounded (64 per functional entity) | ✅ implemented |
| `ConnectionEndpointStatusEnum` (§10.17) | Live `Status` variable: Initial=0 … Error=4; publishers report Operational when output is active, subscribers PreOperational→Operational on first received data | ✅ implemented (exposed as Int32 — the enum DataType node is not loaded, see below) |
| Data plane: Part 14 UADP over UDP | The `PubSub` module engine, wired dynamically per connection; identical wire on both OPC UA backends, unicast and multicast | ✅ implemented |
| ConnectionManager role | External by design: any OPC UA client (the docs show a 20-line asyncua manager; the e2e suite is one) | ✅ implemented (external) |
| Method argument encoding (`ConnectionConfiguration` structures, §6.2.4) | A documented **JSON projection** with spec-aligned member names — not the FX binary DataTypes; third-party FX connection managers cannot interoperate until those land | 🟡 projected |
| Official FX NodeSets (`http://opcfoundation.org/UA/FX/...`) | FX nodes carry spec BrowseNames in the server's namespace (ns=2); the official NodeSets are not loaded (no portable importer — audit C4) | 🟡 projected |
| FunctionalEntityType optional facets (Capabilities, ConfigurationData, OperationalHealth, Verify, control groups) | Not modelled (all Optional per §6.4.2 modelling rules) | ❌ not yet |
| ConnectionManagerType information model (fx.cm), ConnectionConfigurationSets | Connection state lives in the manager's invocation plus the servers' endpoints; no persistent configuration-set store | ❌ not yet |
| TSN / deterministic networking (Part 82) | Standard networks, best-effort delivery (sequence numbers surface loss) | ❌ out of scope |
| Offline engineering / descriptors (Part 83) | Not implemented | ❌ out of scope |
| Safety over FX | Not implemented | ❌ out of scope |

## The path to closing the 🟡 rows

Both projections are wire-compatibility questions, not architectural ones —
the data plane is already the standard's. Encoding `ConnectionConfiguration`
as the spec's binary structures is a codec task of the same nature as the
UADP codec this repository already hand-rolls and golden-tests, and it keeps
the same one-String-argument method shape. Loading the official NodeSets
needs a namespace-aware address-space layer (the Unified Automation backend
is ready today; open62541-compat has no NodeSet2 importer). They are the
natural headline of a future release, once a third-party FX connection
manager exists here to interoperate against.

## Evidence

- `fx` CI case (schema → binding → model → boot) green on **both** backends.
- FX end-to-end (`.supernova-bench/fx-clean`): establish → cyclic values land
  in the subscriber's address space → Operational → close → Initial →
  same-name reuse, in **all four** backend combinations
  (UASDK↔UASDK, o6↔o6, UASDK→o6, o6→UASDK).
- 34-probe hostile-client suite and 50-cycle churn, both backends: every
  malformed request refused, no endpoint-node leaks, ceiling engages,
  auto-naming survives, server stays functional.
- 100-check JSON codec unit suite (gcc + clang, `-Werror`, C++11 floor) on
  every push.
