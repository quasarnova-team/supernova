/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * PubSubWire.cpp
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

#include <PubSubWire.h>

#include <cstring>
#include <stdexcept>

namespace PubSub
{

namespace
{

const uint8_t UadpVersion = 1;

const uint8_t NmPublisherIdEnabled   = 16;
const uint8_t NmGroupHeaderEnabled   = 32;
const uint8_t NmPayloadHeaderEnabled = 64;
const uint8_t NmExtendedFlags1       = 128;

const uint8_t Ext1PublisherIdTypeMask   = 7;
const uint8_t Ext1DataSetClassIdEnabled = 8;
const uint8_t Ext1SecurityEnabled       = 16;
const uint8_t Ext1TimestampEnabled      = 32;
const uint8_t Ext1PicosecondsEnabled    = 64;
const uint8_t Ext1ExtendedFlags2        = 128;

const uint8_t Ext2ChunkMessage          = 1;
const uint8_t Ext2PromotedFieldsEnabled = 2;
const uint8_t Ext2NetworkMessageTypeMask = 28;

const uint8_t GroupWriterGroupIdEnabled        = 1;
const uint8_t GroupGroupVersionEnabled         = 2;
const uint8_t GroupNetworkMessageNumberEnabled = 4;
const uint8_t GroupSequenceNumberEnabled       = 8;

const uint8_t DsmValid                     = 1;
const uint8_t DsmFieldEncodingMask         = 6;
const uint8_t DsmSequenceNumberEnabled     = 8;
const uint8_t DsmStatusEnabled             = 16;
const uint8_t DsmConfigMajorEnabled        = 32;
const uint8_t DsmConfigMinorEnabled        = 64;
const uint8_t DsmFlags2Enabled             = 128;

const uint8_t Dsm2TypeMask           = 15;
const uint8_t Dsm2TimestampEnabled   = 16;
const uint8_t Dsm2PicosecondsEnabled = 32;

const uint8_t FieldEncodingVariant   = 0;
const uint8_t FieldEncodingRawData   = 1;

const uint8_t DsmTypeKeyFrame   = 0;
const uint8_t DsmTypeKeepAlive  = 3;

const uint8_t VariantArrayDimensionsFlag = 0x40;
const uint8_t VariantArrayValuesFlag     = 0x80;

void putU8(std::vector<uint8_t>& out, uint8_t v)
{
    out.push_back(v);
}

void putU16(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void putU32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

void putU64(std::vector<uint8_t>& out, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

void putString(std::vector<uint8_t>& out, const std::string& s)
{
    putU32(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

void encodeVariant(std::vector<uint8_t>& out, const WireValue& value)
{
    putU8(out, static_cast<uint8_t>(value.type()));
    switch (value.type())
    {
        case TypeNull:
            break;
        case TypeBoolean:
            putU8(out, value.boolValue() ? 1 : 0);
            break;
        case TypeSByte:
            putU8(out, static_cast<uint8_t>(static_cast<int8_t>(value.signedValue())));
            break;
        case TypeByte:
            putU8(out, static_cast<uint8_t>(value.unsignedValue()));
            break;
        case TypeInt16:
            putU16(out, static_cast<uint16_t>(static_cast<int16_t>(value.signedValue())));
            break;
        case TypeUInt16:
            putU16(out, static_cast<uint16_t>(value.unsignedValue()));
            break;
        case TypeInt32:
            putU32(out, static_cast<uint32_t>(static_cast<int32_t>(value.signedValue())));
            break;
        case TypeUInt32:
            putU32(out, static_cast<uint32_t>(value.unsignedValue()));
            break;
        case TypeInt64:
            putU64(out, static_cast<uint64_t>(value.signedValue()));
            break;
        case TypeUInt64:
            putU64(out, value.unsignedValue());
            break;
        case TypeFloat:
        {
            float f = static_cast<float>(value.floatValue());
            uint32_t bits;
            std::memcpy(&bits, &f, sizeof bits);
            putU32(out, bits);
            break;
        }
        case TypeDouble:
        {
            double d = value.floatValue();
            uint64_t bits;
            std::memcpy(&bits, &d, sizeof bits);
            putU64(out, bits);
            break;
        }
        case TypeString:
            putString(out, value.stringValue());
            break;
        case TypeDateTime:
            putU64(out, static_cast<uint64_t>(value.signedValue()));
            break;
        default:
            throw std::runtime_error("PubSub wire encoder: unsupported field type");
    }
}

void encodeDataSetMessage(std::vector<uint8_t>& out, const DataSetMessage& message)
{
    uint8_t flags1 = DsmValid | (FieldEncodingVariant << 1);
    if (message.sequenceNumberEnabled)
        flags1 |= DsmSequenceNumberEnabled;
    putU8(out, flags1);
    if (message.sequenceNumberEnabled)
        putU16(out, message.sequenceNumber);
    putU16(out, static_cast<uint16_t>(message.fields.size()));
    for (size_t i = 0; i < message.fields.size(); i++)
        encodeVariant(out, message.fields[i]);
}

class Reader
{
public:
    Reader(const uint8_t* data, size_t size): m_data(data), m_size(size), m_position(0) {}

    bool u8(uint8_t& v)
    {
        if (m_position + 1 > m_size) return false;
        v = m_data[m_position++];
        return true;
    }

    bool u16(uint16_t& v)
    {
        if (m_position + 2 > m_size) return false;
        v = static_cast<uint16_t>(m_data[m_position] | (m_data[m_position + 1] << 8));
        m_position += 2;
        return true;
    }

    bool u32(uint32_t& v)
    {
        if (m_position + 4 > m_size) return false;
        v = 0;
        for (int i = 0; i < 4; i++)
            v |= static_cast<uint32_t>(m_data[m_position + i]) << (8 * i);
        m_position += 4;
        return true;
    }

    bool u64(uint64_t& v)
    {
        if (m_position + 8 > m_size) return false;
        v = 0;
        for (int i = 0; i < 8; i++)
            v |= static_cast<uint64_t>(m_data[m_position + i]) << (8 * i);
        m_position += 8;
        return true;
    }

    bool str(std::string& v)
    {
        uint32_t length;
        if (!u32(length)) return false;
        int32_t signedLength = static_cast<int32_t>(length);
        if (signedLength <= 0)
        {
            v.clear();
            return true;
        }
        if (m_position + static_cast<size_t>(signedLength) > m_size) return false;
        v.assign(reinterpret_cast<const char*>(m_data + m_position), static_cast<size_t>(signedLength));
        m_position += static_cast<size_t>(signedLength);
        return true;
    }

    bool skip(size_t n)
    {
        if (m_position + n > m_size) return false;
        m_position += n;
        return true;
    }

    size_t remaining() const { return m_size - m_position; }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_position;
};

bool decodeVariant(Reader& reader, WireValue& out, std::string& diagnostic)
{
    uint8_t mask;
    if (!reader.u8(mask)) { diagnostic = "truncated variant mask"; return false; }
    if (mask & (VariantArrayValuesFlag | VariantArrayDimensionsFlag))
    {
        diagnostic = "array-valued fields are not supported";
        return false;
    }
    uint8_t type = mask & 0x3F;
    switch (type)
    {
        case TypeNull:
            out = WireValue::makeNull();
            return true;
        case TypeBoolean:
        {
            uint8_t v;
            if (!reader.u8(v)) { diagnostic = "truncated Boolean"; return false; }
            out = WireValue::makeBoolean(v != 0);
            return true;
        }
        case TypeSByte:
        {
            uint8_t v;
            if (!reader.u8(v)) { diagnostic = "truncated SByte"; return false; }
            out = WireValue::makeSigned(TypeSByte, static_cast<int8_t>(v));
            return true;
        }
        case TypeByte:
        {
            uint8_t v;
            if (!reader.u8(v)) { diagnostic = "truncated Byte"; return false; }
            out = WireValue::makeUnsigned(TypeByte, v);
            return true;
        }
        case TypeInt16:
        {
            uint16_t v;
            if (!reader.u16(v)) { diagnostic = "truncated Int16"; return false; }
            out = WireValue::makeSigned(TypeInt16, static_cast<int16_t>(v));
            return true;
        }
        case TypeUInt16:
        {
            uint16_t v;
            if (!reader.u16(v)) { diagnostic = "truncated UInt16"; return false; }
            out = WireValue::makeUnsigned(TypeUInt16, v);
            return true;
        }
        case TypeInt32:
        {
            uint32_t v;
            if (!reader.u32(v)) { diagnostic = "truncated Int32"; return false; }
            out = WireValue::makeSigned(TypeInt32, static_cast<int32_t>(v));
            return true;
        }
        case TypeUInt32:
        {
            uint32_t v;
            if (!reader.u32(v)) { diagnostic = "truncated UInt32"; return false; }
            out = WireValue::makeUnsigned(TypeUInt32, v);
            return true;
        }
        case TypeInt64:
        {
            uint64_t v;
            if (!reader.u64(v)) { diagnostic = "truncated Int64"; return false; }
            out = WireValue::makeSigned(TypeInt64, static_cast<int64_t>(v));
            return true;
        }
        case TypeUInt64:
        {
            uint64_t v;
            if (!reader.u64(v)) { diagnostic = "truncated UInt64"; return false; }
            out = WireValue::makeUnsigned(TypeUInt64, v);
            return true;
        }
        case TypeFloat:
        {
            uint32_t bits;
            if (!reader.u32(bits)) { diagnostic = "truncated Float"; return false; }
            float f;
            std::memcpy(&f, &bits, sizeof f);
            out = WireValue::makeFloat(f);
            return true;
        }
        case TypeDouble:
        {
            uint64_t bits;
            if (!reader.u64(bits)) { diagnostic = "truncated Double"; return false; }
            double d;
            std::memcpy(&d, &bits, sizeof d);
            out = WireValue::makeDouble(d);
            return true;
        }
        case TypeString:
        {
            std::string s;
            if (!reader.str(s)) { diagnostic = "truncated String"; return false; }
            out = WireValue::makeString(s);
            return true;
        }
        case TypeDateTime:
        {
            uint64_t v;
            if (!reader.u64(v)) { diagnostic = "truncated DateTime"; return false; }
            out = WireValue::makeDateTime(static_cast<int64_t>(v));
            return true;
        }
        default:
            diagnostic = "unsupported variant type " + std::to_string(static_cast<int>(type));
            return false;
    }
}

bool decodeDataValueField(Reader& reader, WireValue& out, std::string& diagnostic)
{
    uint8_t mask;
    if (!reader.u8(mask)) { diagnostic = "truncated DataValue mask"; return false; }
    out = WireValue::makeNull();
    if (mask & 1)
    {
        if (!decodeVariant(reader, out, diagnostic)) return false;
    }
    if ((mask & 2) && !reader.skip(4)) { diagnostic = "truncated DataValue status"; return false; }
    if ((mask & 4) && !reader.skip(8)) { diagnostic = "truncated DataValue source timestamp"; return false; }
    if ((mask & 8) && !reader.skip(8)) { diagnostic = "truncated DataValue server timestamp"; return false; }
    if ((mask & 16) && !reader.skip(2)) { diagnostic = "truncated DataValue source picoseconds"; return false; }
    if ((mask & 32) && !reader.skip(2)) { diagnostic = "truncated DataValue server picoseconds"; return false; }
    return true;
}

bool decodeDataSetMessage(Reader& reader, DataSetMessage& out, std::string& diagnostic)
{
    uint8_t flags1;
    if (!reader.u8(flags1)) { diagnostic = "truncated DataSetFlags1"; return false; }

    uint8_t fieldEncoding = (flags1 & DsmFieldEncodingMask) >> 1;
    uint8_t messageType = DsmTypeKeyFrame;
    bool timestampPresent = false;
    bool picosecondsPresent = false;

    if (flags1 & DsmFlags2Enabled)
    {
        uint8_t flags2;
        if (!reader.u8(flags2)) { diagnostic = "truncated DataSetFlags2"; return false; }
        messageType = flags2 & Dsm2TypeMask;
        timestampPresent = (flags2 & Dsm2TimestampEnabled) != 0;
        picosecondsPresent = (flags2 & Dsm2PicosecondsEnabled) != 0;
    }

    if (flags1 & DsmSequenceNumberEnabled)
    {
        out.sequenceNumberEnabled = true;
        if (!reader.u16(out.sequenceNumber)) { diagnostic = "truncated DataSetMessage sequence number"; return false; }
    }
    if (timestampPresent && !reader.skip(8)) { diagnostic = "truncated DataSetMessage timestamp"; return false; }
    if (picosecondsPresent && !reader.skip(2)) { diagnostic = "truncated DataSetMessage picoseconds"; return false; }
    if ((flags1 & DsmStatusEnabled) && !reader.skip(2)) { diagnostic = "truncated DataSetMessage status"; return false; }
    if ((flags1 & DsmConfigMajorEnabled) && !reader.skip(4)) { diagnostic = "truncated config major version"; return false; }
    if ((flags1 & DsmConfigMinorEnabled) && !reader.skip(4)) { diagnostic = "truncated config minor version"; return false; }

    if (messageType == DsmTypeKeepAlive)
    {
        out.keepAlive = true;
        return true;
    }
    if (messageType != DsmTypeKeyFrame)
    {
        diagnostic = "unsupported DataSetMessage type " + std::to_string(static_cast<int>(messageType));
        return false;
    }
    if (fieldEncoding == FieldEncodingRawData)
    {
        diagnostic = "RawData field encoding is not supported";
        return false;
    }

    uint16_t fieldCount;
    if (!reader.u16(fieldCount)) { diagnostic = "truncated field count"; return false; }
    out.fields.reserve(fieldCount);
    for (uint16_t i = 0; i < fieldCount; i++)
    {
        WireValue value;
        bool ok = (fieldEncoding == FieldEncodingVariant)
            ? decodeVariant(reader, value, diagnostic)
            : decodeDataValueField(reader, value, diagnostic);
        if (!ok) return false;
        out.fields.push_back(value);
    }
    return true;
}

}

WireValue WireValue::makeNull()
{
    return WireValue();
}

WireValue WireValue::makeBoolean(bool value)
{
    WireValue v;
    v.m_type = TypeBoolean;
    v.m_uint = value ? 1 : 0;
    return v;
}

WireValue WireValue::makeSigned(BuiltinType type, int64_t value)
{
    WireValue v;
    v.m_type = type;
    v.m_int = value;
    return v;
}

WireValue WireValue::makeUnsigned(BuiltinType type, uint64_t value)
{
    WireValue v;
    v.m_type = type;
    v.m_uint = value;
    return v;
}

WireValue WireValue::makeFloat(float value)
{
    WireValue v;
    v.m_type = TypeFloat;
    v.m_float = value;
    return v;
}

WireValue WireValue::makeDouble(double value)
{
    WireValue v;
    v.m_type = TypeDouble;
    v.m_float = value;
    return v;
}

WireValue WireValue::makeString(const std::string& value)
{
    WireValue v;
    v.m_type = TypeString;
    v.m_string = value;
    return v;
}

WireValue WireValue::makeDateTime(int64_t value)
{
    WireValue v;
    v.m_type = TypeDateTime;
    v.m_int = value;
    return v;
}

bool WireValue::equals(const WireValue& other) const
{
    if (m_type != other.m_type) return false;
    switch (m_type)
    {
        case TypeNull:     return true;
        case TypeBoolean:  return boolValue() == other.boolValue();
        case TypeSByte:
        case TypeInt16:
        case TypeInt32:
        case TypeInt64:
        case TypeDateTime: return m_int == other.m_int;
        case TypeByte:
        case TypeUInt16:
        case TypeUInt32:
        case TypeUInt64:   return m_uint == other.m_uint;
        case TypeFloat:
        case TypeDouble:   return m_float == other.m_float;
        case TypeString:   return m_string == other.m_string;
        default:           return false;
    }
}

std::vector<uint8_t> encodeNetworkMessage(const NetworkMessage& message)
{
    if (message.messages.empty())
        throw std::runtime_error("PubSub wire encoder: no DataSetMessages to encode");
    if (message.messages.size() > 255)
        throw std::runtime_error("PubSub wire encoder: too many DataSetMessages for one NetworkMessage");

    std::vector<uint8_t> out;
    out.reserve(64);

    bool groupHeaderEnabled = message.writerGroupIdEnabled || message.groupSequenceNumberEnabled;
    bool extendedFlags1 = message.publisherIdType != PublisherIdByte;

    uint8_t flags = UadpVersion | NmPublisherIdEnabled | NmPayloadHeaderEnabled;
    if (groupHeaderEnabled)
        flags |= NmGroupHeaderEnabled;
    if (extendedFlags1)
        flags |= NmExtendedFlags1;
    putU8(out, flags);

    if (extendedFlags1)
        putU8(out, static_cast<uint8_t>(message.publisherIdType));

    switch (message.publisherIdType)
    {
        case PublisherIdByte:   putU8(out, static_cast<uint8_t>(message.publisherId)); break;
        case PublisherIdUInt16: putU16(out, static_cast<uint16_t>(message.publisherId)); break;
        case PublisherIdUInt32: putU32(out, static_cast<uint32_t>(message.publisherId)); break;
        case PublisherIdUInt64: putU64(out, message.publisherId); break;
        default: throw std::runtime_error("PubSub wire encoder: unsupported publisher id type");
    }

    if (groupHeaderEnabled)
    {
        uint8_t groupFlags = 0;
        if (message.writerGroupIdEnabled)
            groupFlags |= GroupWriterGroupIdEnabled;
        if (message.groupSequenceNumberEnabled)
            groupFlags |= GroupSequenceNumberEnabled;
        putU8(out, groupFlags);
        if (message.writerGroupIdEnabled)
            putU16(out, message.writerGroupId);
        if (message.groupSequenceNumberEnabled)
            putU16(out, message.groupSequenceNumber);
    }

    putU8(out, static_cast<uint8_t>(message.messages.size()));
    for (size_t i = 0; i < message.messages.size(); i++)
        putU16(out, message.messages[i].dataSetWriterId);

    std::vector< std::vector<uint8_t> > bodies(message.messages.size());
    for (size_t i = 0; i < message.messages.size(); i++)
        encodeDataSetMessage(bodies[i], message.messages[i]);

    if (message.messages.size() > 1)
    {
        for (size_t i = 0; i < bodies.size(); i++)
        {
            if (bodies[i].size() > 0xFFFF)
                throw std::runtime_error("PubSub wire encoder: DataSetMessage too large");
            putU16(out, static_cast<uint16_t>(bodies[i].size()));
        }
    }
    for (size_t i = 0; i < bodies.size(); i++)
        out.insert(out.end(), bodies[i].begin(), bodies[i].end());

    return out;
}

bool decodeNetworkMessage(
    const uint8_t*  data,
    size_t          size,
    NetworkMessage& out,
    std::string&    diagnostic)
{
    Reader reader(data, size);

    uint8_t flags;
    if (!reader.u8(flags)) { diagnostic = "empty datagram"; return false; }
    if ((flags & 0x0F) != UadpVersion)
    {
        diagnostic = "unsupported UADP version " + std::to_string(flags & 0x0F);
        return false;
    }

    bool publisherIdEnabled = (flags & NmPublisherIdEnabled) != 0;
    bool groupHeaderEnabled = (flags & NmGroupHeaderEnabled) != 0;
    bool payloadHeaderEnabled = (flags & NmPayloadHeaderEnabled) != 0;

    uint8_t publisherIdType = PublisherIdByte;
    bool dataSetClassIdPresent = false;
    bool securityPresent = false;
    bool timestampPresent = false;
    bool picosecondsPresent = false;
    bool promotedFieldsPresent = false;

    if (flags & NmExtendedFlags1)
    {
        uint8_t ext1;
        if (!reader.u8(ext1)) { diagnostic = "truncated ExtendedFlags1"; return false; }
        publisherIdType = ext1 & Ext1PublisherIdTypeMask;
        dataSetClassIdPresent = (ext1 & Ext1DataSetClassIdEnabled) != 0;
        securityPresent = (ext1 & Ext1SecurityEnabled) != 0;
        timestampPresent = (ext1 & Ext1TimestampEnabled) != 0;
        picosecondsPresent = (ext1 & Ext1PicosecondsEnabled) != 0;
        if (ext1 & Ext1ExtendedFlags2)
        {
            uint8_t ext2;
            if (!reader.u8(ext2)) { diagnostic = "truncated ExtendedFlags2"; return false; }
            if (ext2 & Ext2ChunkMessage) { diagnostic = "chunked NetworkMessages are not supported"; return false; }
            promotedFieldsPresent = (ext2 & Ext2PromotedFieldsEnabled) != 0;
            if ((ext2 & Ext2NetworkMessageTypeMask) != 0)
            {
                diagnostic = "not a DataSetMessage payload";
                return false;
            }
        }
    }

    if (publisherIdEnabled)
    {
        switch (publisherIdType)
        {
            case PublisherIdByte:
            {
                uint8_t v;
                if (!reader.u8(v)) { diagnostic = "truncated publisher id"; return false; }
                out.publisherId = v;
                break;
            }
            case PublisherIdUInt16:
            {
                uint16_t v;
                if (!reader.u16(v)) { diagnostic = "truncated publisher id"; return false; }
                out.publisherId = v;
                break;
            }
            case PublisherIdUInt32:
            {
                uint32_t v;
                if (!reader.u32(v)) { diagnostic = "truncated publisher id"; return false; }
                out.publisherId = v;
                break;
            }
            case PublisherIdUInt64:
            {
                uint64_t v;
                if (!reader.u64(v)) { diagnostic = "truncated publisher id"; return false; }
                out.publisherId = v;
                break;
            }
            default:
                diagnostic = "unsupported publisher id type " + std::to_string(static_cast<int>(publisherIdType));
                return false;
        }
        out.publisherIdType = static_cast<PublisherIdType>(publisherIdType);
    }

    if (dataSetClassIdPresent && !reader.skip(16)) { diagnostic = "truncated DataSetClassId"; return false; }

    out.writerGroupIdEnabled = false;
    out.groupSequenceNumberEnabled = false;
    if (groupHeaderEnabled)
    {
        uint8_t groupFlags;
        if (!reader.u8(groupFlags)) { diagnostic = "truncated GroupHeader flags"; return false; }
        if (groupFlags & GroupWriterGroupIdEnabled)
        {
            out.writerGroupIdEnabled = true;
            if (!reader.u16(out.writerGroupId)) { diagnostic = "truncated WriterGroupId"; return false; }
        }
        if ((groupFlags & GroupGroupVersionEnabled) && !reader.skip(4)) { diagnostic = "truncated GroupVersion"; return false; }
        if ((groupFlags & GroupNetworkMessageNumberEnabled) && !reader.skip(2)) { diagnostic = "truncated NetworkMessageNumber"; return false; }
        if (groupFlags & GroupSequenceNumberEnabled)
        {
            out.groupSequenceNumberEnabled = true;
            if (!reader.u16(out.groupSequenceNumber)) { diagnostic = "truncated group SequenceNumber"; return false; }
        }
    }

    uint8_t messageCount = 1;
    std::vector<uint16_t> writerIds;
    if (payloadHeaderEnabled)
    {
        if (!reader.u8(messageCount)) { diagnostic = "truncated PayloadHeader count"; return false; }
        if (messageCount == 0) { diagnostic = "PayloadHeader declares zero DataSetMessages"; return false; }
        writerIds.resize(messageCount);
        for (uint8_t i = 0; i < messageCount; i++)
        {
            if (!reader.u16(writerIds[i])) { diagnostic = "truncated PayloadHeader writer ids"; return false; }
        }
    }

    if (timestampPresent && !reader.skip(8)) { diagnostic = "truncated NetworkMessage timestamp"; return false; }
    if (picosecondsPresent && !reader.skip(2)) { diagnostic = "truncated NetworkMessage picoseconds"; return false; }

    if (promotedFieldsPresent)
    {
        uint16_t promotedSize;
        if (!reader.u16(promotedSize)) { diagnostic = "truncated PromotedFields size"; return false; }
        if (!reader.skip(promotedSize)) { diagnostic = "truncated PromotedFields"; return false; }
    }

    if (securityPresent)
    {
        diagnostic = "secured NetworkMessages are not supported";
        return false;
    }

    if (payloadHeaderEnabled && messageCount > 1)
    {
        for (uint8_t i = 0; i < messageCount; i++)
        {
            uint16_t ignored;
            if (!reader.u16(ignored)) { diagnostic = "truncated DataSetMessage sizes"; return false; }
        }
    }

    out.messages.clear();
    out.messages.resize(messageCount);
    for (uint8_t i = 0; i < messageCount; i++)
    {
        out.messages[i].dataSetWriterId = payloadHeaderEnabled ? writerIds[i] : 0;
        if (!decodeDataSetMessage(reader, out.messages[i], diagnostic))
            return false;
    }

    return true;
}

}
