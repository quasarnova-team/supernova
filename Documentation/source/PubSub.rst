OPC UA Pub/Sub
==============

|
| by: Paris Moschovakos
| Created July 2026

Overview and end-user documentation
-----------------------------------

Preface
-------

| OPC UA Pub/Sub (OPC UA specification Part 14) complements the classic
  client/server model with connectionless, one-to-many data distribution:
  a server periodically encodes selected values into compact binary
  NetworkMessages (UADP encoding) and sends them over UDP — typically to a
  multicast group — where any number of subscribers receive them without
  opening a session, without polling, and without adding any per-consumer
  load on the server.

| supernova servers can take both Pub/Sub roles — publisher and
  subscriber — while serving classic OPC UA clients at the same time. The
  feature is provided by the framework's ``PubSub`` module, one
  backend-neutral engine built on the same portability layer as the rest
  of the framework: behaviour is identical with the Unified Automation and
  the open62541 backend, and neither backend's native Pub/Sub support is
  required (or used). Wire-format interoperability is continuously tested
  against open62541's native Pub/Sub implementation as an independent
  reference peer.

Rationale
---------

| A quasar-based server exposes its model to clients which connect, create
  a session and subscribe. That is the right tool for supervision and
  control, but there are recurring situations where it is a poor fit:

-  many consumers want the same few values at a steady rate.
   Example: crate temperatures and fan speeds that a monitoring wall, an
   archiver and an analytics pipeline all want at 10 Hz.
   With client/server, each consumer costs the server a session, a
   subscription and per-client sampling; with Pub/Sub the server sends
   one datagram per interval, whether there are zero consumers or fifty.
-  a server needs values that live in another server.
   Example: an aggregation server computing alarms over quantities
   published by several front-end servers.
   With Pub/Sub the aggregation server simply subscribes to the front-end
   servers' streams and the values land in its own address space — no
   embedded OPC UA client, no session management, no reconnection logic.
-  data must keep flowing with minimal overhead on constrained links or
   at rates where session-based subscriptions add unwelcome jitter: UADP
   datagrams carry a few bytes of header per message.

Client/server subscriptions vs Pub/Sub
--------------------------------------

.. list-table::
   :header-rows: 1
   :widths: 22 39 39

   * - Aspect
     - Client/server subscription
     - Pub/Sub (this module)
   * - Transport
     - Session over TCP, per client
     - UDP datagrams, typically multicast, no sessions
   * - Server cost per consumer
     - Grows with every client
     - Constant (one datagram per interval)
   * - Delivery guarantee
     - Reliable, acknowledged
     - Best effort (sequence numbers allow loss detection)
   * - Consumer coupling
     - Consumer connects to a specific server
     - Consumers join a multicast group; publisher and consumers need not
       know each other
   * - Security
     - Endpoint security policies
     - None yet in this module — use on trusted networks
   * - Typical use
     - Supervision, control, alarms
     - Telemetry fan-out, server-to-server value exchange

Quick start: a Pub/Sub hello world
----------------------------------

| Nothing about Pub/Sub appears in ``Design.xml`` — any cache variable of
  a supported type can be published. Start from a minimal design:

.. code-block:: xml

   <d:design xmlns:d="http://cern.ch/quasar/Design" projectShortName="HelloPubSub">
     <d:class name="Greeter">
       <d:devicelogic/>
       <d:cachevariable name="counter" addressSpaceWrite="forbidden"
           initializeWith="valueAndStatus" nullPolicy="nullForbidden"
           dataType="OpcUa_Int32" initialStatus="OpcUa_Good" initialValue="0"/>
       <d:cachevariable name="greeting" addressSpaceWrite="forbidden"
           initializeWith="valueAndStatus" nullPolicy="nullForbidden"
           dataType="UaString" initialStatus="OpcUa_Good" initialValue="hello, world"/>
     </d:class>
     <d:root>
       <d:hasobjects instantiateUsing="configuration" class="Greeter"/>
     </d:root>
   </d:design>

