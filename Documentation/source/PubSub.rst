OPC UA Pub/Sub
==============

supernova servers can act as OPC UA Pub/Sub (OPC UA Part 14) publishers and
subscribers, exchanging address-space values as UADP NetworkMessages over UDP
(typically multicast) — connectionless, one-to-many and with no client session
involved. This complements the classic client/server access: both are served
by the same process at the same time.

The feature is provided by the framework's ``PubSub`` module, a single
backend-neutral engine built on the same portability layer as the rest of the
framework. It behaves identically with the Unified Automation and the
open62541 backend — neither backend's native Pub/Sub support is required (or
used). Interoperability of the wire format is continuously tested against
open62541's native Pub/Sub implementation as an independent reference peer.

Declaring Pub/Sub in the server configuration
---------------------------------------------

Pub/Sub is deployment configuration, not data modelling: it binds *instances*
(which exist only at configuration time) to network transports. It is
therefore declared in the server configuration file, in a ``PubSub`` element
validated by the generated ``Configuration.xsd``, and it references the same
dotted instance addresses that quasar tools use everywhere else. The
``Design.xml`` of a server needs no change at all — any cache variable of a
supported type can be published or subscribed.

The ``PubSub`` element goes right after ``StandardMetaData`` (if present), and
before any instance elements:

.. code-block:: xml

   <configuration ...>
     <PubSub publisherId="42" publisherIdType="UInt16">
       <Connection address="opc.udp://239.0.0.5:4840" ttl="1" loopback="true">
         <WriterGroup id="100" publishingIntervalMs="100">
           <DataSetWriter id="1">
             <Field source="PS1.counter"/>
             <Field source="PS1.temperature"/>
             <Field source="PS1.label"/>
           </DataSetWriter>
         </WriterGroup>
         <DataSetReader publisherId="7" publisherIdType="UInt16"
                        writerGroupId="200" dataSetWriterId="5">
           <Field target="PS1.mirror"/>
         </DataSetReader>
       </Connection>
     </PubSub>
     <PubSubTester name="PS1"/>
   </configuration>

Publisher role
--------------

Each ``WriterGroup`` publishes one NetworkMessage per ``publishingIntervalMs``
on its connection. Every contained ``DataSetWriter`` contributes one
DataSetMessage (a data key frame, Variant field encoding) whose ``Field``
elements name cache variables by instance address (``source``). Values are
sampled from the address space at publishing time — whatever wrote them last
(device logic, an OPC UA client, calculated variables) is what gets published.

Subscriber role
---------------

A ``DataSetReader`` matches incoming NetworkMessages on the triple
(``publisherId``, ``writerGroupId``, ``dataSetWriterId``) and writes the
received fields, in order, onto the cache variables named by its ``Field
target`` attributes. Received values enter the address space through the
regular cache-variable write path, so clients, calculated variables and
change listeners observe them like any other write.

Supported field types
---------------------

Boolean, Byte, Int16, UInt16, Int32, UInt32, Int64, UInt64, Float, Double and
String scalars. A published field of any other type is sent as a Null variant
(with a warning in the log); a received field of an unsupported type is
skipped. Arrays are not yet supported.

Wire format and interoperability
--------------------------------

Messages are UADP version 1, with publisher id, group header (writer group id
+ sequence number), payload header, and data-key-frame DataSetMessages with
Variant field encoding — the same profile open62541's Pub/Sub tutorials use.
On reception the decoder additionally tolerates timestamps, picoseconds,
status and configuration-version fields, DataValue field encoding, keep-alive
frames and promoted fields. Chunked and secured (signed/encrypted)
NetworkMessages are not supported and are rejected with a diagnostic.

Notes
-----

- The publisher id is shared by all connections of a server (one attribute on
  ``PubSub``); readers specify the *remote* publisher id they listen to.
- Field addresses are resolved once, at server startup; a configuration
  referencing a non-existing address (or something that is not a cache
  variable) fails the startup with a precise error message — same philosophy
  as the rest of the quasar configuration handling.
- ``loopback`` (default true) controls whether multicast datagrams sent by
  the server are visible to readers on the same host; ``ttl`` (default 1)
  controls how far multicast propagates.
- There is no security profile yet: use it on trusted networks (the classic
  client/server endpoint security is unaffected).
