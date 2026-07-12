OPC UA FX (Field eXchange)
==========================

|
| by: Paris Moschovakos
| Created July 2026

Overview and end-user documentation
-----------------------------------

Preface
-------

| OPC UA FX (Field eXchange, OPC UA specification Parts 80–84) is the
  connection layer the OPC Foundation built on top of Pub/Sub: instead of
  hand-configuring who publishes what to which multicast group, servers
  expose *functional entities* whose input and output datasets are
  declared once, and a *connection manager* wires an output of one server
  to an input of another **at runtime**, by calling standard methods. The
  data itself travels exactly as in Pub/Sub — compact UADP datagrams over
  UDP — so everything the PubSub module guarantees (wire format, backend
  parity, coexistence with classic clients) carries over unchanged.

| This module implements the FX interaction pattern in the spec's
  *preconfigured datasets* mode: the datasets a server may publish or
  consume are declared in its configuration file, and connections select
  and activate them. What is deliberately not implemented (yet) is listed
  plainly in `FX-PARITY.md
  <https://github.com/quasarnova-team/supernova/blob/master/FX-PARITY.md>`_.

Rationale
---------

| The ``PubSub`` element makes a server publish from the moment it boots
  to a fixed address — the right tool for standing telemetry. But
  engineered links between two servers have a lifecycle:

-  a link is commissioned, moved, or retired **without rebuilding or
   restarting either server**. Example: a test-stand server starts
   feeding an aggregation server for one campaign, then is disconnected.
   With static configuration that is an edit + restart on a production
   machine; with FX it is one method call from the outside.
-  the *link itself* should be visible: who is connected to whom, since
   when, in what state. FX servers expose every connection as a
   ``ConnectionEndpoint`` object with a live ``Status`` — browsable by
   any OPC UA client, no side channel needed.
-  the industrial world converged on exactly this pattern for
   controller-to-controller exchange: cyclic data over Pub/Sub,
   established and supervised through a standard connection-management
   surface. Speaking the same pattern keeps quasar servers compatible
   with that world's tooling and vocabulary.

Static Pub/Sub vs FX connections
--------------------------------

.. list-table::
   :header-rows: 1
   :widths: 22 39 39

   * - Aspect
     - ``<PubSub>`` (static)
     - ``<Fx>`` (this module)
   * - What the configuration declares
     - Complete wiring: addresses, ids, fields
     - Capabilities: datasets that *may* be connected
   * - When data flows
     - From server start, always
     - From ``EstablishConnections`` until ``CloseConnections``
   * - Who decides the wiring
     - The server's own config.xml
     - An external connection manager, at runtime
   * - Visibility of a link
     - Implicit (two config files agree)
     - A ``ConnectionEndpoint`` object with live ``Status`` on each side
   * - Data plane
     - UADP over UDP (the PubSub engine)
     - The same engine, the same bytes
   * - Typical use
     - Standing telemetry fan-out
     - Engineered server-to-server links with a lifecycle

Quick start: an FX hello world
------------------------------

| As with Pub/Sub, nothing about FX appears in ``Design.xml`` — datasets
  map existing cache variables. Start from a design with a few cache
  variables (the ``FxTester`` case in ``.CI/test_cases/test_fx`` is
  exactly this):

.. code-block:: xml

   <d:design xmlns:d="http://cern.ch/quasar/Design" projectShortName="HelloFx">
     <d:class name="Cell">
       <d:devicelogic/>
       <d:cachevariable name="temperature" addressSpaceWrite="forbidden"
           initializeWith="valueAndStatus" nullPolicy="nullForbidden"
           dataType="OpcUa_Double" initialStatus="OpcUa_Good" initialValue="21.5"/>
       <d:cachevariable name="setpoint" addressSpaceWrite="regular"
           initializeWith="valueAndStatus" nullPolicy="nullForbidden"
           dataType="OpcUa_Double" initialStatus="OpcUa_Good" initialValue="-1"/>
     </d:class>
     <d:root>
       <d:hasobjects instantiateUsing="configuration" class="Cell"/>
     </d:root>
   </d:design>

