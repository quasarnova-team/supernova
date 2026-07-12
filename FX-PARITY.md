# OPC UA FX — what this module implements, projects, and leaves out

The `Fx` module implements the OPC UA FX (Field eXchange, OPC 10000-80/-81)
interaction pattern in the specification's *preconfigured datasets* discipline.
This matrix states the alignment honestly, line by line — the same way
[hypernova's DIP-PARITY](https://github.com/quasarnova-team/hypernova/blob/master/DIP-PARITY.md)
does for DIP. Vocabulary and behaviour references are to `opc.ua.fx.ac` 1.00.03.

| FX concept (Part 80/81) | This module | Status |
|---|---|---|
| AutomationComponent object | Created from `<Fx automationComponent=...>`, hosts the connection methods | ✅ implemented |
| FunctionalEntity, FunctionalEntities folder | Per `<FunctionalEntity>`; spec BrowseNames | ✅ implemented |
| InputData / OutputData | Per-dataset objects whose fields map to existing cache variables (fail-fast validated at startup) | ✅ implemented |
| Preconfigured datasets (`PreconfiguredDataSetOnly` discipline) | Connections can only activate datasets the configuration declared — the spec's conformance-friendly mode | ✅ implemented |
| `EstablishConnections` / `CloseConnections` methods | On the AutomationComponent (AC nodeset method ids 292/293), callable by any OPC UA client acting as connection manager | ✅ implemented |
| ConnectionEndpoints folder + ConnectionEndpoint objects | Created per connection; persist across close/re-establish under the same name | ✅ implemented |
| `ConnectionEndpointStatusEnum` | `Status` variable: Initial/Ready/PreOperational/Operational/Error (0–4), live | ✅ implemented |
| Data plane: Part 14 UADP over UDP (unicast + multicast) | The PubSub module engine, wired dynamically per connection; byte-identical on both OPC UA backends | ✅ implemented |
| ConnectionManager role | External: `hypernova fx connect/status/close` (publisher first, coordinates handed to the subscriber side, rollback on failure) | ✅ implemented (hypernova) |
| Method argument encoding (`ConnectionConfiguration` structures) | A documented **JSON projection** with spec field names, not the FX Data binary DataTypes — interoperable with the hypernova manager today, not yet with third-party FX managers | 🟡 projected |
| FX companion NodeSets (`http://opcfoundation.org/UA/FX/...` namespaces) | FX nodes live in the server's namespace (ns=2) with spec BrowseNames; the official NodeSets are not loaded | 🟡 projected |
| ConnectionManagerType information model (fx.cm), ConnectionConfigurationSets | Connection state lives in the manager's invocation + the servers' endpoints; no persistent configuration-set store | ❌ not yet |
| TSN / deterministic networking (Part 82) | Standard networks, best-effort delivery (sequence numbers surface loss) — no determinism claim | ❌ out of scope |
| Offline engineering / descriptors (Part 83) | Not implemented | ❌ out of scope |
| Safety over FX | Not implemented | ❌ out of scope |

## The path to closing the 🟡 rows

Both projections are wire-compatible questions, not architectural ones: the
data plane is already the standard's. Loading the official NodeSets needs a
namespace-aware address-space layer; encoding `ConnectionConfiguration` as the
spec's binary structures is a codec task of the same nature as the UADP codec
this repository already hand-rolled and golden-tests. They are the natural
headline of a future release once a third-party FX connection manager exists
to interoperate against.

## Evidence

- `fx` CI case (model + services + engine empty-start) green on **both**
  backends × alma9 / alma10 / Windows (incl. the commercial toolkit).
- FX end-to-end (`.supernova-bench/fx-e2e`): establish → cyclic values land in
  the subscriber's address space → Operational → close → Initial, in **all
  four** backend combinations (UASDK↔UASDK, o6↔o6, UASDK→o6, o6→UASDK).
- 47-check JSON codec suite (gcc + clang, `-Werror`, C++11 floor).
