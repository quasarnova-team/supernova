OPC UA FX (Field eXchange)
==========================

|
| by: Paris Moschovakos
| Created July 2026

Overview and end-user documentation
-----------------------------------

Preface
-------

| OPC UA FX (Field eXchange, OPC UA specifications 10000-80 to 10000-84)
  layers a controller-to-controller interaction pattern on top of the
  OPC UA client/server and Pub/Sub mechanisms: servers describe themselves
  as *automation components* whose *functional entities* offer named,
  preconfigured *datasets*; a *connection manager* — any OPC UA client —
  establishes *connections* between them at runtime, and the process data
  then flows directly between the servers over standard Part 14 Pub/Sub,
  with no manager in the data path.

| supernova's ``Fx`` module implements the Part 81 interaction pattern in
  the specification's *preconfigured datasets* discipline. Like the
  ``PubSub`` module it builds on, it is one backend-neutral implementation
  on the framework's portability layer: behaviour is identical with the
  Unified Automation and the open62541 backend (neither of which ships FX
  support that supernova could use — see ``FX-ARCHITECTURE.md`` for the
  audit), and connections interoperate across backends, which is
  continuously proven by the FX end-to-end suite in all four backend
  combinations.

Rationale
---------

| The ``PubSub`` module already moves values between servers — statically:
  what is published, and what is received where, is fixed in each server's
  configuration file. That is the right tool when the wiring *is* static.
  FX is for the recurring situations where the wiring is a decision made
  later, or by someone else, or repeatedly:

-  **commissioning**: a front-end server publishes environment data; which
   aggregation server consumes it — and on which multicast group — is
   decided when the rack is installed, by a commissioning tool, not when
   the server was configured. With FX the tool calls one method on each
   side; neither server is rebuilt, reconfigured or restarted.