| The ``Fx`` element declares what this server *is* (an automation
  component), what functional entities it carries, and which datasets
  each entity offers (``OutputDataset``) or accepts (``InputDataset``).
  It goes after ``PubSub`` (if present) and before the instance
  elements:

.. code-block:: xml

   <configuration xmlns="http://cern.ch/quasar/Configuration">
     <Fx automationComponent="CellA">
       <FunctionalEntity name="control" publisherId="91" publisherIdType="UInt16">
         <OutputDataset name="env" writerGroupId="200" dataSetWriterId="1"
                        publishingIntervalMs="100">
           <Field name="temperature" source="C1.temperature"/>
         </OutputDataset>
         <InputDataset name="setpoints">
           <Field name="target" target="C1.setpoint"/>
         </InputDataset>
       </FunctionalEntity>
     </Fx>
     <Cell name="C1"/>
   </configuration>

| Run the server. The log confirms the FX view is online:

.. code-block:: none

   [FxEngine.cpp, INF] Fx: automation component 'CellA' online (1 functional entity(ies), 1 output dataset(s), 1 input dataset(s))

| Browsing the server now shows the FX view next to the ordinary quasar
  objects::

   CellA
   ├── FunctionalEntities
   │   └── control
   │       ├── OutputData
   │       │   └── env            (fields → mapped cache variables)
   │       ├── InputData
   │       │   └── setpoints
   │       └── ConnectionEndpoints
   ├── EstablishConnections       (method)
   └── CloseConnections           (method)

| Nothing is being published yet — FX datasets are *capabilities*. To
  wire two such servers together, call ``EstablishConnections`` on each,
  or let the `hypernova <https://github.com/quasarnova-team/hypernova>`_
  connection manager do both sides in one command:

.. code-block:: none

   hypernova fx connect \
     --publisher  opc.tcp://cell-a:4841 --pub-entity control --pub-dataset env \
     --subscriber opc.tcp://cell-b:4841 --sub-entity control --sub-dataset setpoints \
     --address opc.udp://239.192.0.20:4841

| The manager establishes the publisher side (the reply carries the wire
  coordinates), hands those coordinates to the subscriber side, and both
  servers grow a ``ConnectionEndpoint`` whose ``Status`` reads
  ``Operational`` (3). From that moment CellA emits one datagram per
  interval and the values land in CellB's ``C1.setpoint`` — visible to
  any classic OPC UA client reading CellB. ``hypernova fx status`` shows
  the endpoints; ``hypernova fx close`` tears the link down and the
  endpoints return to ``Initial`` (0).

Configuration file schema regarding FX
--------------------------------------

| The ``Fx`` element is part of the generated ``Configuration.xsd``. All
  addresses are the dotted instance addresses quasar tools use everywhere
  (``instance.variable``); they are resolved once at server startup and a
  configuration referencing a non-existing address fails the startup with
  a precise error.

.. list-table:: ``Fx``
   :header-rows: 1
   :widths: 30 15 55

   * - Attribute / element
     - Kind
     - Meaning
   * - ``automationComponent``
     - required
     - Name of the automation component object created under Objects
   * - ``FunctionalEntity``
     - 1..n
     - The entities this component carries

.. list-table:: ``FunctionalEntity``
   :header-rows: 1
   :widths: 30 15 55

   * - Attribute / element
     - Kind
     - Meaning
   * - ``name``
     - required
     - Entity name (unique within the component)
   * - ``publisherId`` / ``publisherIdType``
     - required / optional (default ``UInt16``)
     - The identity this entity publishes under
   * - ``OutputDataset``
     - 0..n
     - A dataset this entity can publish
   * - ``InputDataset``
     - 0..n
     - A dataset this entity can consume

.. list-table:: ``OutputDataset`` / ``InputDataset``
   :header-rows: 1
   :widths: 30 15 55

   * - Attribute / element
     - Kind
     - Meaning
   * - ``name``
     - required
     - Dataset name (unique within the entity)
   * - ``writerGroupId`` / ``dataSetWriterId``
     - required (output only)
     - The Part 14 coordinates this dataset publishes under
   * - ``publishingIntervalMs``
     - optional (default 100, output only)
     - Default publishing interval; a connection may override it
   * - ``Field``
     - 1..n
     - ``name`` plus ``source`` (output, published from) or ``target``
       (input, received into) — dotted cache-variable addresses