| Build the server as usual (``./quasar.py validate_design``, choose your
  backend and build configuration, ``./quasar.py generate device --all``,
  ``./quasar.py build Release``). To make the published data alive, tick
  the counter from the server's main loop
  (``Server/src/QuasarServer.cpp``):

.. code-block:: cpp

   void QuasarServer::mainLoop()
   {
       OpcUa_Int32 counter = 0;
       const std::vector<Device::DGreeter*>& greeters =
           Device::DRoot::getInstance()->greeters();
       while (ShutDownFlag() == 0)
       {
           std::this_thread::sleep_for(std::chrono::milliseconds(100));
           counter++;
           for (size_t i = 0; i < greeters.size(); i++)
               greeters[i]->getAddressSpaceLink()->setCounter(counter, OpcUa_Good);
       }
   }

| Publishing is declared entirely in the configuration file. The
  ``PubSub`` element goes right after ``StandardMetaData`` (if present)
  and before the instance elements:

.. code-block:: xml

   <configuration xmlns="http://cern.ch/quasar/Configuration">
     <PubSub publisherId="42" publisherIdType="UInt16">
       <Connection address="opc.udp://239.0.0.5:4840">
         <WriterGroup id="100" publishingIntervalMs="100">
           <DataSetWriter id="1">
             <Field source="G1.counter"/>
             <Field source="G1.greeting"/>
           </DataSetWriter>
         </WriterGroup>
       </Connection>
     </PubSub>
     <Greeter name="G1"/>
   </configuration>

| Run the server. The log confirms the engine came up:

.. code-block:: none

   [PubSubEngine.cpp, INF] PubSub: engine started (1 writer group(s), 1 data set writer(s), 0 data set reader(s))

| From this moment the server emits one NetworkMessage every 100 ms on
  multicast group 239.0.0.5, port 4840: publisher id 42, writer group
  100, DataSetWriter 1, a data key frame with two fields — the live
  counter and the greeting string. Any OPC UA Part 14 subscriber can
  receive it; open62541's ``pubsub_subscribe_standalone`` example is a
  convenient reference tool. And because Pub/Sub coexists with the
  classic server, any ordinary OPC UA client can read
  ``ns=2;s=G1.counter`` over ``opc.tcp`` at the same time and see the
  same values.

| To *receive* instead, declare a ``DataSetReader`` — the mirror image
  (see the subscriber example at the end of this page).

Configuration file schema regarding Pub/Sub
-------------------------------------------

| The ``PubSub`` element is part of the generated ``Configuration.xsd``,
  so a configuration carrying it is validated like everything else. All
  addresses are the dotted instance addresses quasar tools use everywhere
  (``instance.variable``); they are resolved once at server startup, and
  a configuration referencing a non-existing address (or something that
  is not a cache variable) fails the startup with a precise error
  message.

``PubSub`` (at most one per configuration)

.. list-table::
   :header-rows: 1
   :widths: 25 15 60

   * - Attribute
     - Default
     - Meaning
   * - ``publisherId``
     - required
     - This server's publisher id, shared by all its connections
   * - ``publisherIdType``
     - ``UInt16``
     - Wire type of the publisher id: ``Byte``, ``UInt16``, ``UInt32`` or
       ``UInt64``

``Connection`` (one or more per ``PubSub``)

.. list-table::
   :header-rows: 1
   :widths: 25 15 60

   * - Attribute
     - Default
     - Meaning
   * - ``address``
     - required
     - ``opc.udp://host:port``; a multicast group address enables
       one-to-many distribution
   * - ``ttl``
     - ``1``
     - Multicast time-to-live (how far datagrams propagate)
   * - ``loopback``
     - ``true``
     - Whether datagrams sent by this server are visible to subscribers
       on the same host

``WriterGroup`` (zero or more per ``Connection``) — attributes ``id``
(required) and ``publishingIntervalMs`` (required, positive). One
NetworkMessage is published per interval, carrying one DataSetMessage per
contained ``DataSetWriter``.

``DataSetWriter`` (one or more per ``WriterGroup``) — attribute ``id``
(required); contains ``Field`` elements whose ``source`` attribute names
the cache variable to publish. Field order defines the DataSetMessage
field order.

