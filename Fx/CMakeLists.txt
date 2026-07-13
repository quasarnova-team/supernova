# © Copyright CERN, 2026. All rights not expressly granted are reserved.
# Author: Paris Moschovakos <paris@moschovakos.com>

add_library( Fx OBJECT
    src/FxJson.cpp
)

add_dependencies( Fx Configuration.hxx_GENERATED )
