OPC UA Pub/Sub end to end
=========================

|
| by: Paris Moschovakos
| Created August 2026

| This is the runnable companion to :doc:`PubSub`. Every file on this page
  is included from ``demo/`` in the repository — the same files the
  ``family-demo`` CI workflow executes on every change, on x86_64 and
  arm64, asserting live decoded values within sixty seconds. Nothing here
  is documentation-only: if a snippet on this page stopped working, the
  build would be red before you could read it.

What you will build
-------------------

| One sixteen-line Design file served by two engines, with values crossing
  between them over the standard's wire:

-  **supernova** (this framework, C++): serves the model over ``opc.tcp``,
   publishes three fields at 10 Hz over UADP multicast — and subscribes to
   its own counter feed back into a ``mirror`` variable, so a single
   server demonstrates both Pub/Sub roles.
-  **kilonova** (the family's Python engine): serves the *identical*
   Design and configuration, and its ``mirror`` tracks the C++ server's
   counter — received off the multicast wire by a hypernova subscriber and
   written through a generated setter.
-  **hypernova**: names the stream in a registry and decodes it in your
   terminal, without opening an OPC UA session anywhere.

The sixty-second path
---------------------

| With Docker installed:

.. code-block:: bash

   git clone --recursive https://github.com/quasarnova-team/supernova
   cd supernova/demo
   docker compose up

| Within seconds your terminal streams decoded values by name:

.. code-block:: none

   subscriber-1  | 14:03:07  demo/ps1/env  seq=412  counter=412  temperature=29.20553815192969  label='supernova'

| The rest of this page rebuilds that result piece by piece, so you can
  transplant each piece into your own server.

The model
---------

| ``demo/Design.xml`` — byte-identical to the ``pubsub`` case CI builds
  and boots on the matrix's main platforms on every push. Nothing in it
  is Pub/Sub-specific:
  they are ordinary cache variables, which is the point — any cache
  variable of a supported type can be published.

.. literalinclude:: ../../demo/Design.xml
   :language: xml

The behavior
------------

| The one file quasar asks you to write. The main loop ticks the values
  the publisher will sample (``demo/QuasarServer.cpp``):

.. literalinclude:: ../../demo/QuasarServer.cpp
   :language: cpp
   :start-at: void QuasarServer::mainLoop()
   :end-before: void QuasarServer::initialize()

The wire declaration
--------------------

| ``demo/config.xml`` — the entire Pub/Sub setup. One publisher
  (``publisherId`` 77) with two DataSetWriters on one 100 ms WriterGroup,
  and a ``DataSetReader`` on the same connection through which the server
  receives its own counter feed into ``PS1.mirror``:

.. literalinclude:: ../../demo/config.xml
   :language: xml

| Element-by-element meaning: :doc:`PubSub`, section *Configuration file
  schema*.

Building it, on either stack
----------------------------

| The engine is backend-neutral: the same ``config.xml`` drives the same
  wire behaviour on both supported stacks. With the open-source backend
  (the demo's build, fresh Ubuntu):

.. code-block:: bash

   ./quasar.py enable_module open62541-compat v1.5.8
   ./quasar.py set_build_config open62541_config.cmake
   ./quasar.py generate device --all
   ./quasar.py build Release
   cd build/bin && ./OpcUaServer

| With the Unified Automation backend, only the build configuration
  changes — choose your UASDK build configuration with
  ``set_build_config`` as for any quasar server; the configuration file
  and the wire behaviour are identical. This is not an aspiration: the
  Windows CI builds and runs the same Pub/Sub case against UASDK 1.8.9
  on every push, next to the open62541 lane — and against UASDK 2.0.3
  nightly.

Receiving
---------

| Three receivers, three vantage points:

| **By name, in a terminal** — hypernova is the family's reference
  subscriber. The demo's registry gives the stream a name, and:

.. code-block:: bash

   hypernova sub demo/ps1/env --registry http://localhost:4850

| prints every sample, decoded, with sequence numbers. No OPC UA session
  exists anywhere on this path.

| **Server-to-server, C++** — the ``DataSetReader`` above is the receive
  side inside the same server. Browse ``PS1`` with any OPC UA client and
  watch ``mirror`` shadow ``counter`` one publishing interval behind:
  that value left the server as a UADP frame and came back through the
  UDP socket.

| **Server-to-server, across engines** — the Python engine's behavior
  script (``demo/kilonova_logic.py``): kilonova serves the same Design
  and ticks its own values; its ``mirror`` is fed from the *C++* server's
  frames by a hypernova subscriber writing through a generated setter.
  kilonova deliberately has no Pub/Sub stack — in the family, the Python
  wire lives in hypernova, and composition replaces reimplementation:

.. literalinclude:: ../../demo/kilonova_logic.py
   :language: python

Verified end to end
-------------------

| The claims on this page are enforced, not asserted:

-  the ``family-demo`` workflow runs the compose above on clean x86_64
   and arm64 runners on every demo change and fails unless changing
   values arrive within sixty seconds;
-  the ``pubsub`` case (this Design) builds and boots on every push on
   alma10 (x86_64 and arm64) and on Windows Server 2025, where it runs on
   both stacks (open62541 + UASDK 1.8.9; 2.0.3 nightly);
-  the wire codec has its own unit suite (``pubsub-tests`` workflow);
-  the :doc:`PubSub` configuration reference is checked against the
   generated schema by a hermetic CI test — an attribute added to the
   schema without documentation fails the build.