``DataSetReader`` (zero or more per ``Connection``) — attributes
``publisherId``, ``writerGroupId``, ``dataSetWriterId`` (all required;
they select which remote DataSetMessages to accept) and
``publisherIdType`` (default ``UInt16``); contains ``Field`` elements
whose ``target`` attribute names the cache variable that receives the
corresponding field, in order.

Publisher role
--------------

| Values are sampled from the address space at publishing time — whatever
  wrote them last (device logic, an OPC UA client, calculated variables)
  is what gets published. Fields of one DataSetMessage are sampled
  sequentially and independently, per OPC UA Part 14 semantics; there is
  no cross-field atomicity guarantee. NetworkMessages carry the group
  sequence number and each DataSetMessage its own sequence number, so
  subscribers can detect datagram loss.

Subscriber role
---------------

| A ``DataSetReader`` matches incoming NetworkMessages on the triple
  (``publisherId``, ``writerGroupId``, ``dataSetWriterId``) and writes
  the received fields, in order, onto its target cache variables.
  Received values enter the address space through the regular
  cache-variable write path, so clients, calculated variables and change
  listeners observe them like any other write. Datagrams that do not
  match any reader, or that cannot be decoded, are ignored (with a debug
  log entry stating why).

Supported field types
---------------------

| Boolean, Byte, Int16, UInt16, Int32, UInt32, Int64, UInt64, Float,
  Double and String scalars. A published field of an unsupported type is
  sent as a Null variant (with a warning in the log); a received field of
  an unsupported type is skipped. Arrays are not yet supported.

Wire format and interoperability
--------------------------------

| Messages are UADP version 1, with publisher id, group header (writer
  group id + sequence number), payload header, and data-key-frame
  DataSetMessages with Variant field encoding — the same profile
  open62541's Pub/Sub tutorials use. On reception the decoder
  additionally tolerates timestamps, picoseconds, status and
  configuration-version fields, DataValue field encoding, keep-alive
  frames and promoted fields. Chunked and secured (signed/encrypted)
  NetworkMessages are not supported and are rejected with a diagnostic.

Notes and restrictions
----------------------

-  There is no Pub/Sub security profile yet: use it on trusted networks
   (the classic client/server endpoint security is unaffected).
-  The publisher id is one attribute on ``PubSub`` and is shared by all
   connections of the server; readers specify the *remote* publisher id
   they listen to.
-  A ``DataSetReader``'s targets are written with status ``Good`` and the
   reception time as timestamps.
-  Field counts should match between a writer and its readers; when they
   do not, the common prefix is applied and a warning is logged.
-  Values live in the address space; extreme publishing rates were not a
   design goal — intervals from a few milliseconds upwards are the
   intended regime.

Examples
--------

Telemetry fan-out: one publisher, many listeners
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

| A front-end server publishes two environment quantities ten times per
  second; a monitoring wall, an archiver and an ad-hoc debugging laptop
  all join the same multicast group. None of them appears in the server's
  session list; unplugging any of them changes nothing for the others.

.. code-block:: xml

   <PubSub publisherId="7" publisherIdType="UInt16">
     <Connection address="opc.udp://239.192.0.10:4840" ttl="4">
       <WriterGroup id="1" publishingIntervalMs="100">
         <DataSetWriter id="1">
           <Field source="crate1.temperature"/>
           <Field source="crate1.fanSpeed"/>
         </DataSetWriter>
       </WriterGroup>
     </Connection>
   </PubSub>

Server-to-server: aggregating values from another server
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

| An aggregation server mirrors the front-end's quantities into its own
  address space, where its device logic (or calculated variables) can
  compute over them. The two servers may even run different OPC UA
  backends — the wire behaviour is identical.

.. code-block:: xml

   <PubSub publisherId="8" publisherIdType="UInt16">
     <Connection address="opc.udp://239.192.0.10:4840">
       <DataSetReader publisherId="7" writerGroupId="1" dataSetWriterId="1">
         <Field target="remoteCrate1.temperature"/>
         <Field target="remoteCrate1.fanSpeed"/>
       </DataSetReader>
     </Connection>
   </PubSub>
