/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * PubSubEngine.cpp
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

#include <PubSubEngine.h>

#include <stdexcept>

#include <ArrayTools.h>
#include <ASNodeManager.h>
#include <ChangeNotifyingVariable.h>
#include <LogIt.h>

namespace PubSub
{

namespace
{

template <typename CppType>
bool arrayToWireSigned(const UaVariant& variant, BuiltinType type, WireValue& out)
{
    std::vector<CppType> values;
    if (!AddressSpace::ArrayTools::convertUaVariantToVector(variant, values).isGood()) return false;
    std::vector<WireValue> elements;
    elements.reserve(values.size());
    for (size_t i = 0; i < values.size(); i++)
        elements.push_back(WireValue::makeSigned(type, static_cast<int64_t>(values[i])));
    out = WireValue::makeArray(type, elements);
    return true;
}

template <typename CppType>
bool arrayToWireUnsigned(const UaVariant& variant, BuiltinType type, WireValue& out)
{
    std::vector<CppType> values;
    if (!AddressSpace::ArrayTools::convertUaVariantToVector(variant, values).isGood()) return false;
    std::vector<WireValue> elements;
    elements.reserve(values.size());
    for (size_t i = 0; i < values.size(); i++)
        elements.push_back(WireValue::makeUnsigned(type, static_cast<uint64_t>(values[i])));
    out = WireValue::makeArray(type, elements);
    return true;
}

bool uaArrayToWire(const UaVariant& variant, WireValue& out)
{
    namespace tools = AddressSpace::ArrayTools;
    switch (variant.type())
    {
        case OpcUaType_Boolean:
        {
            std::vector<OpcUa_Boolean> values;
            if (!tools::convertUaVariantToBooleanVector(variant, values).isGood()) return false;
            std::vector<WireValue> elements;
            for (size_t i = 0; i < values.size(); i++)
                elements.push_back(WireValue::makeBoolean(values[i] != OpcUa_False));
            out = WireValue::makeArray(TypeBoolean, elements);
            return true;
        }
        case OpcUaType_Byte:
        {
            std::vector<OpcUa_Byte> values;
            if (!tools::convertUaVariantToByteVector(variant, values).isGood()) return false;
            std::vector<WireValue> elements;
            for (size_t i = 0; i < values.size(); i++)
                elements.push_back(WireValue::makeUnsigned(TypeByte, values[i]));
            out = WireValue::makeArray(TypeByte, elements);
            return true;
        }
        case OpcUaType_SByte:  return arrayToWireSigned<OpcUa_SByte>(variant, TypeSByte, out);
        case OpcUaType_Int16:  return arrayToWireSigned<OpcUa_Int16>(variant, TypeInt16, out);
        case OpcUaType_UInt16: return arrayToWireUnsigned<OpcUa_UInt16>(variant, TypeUInt16, out);
        case OpcUaType_Int32:  return arrayToWireSigned<OpcUa_Int32>(variant, TypeInt32, out);
        case OpcUaType_UInt32: return arrayToWireUnsigned<OpcUa_UInt32>(variant, TypeUInt32, out);
        case OpcUaType_Int64:  return arrayToWireSigned<OpcUa_Int64>(variant, TypeInt64, out);
        case OpcUaType_UInt64: return arrayToWireUnsigned<OpcUa_UInt64>(variant, TypeUInt64, out);
        case OpcUaType_Float:
        {
            std::vector<OpcUa_Float> values;
            if (!tools::convertUaVariantToVector(variant, values).isGood()) return false;
            std::vector<WireValue> elements;
            for (size_t i = 0; i < values.size(); i++)
                elements.push_back(WireValue::makeFloat(values[i]));
            out = WireValue::makeArray(TypeFloat, elements);
            return true;
        }
        case OpcUaType_Double:
        {
            std::vector<OpcUa_Double> values;
            if (!tools::convertUaVariantToVector(variant, values).isGood()) return false;
            std::vector<WireValue> elements;
            for (size_t i = 0; i < values.size(); i++)
                elements.push_back(WireValue::makeDouble(values[i]));
            out = WireValue::makeArray(TypeDouble, elements);
            return true;
        }
        case OpcUaType_String:
        {
            std::vector<UaString> values;
            if (!tools::convertUaVariantToVector(variant, values).isGood()) return false;
            std::vector<WireValue> elements;
            for (size_t i = 0; i < values.size(); i++)
                elements.push_back(WireValue::makeString(
                    values[i].toUtf8() ? std::string(values[i].toUtf8()) : std::string()));
            out = WireValue::makeArray(TypeString, elements);
            return true;
        }
        default:
            return false;
    }
}

bool uaVariantToWire(const UaVariant& variant, WireValue& out)
{
    if (variant.isArray())
        return uaArrayToWire(variant, out);
    switch (variant.type())
    {
        case OpcUaType_Null:
            out = WireValue::makeNull();
            return true;
        case OpcUaType_Boolean:
        {
            OpcUa_Boolean v = OpcUa_False;
            if (!UaStatus(variant.toBool(v)).isGood()) return false;
            out = WireValue::makeBoolean(v != OpcUa_False);
            return true;
        }
        case OpcUaType_Byte:
        {
            OpcUa_Byte v = 0;
            if (!UaStatus(variant.toByte(v)).isGood()) return false;
            out = WireValue::makeUnsigned(TypeByte, v);
            return true;
        }
        case OpcUaType_Int16:
        {
            OpcUa_Int16 v = 0;
            if (!UaStatus(variant.toInt16(v)).isGood()) return false;
            out = WireValue::makeSigned(TypeInt16, v);
            return true;
        }
        case OpcUaType_UInt16:
        {
            OpcUa_UInt16 v = 0;
            if (!UaStatus(variant.toUInt16(v)).isGood()) return false;
            out = WireValue::makeUnsigned(TypeUInt16, v);
            return true;
        }
        case OpcUaType_Int32:
        {
            OpcUa_Int32 v = 0;
            if (!UaStatus(variant.toInt32(v)).isGood()) return false;
            out = WireValue::makeSigned(TypeInt32, v);
            return true;
        }
        case OpcUaType_UInt32:
        {
            OpcUa_UInt32 v = 0;
            if (!UaStatus(variant.toUInt32(v)).isGood()) return false;
            out = WireValue::makeUnsigned(TypeUInt32, v);
            return true;
        }
        case OpcUaType_Int64:
        {
            OpcUa_Int64 v = 0;
            if (!UaStatus(variant.toInt64(v)).isGood()) return false;
            out = WireValue::makeSigned(TypeInt64, v);
            return true;
        }
        case OpcUaType_UInt64:
        {
            OpcUa_UInt64 v = 0;
            if (!UaStatus(variant.toUInt64(v)).isGood()) return false;
            out = WireValue::makeUnsigned(TypeUInt64, v);
            return true;
        }
        case OpcUaType_Float:
        {
            OpcUa_Float v = 0;
            if (!UaStatus(variant.toFloat(v)).isGood()) return false;
            out = WireValue::makeFloat(v);
            return true;
        }
        case OpcUaType_Double:
        {
            OpcUa_Double v = 0;
            if (!UaStatus(variant.toDouble(v)).isGood()) return false;
            out = WireValue::makeDouble(v);
            return true;
        }
        case OpcUaType_String:
        {
            UaString v = variant.toString();
            out = WireValue::makeString(v.toUtf8() ? std::string(v.toUtf8()) : std::string());
            return true;
        }
        default:
            return false;
    }
}

template <typename CppType>
std::vector<CppType> wireArraySigned(const WireValue& value)
{
    std::vector<CppType> values;
    values.reserve(value.elements().size());
    for (size_t i = 0; i < value.elements().size(); i++)
        values.push_back(static_cast<CppType>(value.elements()[i].signedValue()));
    return values;
}

template <typename CppType>
std::vector<CppType> wireArrayUnsigned(const WireValue& value)
{
    std::vector<CppType> values;
    values.reserve(value.elements().size());
    for (size_t i = 0; i < value.elements().size(); i++)
        values.push_back(static_cast<CppType>(value.elements()[i].unsignedValue()));
    return values;
}

bool wireArrayToUa(const WireValue& value, UaVariant& out)
{
    namespace tools = AddressSpace::ArrayTools;
    switch (value.type())
    {
        case TypeBoolean:
        {
            std::vector<OpcUa_Boolean> values;
            values.reserve(value.elements().size());
            for (size_t i = 0; i < value.elements().size(); i++)
                values.push_back(value.elements()[i].boolValue() ? OpcUa_True : OpcUa_False);
            tools::convertBooleanVectorToUaVariant(values, out);
            return true;
        }
        case TypeByte:
        {
            std::vector<OpcUa_Byte> values = wireArrayUnsigned<OpcUa_Byte>(value);
            tools::convertByteVectorToUaVariant(values, out);
            return true;
        }
        case TypeSByte:
        {
            std::vector<OpcUa_SByte> values = wireArraySigned<OpcUa_SByte>(value);
            tools::convertVectorToUaVariant(values, out);
            return true;
        }
        case TypeInt16:
        {
            std::vector<OpcUa_Int16> values = wireArraySigned<OpcUa_Int16>(value);
            tools::convertVectorToUaVariant(values, out);
            return true;
        }
        case TypeUInt16:
        {
            std::vector<OpcUa_UInt16> values = wireArrayUnsigned<OpcUa_UInt16>(value);
            tools::convertVectorToUaVariant(values, out);
            return true;
        }
        case TypeInt32:
        {
            std::vector<OpcUa_Int32> values = wireArraySigned<OpcUa_Int32>(value);
            tools::convertVectorToUaVariant(values, out);
            return true;
        }
        case TypeUInt32:
        {
            std::vector<OpcUa_UInt32> values = wireArrayUnsigned<OpcUa_UInt32>(value);
            tools::convertVectorToUaVariant(values, out);
            return true;
        }
        case TypeInt64:
        {
            std::vector<OpcUa_Int64> values = wireArraySigned<OpcUa_Int64>(value);
            tools::convertVectorToUaVariant(values, out);
            return true;
        }
        case TypeUInt64:
        {
            std::vector<OpcUa_UInt64> values = wireArrayUnsigned<OpcUa_UInt64>(value);
            tools::convertVectorToUaVariant(values, out);
            return true;
        }
        case TypeFloat:
        {
            std::vector<OpcUa_Float> values;
            values.reserve(value.elements().size());
            for (size_t i = 0; i < value.elements().size(); i++)
                values.push_back(static_cast<OpcUa_Float>(value.elements()[i].floatValue()));
            tools::convertVectorToUaVariant(values, out);
            return true;
        }
        case TypeDouble:
        {
            std::vector<OpcUa_Double> values;
            values.reserve(value.elements().size());
            for (size_t i = 0; i < value.elements().size(); i++)
                values.push_back(value.elements()[i].floatValue());
            tools::convertVectorToUaVariant(values, out);
            return true;
        }
        case TypeString:
        {
            std::vector<UaString> values;
            values.reserve(value.elements().size());
            for (size_t i = 0; i < value.elements().size(); i++)
                values.push_back(UaString(value.elements()[i].stringValue().c_str()));
            tools::convertVectorToUaVariant(values, out);
            return true;
        }
        default:
            return false;
    }
}

bool wireToUaVariant(const WireValue& value, UaVariant& out)
{
    if (value.isArray())
        return wireArrayToUa(value, out);
    switch (value.type())
    {
        case TypeBoolean:
            out.setBool(value.boolValue() ? OpcUa_True : OpcUa_False);
            return true;
        case TypeByte:
            out.setByte(static_cast<OpcUa_Byte>(value.unsignedValue()));
            return true;
        case TypeInt16:
            out.setInt16(static_cast<OpcUa_Int16>(value.signedValue()));
            return true;
        case TypeUInt16:
            out.setUInt16(static_cast<OpcUa_UInt16>(value.unsignedValue()));
            return true;
        case TypeInt32:
            out.setInt32(static_cast<OpcUa_Int32>(value.signedValue()));
            return true;
        case TypeUInt32:
            out.setUInt32(static_cast<OpcUa_UInt32>(value.unsignedValue()));
            return true;
        case TypeInt64:
            out.setInt64(value.signedValue());
            return true;
        case TypeUInt64:
            out.setUInt64(value.unsignedValue());
            return true;
        case TypeFloat:
            out.setFloat(static_cast<OpcUa_Float>(value.floatValue()));
            return true;
        case TypeDouble:
            out.setDouble(value.floatValue());
            return true;
        case TypeString:
            out.setString(UaString(value.stringValue().c_str()));
            return true;
        default:
            return false;
    }
}

}

Engine::Engine(): m_running(false)
{
}

Engine& Engine::instance()
{
    static Engine engine;
    return engine;
}

void Engine::stageConfiguration(const Configuration& configuration)
{
    if (m_running)
        throw std::runtime_error("PubSub: engine is already running");
    m_staged.reset(new Configuration(configuration));
}

void Engine::startIfStaged(AddressSpace::ASNodeManager* nodeManager)
{
    if (!m_staged)
    {
        LOG(Log::INF) << "PubSub: no PubSub section in the configuration, Pub/Sub stays inactive";
        return;
    }
    if (m_running)
        throw std::runtime_error("PubSub: engine is already running");

    Configuration configuration = *m_staged;
    m_staged.reset();

    m_ioContext.reset(new boost::asio::io_context());
    buildRuntimes(configuration, nodeManager);

    m_workGuard.reset(new boost::asio::executor_work_guard<boost::asio::io_context::executor_type>(
        boost::asio::make_work_guard(*m_ioContext)));

    for (size_t i = 0; i < m_writerGroups.size(); i++)
        scheduleGroup(m_writerGroups[i]);
    for (size_t i = 0; i < m_receivers.size(); i++)
        m_receivers[i]->start();

    boost::asio::io_context* io = m_ioContext.get();
    m_thread = std::thread([io]()
    {
        io->run();
    });
    m_running = true;

    size_t writerCount = 0;
    for (size_t i = 0; i < m_writerGroups.size(); i++)
        writerCount += m_writerGroups[i]->writers.size();
    LOG(Log::INF) << "PubSub: engine started"
                  << " (" << m_writerGroups.size() << " writer group(s), "
                  << writerCount << " data set writer(s), "
                  << m_readers.size() << " data set reader(s))";
}

void Engine::shutdown()
{
    if (!m_running)
        return;

    for (size_t i = 0; i < m_writerGroups.size(); i++)
    {
        if (m_writerGroups[i]->timer)
            m_writerGroups[i]->timer->cancel();
    }
    for (size_t i = 0; i < m_receivers.size(); i++)
        m_receivers[i]->stop();

    m_workGuard.reset();
    m_ioContext->stop();
    if (m_thread.joinable())
        m_thread.join();

    m_writerGroups.clear();
    m_receivers.clear();
    m_readers.clear();
    m_ioContext.reset();
    m_running = false;
    LOG(Log::INF) << "PubSub: engine stopped";
}

AddressSpace::ChangeNotifyingVariable* Engine::resolveVariable(
    AddressSpace::ASNodeManager* nodeManager,
    const std::string& address) const
{
    UaNodeId nodeId(UaString(address.c_str()), nodeManager->getNameSpaceIndex());
    UaNode* node = nodeManager->getNode(nodeId);
    if (!node)
        throw std::runtime_error("PubSub: address '" + address + "' does not exist in the address space");
    AddressSpace::ChangeNotifyingVariable* variable =
        dynamic_cast<AddressSpace::ChangeNotifyingVariable*>(node);
    if (!variable)
        throw std::runtime_error("PubSub: address '" + address + "' is not a cache variable");
    return variable;
}

void Engine::buildRuntimes(const Configuration& configuration, AddressSpace::ASNodeManager* nodeManager)
{
    for (size_t c = 0; c < configuration.connections.size(); c++)
    {
        const ConnectionConfig& connection = configuration.connections[c];

        for (size_t g = 0; g < connection.writerGroups.size(); g++)
        {
            std::shared_ptr<WriterGroupRuntime> group(new WriterGroupRuntime());
            group->config = connection.writerGroups[g];
            group->transmitter.reset(new UdpTransmitter(
                *m_ioContext, connection.host, connection.port, connection.ttl, connection.loopback));
            group->timer.reset(new boost::asio::steady_timer(*m_ioContext));
            for (size_t w = 0; w < group->config.writers.size(); w++)
            {
                WriterRuntime writer;
                writer.config = group->config.writers[w];
                for (size_t f = 0; f < writer.config.fields.size(); f++)
                    writer.variables.push_back(resolveVariable(nodeManager, writer.config.fields[f].address));
                group->writers.push_back(writer);
            }
            group->publisherIdType = configuration.publisherIdType;
            group->publisherId = configuration.publisherId;
            m_writerGroups.push_back(group);
        }

        if (!connection.readers.empty())
        {
            for (size_t r = 0; r < connection.readers.size(); r++)
            {
                std::shared_ptr<ReaderRuntime> reader(new ReaderRuntime());
                reader->config = connection.readers[r];
                for (size_t f = 0; f < reader->config.targets.size(); f++)
                    reader->targets.push_back(resolveVariable(nodeManager, reader->config.targets[f].address));
                ReaderKey key(reader->config.publisherId, reader->config.writerGroupId, reader->config.dataSetWriterId);
                if (m_readers.count(key))
                    throw std::runtime_error("PubSub: duplicate DataSetReader (publisherId/writerGroupId/dataSetWriterId)");
                m_readers[key] = reader;
            }
            m_receivers.push_back(std::shared_ptr<UdpReceiver>(new UdpReceiver(
                *m_ioContext, connection.host, connection.port,
                [this](const uint8_t* data, size_t size) { handleDatagram(data, size); })));
        }
    }
}

void Engine::scheduleGroup(const std::shared_ptr<WriterGroupRuntime>& group)
{
    long long microseconds = static_cast<long long>(group->config.publishingIntervalMs * 1000.0 + 0.5);
    if (microseconds < 1000)
        microseconds = 1000;
    group->timer->expires_after(std::chrono::microseconds(microseconds));
    group->timer->async_wait([this, group](const boost::system::error_code& error)
    {
        if (error == boost::asio::error::operation_aborted)
            return;
        publishGroup(group);
        scheduleGroup(group);
    });
}

void Engine::publishGroup(const std::shared_ptr<WriterGroupRuntime>& group)
{
    NetworkMessage message;
    message.publisherIdType = group->publisherIdType;
    message.publisherId = group->publisherId;
    message.writerGroupIdEnabled = true;
    message.writerGroupId = group->config.id;
    message.groupSequenceNumberEnabled = true;
    message.groupSequenceNumber = ++group->sequenceNumber;

    for (size_t w = 0; w < group->writers.size(); w++)
    {
        WriterRuntime& writer = group->writers[w];
        DataSetMessage dsm;
        dsm.dataSetWriterId = writer.config.id;
        dsm.sequenceNumberEnabled = true;
        dsm.sequenceNumber = ++writer.sequenceNumber;
        for (size_t f = 0; f < writer.variables.size(); f++)
        {
            UaDataValue dataValue = writer.variables[f]->value(0);
            WireValue wireValue;
            if (!dataValue.value() || !uaVariantToWire(UaVariant(*dataValue.value()), wireValue))
            {
                LOG(Log::WRN) << "PubSub: cannot encode value of '" << writer.config.fields[f].address
                              << "', publishing Null";
                wireValue = WireValue::makeNull();
            }
            dsm.fields.push_back(wireValue);
        }
        message.messages.push_back(dsm);
    }

    try
    {
        group->transmitter->send(encodeNetworkMessage(message));
    }
    catch (const std::exception& e)
    {
        LOG(Log::ERR) << "PubSub: publishing writer group " << group->config.id << " failed: " << e.what();
    }
}

void Engine::handleDatagram(const uint8_t* data, size_t size)
{
    NetworkMessage message;
    std::string diagnostic;
    if (!decodeNetworkMessage(data, size, message, diagnostic))
    {
        LOG(Log::DBG) << "PubSub: ignoring undecodable datagram (" << diagnostic << ")";
        return;
    }

    uint16_t writerGroupId = message.writerGroupIdEnabled ? message.writerGroupId : 0;
    for (size_t m = 0; m < message.messages.size(); m++)
    {
        const DataSetMessage& dsm = message.messages[m];
        if (dsm.keepAlive)
            continue;
        ReaderKey key(message.publisherId, writerGroupId, dsm.dataSetWriterId);
        std::map<ReaderKey, std::shared_ptr<ReaderRuntime> >::iterator it = m_readers.find(key);
        if (it == m_readers.end())
            continue;
        ReaderRuntime& reader = *it->second;
        size_t count = dsm.fields.size() < reader.targets.size() ? dsm.fields.size() : reader.targets.size();
        if (dsm.fields.size() != reader.targets.size())
            LOG(Log::WRN) << "PubSub: DataSetMessage from publisher " << message.publisherId
                          << " carries " << dsm.fields.size() << " field(s) but reader expects "
                          << reader.targets.size();
        for (size_t f = 0; f < count; f++)
        {
            UaVariant variant;
            if (!wireToUaVariant(dsm.fields[f], variant))
            {
                LOG(Log::WRN) << "PubSub: unsupported field type from publisher " << message.publisherId
                              << ", field " << f;
                continue;
            }
            UaDataValue dataValue(variant, OpcUa_Good, UaDateTime::now(), UaDateTime::now());
            UaStatus status = reader.targets[f]->setValue(0, dataValue, OpcUa_False);
            if (!status.isGood())
                LOG(Log::WRN) << "PubSub: writing received value to '"
                              << reader.config.targets[f].address << "' failed";
        }
    }
}

}
