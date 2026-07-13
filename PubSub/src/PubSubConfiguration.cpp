/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * PubSubConfiguration.cpp
 *
 *  Created on: 12 Jul 2026
 *      Author: Paris Moschovakos
 *
 *  This file is part of Quasar.
 *
 *  Quasar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public Licence as published by
 *  the Free Software Foundation, either version 3 of the Licence.
 *
 *  Quasar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public Licence for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Quasar.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <PubSubConfiguration.h>

#include <stdexcept>

namespace PubSub
{

PublisherIdType parsePublisherIdType(const std::string& text)
{
    if (text == "Byte")   return PublisherIdByte;
    if (text == "UInt16") return PublisherIdUInt16;
    if (text == "UInt32") return PublisherIdUInt32;
    if (text == "UInt64") return PublisherIdUInt64;
    throw std::runtime_error("PubSub configuration: unknown publisherIdType '" + text + "' (expected Byte, UInt16, UInt32 or UInt64)");
}

const char* publisherIdTypeName(PublisherIdType type)
{
    switch (type)
    {
        case PublisherIdByte:   return "Byte";
        case PublisherIdUInt16: return "UInt16";
        case PublisherIdUInt32: return "UInt32";
        case PublisherIdUInt64: return "UInt64";
    }
    return "UInt16";
}

uint64_t publisherIdMaximum(PublisherIdType type)
{
    switch (type)
    {
        case PublisherIdByte:   return 0xFFull;
        case PublisherIdUInt16: return 0xFFFFull;
        case PublisherIdUInt32: return 0xFFFFFFFFull;
        case PublisherIdUInt64: return 0xFFFFFFFFFFFFFFFFull;
    }
    return 0xFFFFull;
}

void parseNetworkAddress(const std::string& address, std::string& host, uint16_t& port)
{
    const std::string scheme = "opc.udp://";
    if (address.compare(0, scheme.size(), scheme) != 0)
        throw std::runtime_error("PubSub configuration: address '" + address + "' does not start with opc.udp://");
    std::string rest = address.substr(scheme.size());
    while (!rest.empty() && rest[rest.size() - 1] == '/')
        rest.erase(rest.size() - 1);
    size_t colon = rest.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon == rest.size() - 1)
        throw std::runtime_error("PubSub configuration: address '" + address + "' lacks a :port");
    host = rest.substr(0, colon);
    std::string portText = rest.substr(colon + 1);
    int parsed = 0;
    for (size_t i = 0; i < portText.size(); i++)
    {
        if (portText[i] < '0' || portText[i] > '9')
            throw std::runtime_error("PubSub configuration: non-numeric port in address '" + address + "'");
        parsed = parsed * 10 + (portText[i] - '0');
        if (parsed > 65535)
            throw std::runtime_error("PubSub configuration: port out of range in address '" + address + "'");
    }
    if (parsed == 0)
        throw std::runtime_error("PubSub configuration: port out of range in address '" + address + "'");
    port = static_cast<uint16_t>(parsed);
}

}