The connection services
-----------------------

| ``EstablishConnections`` takes one ``String`` argument: a JSON object
  (the *connection configuration projection* — see the alignment section
  below). Fields:

.. list-table::
   :header-rows: 1
   :widths: 28 15 57

   * - Field
     - Kind
     - Meaning
   * - ``functionalEntity``
     - required
     - Entity name
   * - ``role``
     - required
     - ``publisher`` or ``subscriber``
   * - ``dataset``
     - required
     - Output dataset (publisher) or input dataset (subscriber)
   * - ``address``
     - required
     - ``opc.udp://host:port`` — multicast or unicast
   * - ``publishingIntervalMs``
     - optional (publisher)
     - Overrides the dataset default
   * - ``peer``
     - required (subscriber)
     - Object with ``publisherId``, ``publisherIdType`` (optional),
       ``writerGroupId``, ``dataSetWriterId`` — as returned by the
       publisher side's establish
   * - ``connectionName``
     - optional
     - Endpoint name; server-assigned if absent
   * - ``ttl``
     - optional
     - Multicast time-to-live

| Output arguments: ``connectionId`` (the endpoint name) and ``detail``
  (JSON: ``status``, ``address``, and — for publisher connections — the
  ``coordinates`` object a connection manager hands to the subscriber
  side). On refusal the call returns ``BadInvalidArgument`` and the
  reason is logged by the server.

| ``CloseConnections`` takes the ``connectionId`` (plain string, or JSON
  carrying ``connectionId``), stops the flow, and sets the endpoint's
  ``Status`` back to ``Initial``. Re-establishing under the same name
  reuses the endpoint object.

| ``ConnectionEndpoint`` objects carry ``Status`` (``Initial`` 0,
  ``Ready`` 1, ``PreOperational`` 2, ``Operational`` 3, ``Error`` 4 —
  the spec's ``ConnectionEndpointStatusEnum``), ``Address`` and
  ``Dataset``. Endpoints and their dynamic Pub/Sub wiring are torn down
  at server shutdown.

Alignment with the OPC UA FX specification
------------------------------------------

| The module follows Part 80/81 concepts and vocabulary —
  AutomationComponent, FunctionalEntity, InputData/OutputData,
  ConnectionEndpoints, ``EstablishConnections``/``CloseConnections``,
  the endpoint status enumeration, and the *preconfigured datasets*
  discipline (connections can only activate what the configuration
  declared). Two deliberate simplifications, both stated in
  `FX-PARITY.md
  <https://github.com/quasarnova-team/supernova/blob/master/FX-PARITY.md>`_:

-  the FX nodes live in the server's own namespace with spec BrowseNames
   (the official ``http://opcfoundation.org/UA/FX/`` companion NodeSets
   are not loaded);
-  the method arguments are a documented JSON projection of the spec's
   ``ConnectionConfiguration`` structures rather than their binary
   encodings — interoperable with the hypernova connection manager
   today, with third-party FX connection managers once the binary
   structures land.

| No TSN (Part 82) determinism is claimed — connections run on standard
  networks with Pub/Sub's best-effort delivery — and offline descriptors
  (Part 83) are out of scope.

Worked example: wiring two servers
----------------------------------

| Server A publishes environment data; server B consumes it into its own
  address space. A's configuration declares the output dataset (as in
  the quick start above); B declares the mirror image:

.. code-block:: xml

   <Fx automationComponent="CellB">
     <FunctionalEntity name="control" publisherId="92" publisherIdType="UInt16">
       <InputDataset name="mirror">
         <Field name="temperature" target="C1.setpoint"/>
       </InputDataset>
     </FunctionalEntity>
   </Fx>

| One ``hypernova fx connect`` later (see the quick start), A's
  ``temperature`` ticks into B's ``setpoint`` at the configured interval.
  The two servers may run **different OPC UA backends** — the engine's
  wire format is byte-identical on both, and the FX end-to-end test
  exercises all four backend combinations.