-  **supervised rewiring**: an operator moves a consumer from one data
   source to another (a failed crate's twin, a test stand) by closing one
   connection and establishing another — reversible, observable, bounded.
-  **self-description**: what a server *can* exchange is browsable in its
   address space (the functional entities and their datasets), so an
   engineering tool can offer exactly the legal wiring choices instead of
   free-text addresses.

| The discipline is the specification's conformance-friendly
  *PreconfiguredDataSets* mode: the configuration file declares what may
  flow (which cache variables, under which dataset names, with which wire
  coordinates); the connection manager can activate exactly that, nothing
  else. A connection manager cannot invent new data flows.

Static Pub/Sub vs FX connections
--------------------------------

.. list-table::
   :header-rows: 1
   :widths: 22 39 39

   * - Aspect
     - ``<PubSub>`` (static)
     - ``<Fx>`` (this module)
   * - Wiring decided
     - In each server's configuration file
     - At runtime, by a connection manager (any OPC UA client)
   * - Rewiring
     - Edit configuration, restart server
     - ``CloseConnections`` + ``EstablishConnections``, live
   * - What may flow
     - Whatever the configuration declares
     - Only the preconfigured datasets the configuration declares
   * - Visibility
     - Log lines
     - Browsable ConnectionEndpoints with live Status per connection
   * - Data path
     - UADP over UDP (Part 14)
     - The same — one engine, one wire; FX only adds the control plane
   * - Typical use
     - Fixed telemetry fan-out
     - Commissioned / orchestrated server-to-server links

Quick start: an FX hello world
------------------------------

| Nothing about FX appears in ``Design.xml`` — any cache variable of a
  supported type can participate, on any existing design. Start from a
  minimal design:

.. code-block:: xml

   <d:design xmlns:d="http://cern.ch/quasar/Design" projectShortName="HelloFx">
     <d:class name="FxDevice">
       <d:devicelogic/>
       <d:cachevariable name="temperature" addressSpaceWrite="forbidden"
           initializeWith="valueAndStatus" nullPolicy="nullForbidden"
           dataType="OpcUa_Double" initialStatus="OpcUa_Good" initialValue="21.5"/>
       <d:cachevariable name="setpoint" addressSpaceWrite="regular"
           initializeWith="valueAndStatus" nullPolicy="nullForbidden"
           dataType="OpcUa_Double" initialStatus="OpcUa_Good" initialValue="-1"/>
     </d:class>
     <d:root>
       <d:hasobjects instantiateUsing="configuration" class="FxDevice"/>
     </d:root>
   </d:design>

| Build as usual (``./quasar.py validate_design``, choose the backend,
  ``./quasar.py generate device --all``, ``./quasar.py build Release``).
  The FX side of the server is declared entirely in the configuration
  file — the ``Fx`` element goes right after ``PubSub`` (if present) and
  before the instance elements:

.. code-block:: xml

   <configuration xmlns="http://cern.ch/quasar/Configuration">
     <Fx automationComponent="ProcessCell" publisherId="91" publisherIdType="UInt16">
       <FunctionalEntity name="control">
         <OutputDataset name="env" writerGroupId="200" dataSetWriterId="1"
                        publishingIntervalMs="100">
           <Field source="FX1.temperature"/>
         </OutputDataset>
         <InputDataset name="setpoints">
           <Field target="FX1.setpoint"/>
         </InputDataset>
       </FunctionalEntity>
     </Fx>
     <FxDevice name="FX1"/>
   </configuration>

| Run the server. The log confirms the component came online:

.. code-block:: none

   [FxEngine.cpp, INF] Fx: automation component 'ProcessCell' online (1 functional entity(ies), 1 output dataset(s), 1 input dataset(s))

| The address space now carries the component's self-description and its
  connection services:

.. code-block:: none

   Objects/
   └── ProcessCell
       ├── EstablishConnections(connectionConfiguration) → (connectionId, detail)
       ├── CloseConnections(connectionId) → (detail)
       └── FunctionalEntities/
           └── control/
               ├── OutputData/env/temperature      (descriptor: "FX1.temperature")
               ├── InputData/setpoints/setpoint    (descriptor: "FX1.setpoint")
               └── ConnectionEndpoints/            (one object per connection)

| Now wire two such servers together. *Any* OPC UA client is the
  connection manager; with `asyncua <https://github.com/FreeOpcUa/opcua-asyncio>`_
  the whole exchange is:

.. code-block:: python

   import asyncio, json
   from asyncua import Client, ua

   async def main():
       arg = lambda s: ua.Variant(s, ua.VariantType.String)
       async with Client("opc.tcp://server-a:4841") as a, \
                  Client("opc.tcp://server-b:4841") as b:
           # 1. publisher side on A — returns its wire coordinates
           _, detail = await a.get_node("ns=2;s=ProcessCell").call_method(
               "2:EstablishConnections", arg(json.dumps({
                   "functionalEntity": "control", "role": "publisher",
                   "dataset": "env", "address": "opc.udp://239.0.0.7:14840"})))
           coordinates = json.loads(detail)["coordinates"]
           # 2. subscriber side on B — hand the coordinates over
           await b.get_node("ns=2;s=ProcessCell").call_method(
               "2:EstablishConnections", arg(json.dumps({
                   "functionalEntity": "control", "role": "subscriber",
                   "dataset": "setpoints", "address": "opc.udp://239.0.0.7:14840",
                   "peer": coordinates})))

   asyncio.run(main())

| From this moment server A publishes ``FX1.temperature`` every 100 ms and
  the value lands in server B's ``FX1.setpoint`` — browsable, at its
  canonical address, through the regular cache-variable write path. Both
  servers grew a ``ConnectionEndpoints`` entry whose ``Status`` variable
  reports the connection state, live. Calling the methods from UaExpert
  (or any other client) works the same way — the argument is one JSON
  string.

Configuration file schema regarding FX
--------------------------------------

| The ``Fx`` element is part of the generated ``Configuration.xsd``, so a
  configuration carrying it is validated like everything else. Addresses
  are the dotted instance addresses quasar tools use everywhere
  (``instance.variable``); they are resolved once at server startup, and a
  configuration referencing a non-existing address (or something that is
  not a cache variable) fails the startup with a precise error message.
  Everything the schema cannot express — unique names, component-wide
  unique writer groups, the source/target discipline — is validated at
  parse time, equally fail-fast.

``Fx`` (at most one per configuration — the server's automation component)

.. list-table::
   :header-rows: 1
   :widths: 30 15 55

   * - Attribute
     - Default
     - Meaning
   * - ``automationComponent``
     - required
     - Name of the automation component object (under ``Objects/``)
   * - ``publisherId``
     - required
     - The component's Pub/Sub publisher identity, shared by all its
       output datasets (mirrors the ``PubSub`` element's attribute; at
       most 2^53 — the services exchange coordinates as JSON numbers)
   * - ``publisherIdType``
     - ``UInt16``
     - Wire type of the publisher id: ``Byte``, ``UInt16``, ``UInt32`` or
       ``UInt64``

``FunctionalEntity`` (one or more per ``Fx``) — attribute ``name``
(required, unique). Declares one functional entity with its datasets.

``OutputDataset`` (zero or more per entity) — what this entity can
publish:

.. list-table::
   :header-rows: 1
   :widths: 30 15 55

   * - Attribute
     - Default
     - Meaning
   * - ``name``
     - required
     - Dataset name (unique among the entity's output datasets)
   * - ``writerGroupId``
     - required
     - Writer group on the wire; each output dataset owns its group, so
       the id must be unique across the whole component
   * - ``dataSetWriterId``
     - required
     - DataSetWriter id within the group
   * - ``publishingIntervalMs``
     - ``100``
     - Default publishing interval (positive, at most one day); a
       connection manager may override it per connection

``InputDataset`` (zero or more per entity) — what this entity can
receive; attribute ``name`` (required, unique among the entity's input
datasets).

``Field`` (one or more per dataset) — one dataset member, in wire order:

.. list-table::
   :header-rows: 1
   :widths: 30 15 55

   * - Attribute
     - Default
     - Meaning
   * - ``source``
     - —
     - OutputDataset only: the cache variable to publish
   * - ``target``
     - —
     - InputDataset only: the cache variable that receives the field
   * - ``name``
     - leaf of the address
     - Field name in the dataset view; ``FX1.temperature`` defaults to
       ``temperature`` — give explicit names when two leaves collide

The connection services
-----------------------

| Both methods live on the automation component object (the Part 81
  placement) and are callable by any client the server's endpoint admits.
  The argument is a JSON object in one String argument — a documented
  *projection* of the specification's binary ``ConnectionConfiguration``
  structures (which are not portable across both backends today; see
  FX-PARITY.md). The parser is strict: unknown members, arrays, duplicate
  members, oversized (>64 KiB) or overly nested (>16 levels) documents,
  and numbers JSON cannot carry exactly are all refused with a precise
  diagnostic. A refused call returns ``Bad_InvalidArgument`` and puts the
  reason in ``detail``; the server's state is untouched by refused calls.

``EstablishConnections(connectionConfiguration: String) → (connectionId: String, detail: String)``

.. list-table::
   :header-rows: 1
   :widths: 28 12 60

   * - Member
     - Applies to
     - Meaning
   * - ``functionalEntity``
     - both
     - Entity name (required)
   * - ``role``
     - both
     - ``"publisher"`` or ``"subscriber"`` (required)
   * - ``dataset``
     - both
     - An OutputDataset (publisher) or InputDataset (subscriber) name
       (required)
   * - ``address``
     - both
     - ``opc.udp://host:port`` (required). Publisher: where datagrams are
       sent (multicast group or a peer's unicast address). Subscriber:
       the local listen address — a multicast group to join, a local
       interface address, or ``0.0.0.0`` (a non-local address is refused)
   * - ``connectionName``
     - both
     - Optional endpoint name (at most 64 bytes, no control characters);
       auto-named ``cep-N`` when omitted
   * - ``publishingIntervalMs``
     - publisher
     - Optional override of the dataset's configured interval (positive,
       at most 86400000 — one day)
   * - ``ttl``
     - publisher
     - Optional multicast time-to-live (0–255, default 1)
   * - ``peer``
     - subscriber
     - Required object with the publishing side's wire coordinates:
       ``publisherId``, ``publisherIdType`` (default ``UInt16``),
       ``writerGroupId``, ``dataSetWriterId``

| On success, ``connectionId`` names the connection endpoint and
  ``detail`` reports its status; a publisher's ``detail`` additionally
  carries ``coordinates`` — exactly the object a manager passes as the
  subscriber side's ``peer``. On refusal, ``detail`` is
  ``{"status":"Error","diagnostic":"..."}``.

``CloseConnections(connectionId: String) → (detail: String)``

| Accepts the bare connection id (or ``{"connectionId":"..."}``), stops
  the connection's data plane and returns the endpoint to ``Initial``.
  The endpoint object stays (nodes are permanent on both backends) and
  its name is reusable by a later establish.

Connection endpoints and their status
-------------------------------------

| Every connection is represented under its entity's
  ``ConnectionEndpoints`` folder: an object named after the connection
  with ``Status`` (Int32), ``Address`` and ``Dataset`` variables, all
  live. ``Status`` carries ``ConnectionEndpointStatusEnum`` (Part 81
  §10.17):

.. list-table::
   :header-rows: 1
   :widths: 20 10 70

   * - State
     - Value
     - When
   * - Initial
     - 0
     - Before the first establish, and after every close
   * - Ready
     - 1
     - (not used by this module — establish activates immediately)
   * - PreOperational
     - 2
     - Subscribing endpoint established, no data received yet
   * - Operational
     - 3
     - Publishing endpoint active; subscribing endpoint after its first
       received data
   * - Error
     - 4
     - Reported in ``detail`` for refused requests

| Endpoints are bounded: at most 64 distinct connection names per
  functional entity (one entity's name churn can never starve another).
  Beyond that, establishes with new names are refused; closed endpoints
  are reused by name (auto-naming reuses closed endpoints automatically),
  so long-running managers churn within the bound.

Notes and restrictions
----------------------

-  A dataset can be connected once at a time (per direction); close the
   live connection before rewiring it elsewhere.
-  Field counts and types follow the dataset declaration on *each* side;
   wire order is the ``Field`` order. The supported field types are those
   of the ``PubSub`` module (scalars and one-dimensional arrays).
-  The connection methods are callable by any client the server's
   endpoint security admits — there is no FX-specific access control; the
   data plane is plain UADP without Pub/Sub security. Use both on trusted
   networks.
-  The methods' argument encoding is a JSON projection, interoperable
   with any client that can write a string — but not (yet) with
   third-party FX connection managers expecting the binary
   ``ConnectionConfiguration`` DataTypes; FX-PARITY.md tracks this and
   the other honest deltas to the specification (BrowseNames live in the
   server's namespace ns=2; the official FX NodeSets are not loaded).
-  TSN determinism (Part 82), offline descriptors (Part 83) and safety
   profiles are out of scope.

Spec alignment
--------------

| The module aligns its vocabulary and behaviour to OPC 10000-81
  (UAFX Connecting Devices and Information Model), nodeset version
  1.00.03: ``EstablishConnections``/``CloseConnections`` on the
  automation component (§6.2.4/§6.2.5), ``FunctionalEntities`` with
  ``InputData``/``OutputData``/``ConnectionEndpoints`` (§6.4.2/§6.6),
  ``ConnectionEndpointStatusEnum`` values (§10.17), and the
  PreconfiguredDataSets discipline. The full line-by-line matrix —
  implemented / projected / out of scope, with evidence — is in
  `FX-PARITY.md <https://github.com/quasarnova-team/supernova/blob/master/FX-PARITY.md>`_;
  the architecture decisions and the backend audit behind them are in
  `FX-ARCHITECTURE.md <https://github.com/quasarnova-team/supernova/blob/master/FX-ARCHITECTURE.md>`_.
