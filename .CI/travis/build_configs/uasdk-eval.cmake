# Please see http://quasar.docs.cern.ch/quasarBuildSystem.html for
# information how to use this file.
# LICENSE:
# Copyright (c) 2015, CERN
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
# Author: Piotr Nikiel <piotr@nikiel.info>
# @author pnikiel
# @date 03-Sep-2015
# The purpose of this file is to set default parameters in case no build configuration file (aka toolchain) was specified.

# The approach is to satisfy the requirements as much as possible.

#-------
#Boost
#-------
# this will be resolved by BoostSetup.cmake since quasar 1.3.13, nothing to do

#------
#OPCUA
#------

SET( OPCUA_TOOLKIT_PATH /opt/uasdk )

# The installed UA SDK module set differs across majors. 1.x (1.6.5/1.7.9/1.8.9) ships
# uamodule + separate uapkicpp/xmlparsercpp; 2.0.x folds pki + xmlparser into uabasecpp,
# drops uamodule, and adds uabasedi/uaserverdi + an alternative embeddedstack (NOT linked --
# it conflicts with uastack). Detect the layout from a 1.x-only lib and pick the right
# module + include set so one config serves every toolkit version. link_directories(
# ${OPCUA_TOOLKIT_PATH}/lib ) is added by the top-level CMakeLists, so bare -l names resolve.
if( EXISTS "${OPCUA_TOOLKIT_PATH}/lib/libuamodule.a" )
        # UA SDK 1.x layout
        SET( OPCUA_TOOLKIT_LIBS_RELEASE "-luamodule -lcoremodule -luabasecpp -luastack -luapkicpp -lxmlparsercpp -lxml2 -lssl -lcrypto -lpthread" )
        SET( OPCUA_TOOLKIT_LIBS_DEBUG   "-luamoduled -lcoremoduled -luabasecppd -luastackd -luapkicppd -lxmlparsercppd -lxml2 -lssl -lcrypto -lpthread" )
        SET( _UASDK_INCDIRS uabasecpp uaservercpp uapkicpp uastack xmlparsercpp )
else()
        # UA SDK 2.0.x layout. embeddedstack provides the low-level C base (ua_malloc/ua_free,
        # ua_buffer_*, the ua_jdecode_* JSON decoder, ua_decoder_context_*) that uabasecpp links
        # against -- it is a required dependency, not an alternative to uastack. --start-group
        # lets ld resolve the mutually-referential 2.0.x static modules regardless of -l order.
        SET( OPCUA_TOOLKIT_LIBS_RELEASE "-Wl,--start-group -lcoremodule -luabasecpp -luastack -lembeddedstack -Wl,--end-group -lxml2 -lssl -lcrypto -lpthread" )
        SET( OPCUA_TOOLKIT_LIBS_DEBUG   "${OPCUA_TOOLKIT_LIBS_RELEASE}" )
        SET( _UASDK_INCDIRS uabasecpp uaservercpp uastack uaclientcpp )
endif()

add_custom_target( quasar_opcua_backend_is_ready )

include_directories ( ${OPCUA_TOOLKIT_PATH}/include )
foreach( _d ${_UASDK_INCDIRS} )
        include_directories ( ${OPCUA_TOOLKIT_PATH}/include/${_d} )
endforeach()

#-----
#XML Libs
#-----
#As of 03-Sep-2015 I see no FindXerces or whatever in our Cmake 2.8 installation, so no find_package can be user...
# TODO perhaps also take it from environment if requested
SET( XML_LIBS "-lxerces-c" )

#-----
#General settings
#-----

# TODO: split between Win / MSVC, perhaps MSVC has different notation for these
add_definitions(-Wall -Wno-deprecated -std=gnu++0x -DBACKEND_UATOOLKIT )
