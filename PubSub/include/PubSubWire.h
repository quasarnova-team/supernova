/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * PubSubWire.h
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

#ifndef PUBSUB_INCLUDE_PUBSUBWIRE_H_
#define PUBSUB_INCLUDE_PUBSUBWIRE_H_

#include <stdint.h>
#include <string>
#include <vector>

namespace PubSub
{

enum BuiltinType
{
    TypeNull     = 0,
    TypeBoolean  = 1,
    TypeSByte    = 2,
    TypeByte     = 3,
    TypeInt16    = 4,
    TypeUInt16   = 5,
    TypeInt32    = 6,
    TypeUInt32   = 7,
    TypeInt64    = 8,
    TypeUInt64   = 9,
    TypeFloat    = 10,
    TypeDouble   = 11,
    TypeString   = 12,
    TypeDateTime = 13
};

class WireValue
{
public:
    WireValue(): m_type(TypeNull), m_int(0), m_uint(0), m_float(0) {}

    static WireValue makeNull();
    static WireValue makeBoolean(bool value);
    static WireValue makeSigned(BuiltinType type, int64_t value);
    static WireValue makeUnsigned(BuiltinType type, uint64_t value);
    static WireValue makeFloat(float value);
    static WireValue makeDouble(double value);
    static WireValue makeString(const std::string& value);
    static WireValue makeDateTime(int64_t value);

    BuiltinType type() const { return m_type; }
    bool boolValue() const { return m_uint != 0; }
    int64_t signedValue() const { return m_int; }
    uint64_t unsignedValue() const { return m_uint; }
    double floatValue() const { return m_float; }
    const std::string& stringValue() const { return m_string; }

    bool equals(const WireValue& other) const;

private:
    BuiltinType m_type;
    int64_t     m_int;
    uint64_t    m_uint;
    double      m_float;
    std::string m_string;
};

enum PublisherIdType
{
    PublisherIdByte   = 0,
    PublisherIdUInt16 = 1,
    PublisherIdUInt32 = 2,
    PublisherIdUInt64 = 3
};

struct DataSetMessage
{
    DataSetMessage(): dataSetWriterId(0), sequenceNumberEnabled(false), sequenceNumber(0), keepAlive(false) {}

    uint16_t dataSetWriterId;
    bool     sequenceNumberEnabled;
    uint16_t sequenceNumber;
    bool     keepAlive;
    std::vector<WireValue> fields;
};

struct NetworkMessage
{
    NetworkMessage():
        publisherIdType(PublisherIdUInt16),
        publisherId(0),
        writerGroupIdEnabled(true),
        writerGroupId(0),
        groupSequenceNumberEnabled(true),
        groupSequenceNumber(0)
    {}

    PublisherIdType publisherIdType;
    uint64_t publisherId;
    bool     writerGroupIdEnabled;
    uint16_t writerGroupId;
    bool     groupSequenceNumberEnabled;
    uint16_t groupSequenceNumber;
    std::vector<DataSetMessage> messages;
};

std::vector<uint8_t> encodeNetworkMessage(const NetworkMessage& message);

bool decodeNetworkMessage(
    const uint8_t*  data,
    size_t          size,
    NetworkMessage& out,
    std::string&    diagnostic);

}

#endif /* PUBSUB_INCLUDE_PUBSUBWIRE_H_ */
