# LICENSE:
# Copyright (c) 2026, CERN
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# uasdk-native.cmake
#
# Created on: 3 July 2026
# Author: Paris Moschovakos <paris.moschovakos@cern.ch>
#
# Minimal build config: sets only the toolkit path and lets FindOpcUaToolkit.cmake
# do the discovery (UASDKCPP package config on UA SDK >= 2.0, legacy hand-search
# otherwise). Used by CI to prove the config-mode path.

message( "using build configuration from uasdk-native.cmake (UASDKCPP package-config discovery)" )

SET( OPCUA_TOOLKIT_PATH /opt/uasdk )

# Same language standard as uasdk-eval.cmake, so this cell differs from the
# rest of the UASDK lane only in how the toolkit is discovered.
add_definitions( -Wall -Wno-deprecated -std=gnu++0x )
