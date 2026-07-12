# © Copyright CERN, 2026. All rights not expressly granted are reserved.
# Author: Paris Moschovakos <paris@moschovakos.com>

add_library( PubSub OBJECT
    src/PubSubWire.cpp
    src/PubSubConfiguration.cpp
    src/PubSubUdp.cpp
    src/PubSubEngine.cpp
)
