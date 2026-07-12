/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * FxEngine.cpp
 *
 *  Created on: 13 Jul 2026
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

#include <FxEngine.h>

#include <sstream>
#include <stdexcept>

#include <methodhandleuanode.h>

#include <ASDelegatingMethod.h>
#include <ASNodeManager.h>
#include <ChangeNotifyingVariable.h>
#include <FxJson.h>
#include <LogIt.h>
#include <PubSubEngine.h>

namespace Fx
{

namespace
{

const char* endpointStatusName(EndpointStatus status)
{
    switch (status)
    {
        case EndpointInitial:        return "Initial";
        case EndpointReady:          return "Ready";
        case EndpointPreOperational: return "PreOperational";
        case EndpointOperational:    return "Operational";
        case EndpointError:          return "Error";
    }
    return "?";
}

std::string requiredString(const JsonValue& object, const std::string& key)
{
    if (!object.has(key))
        throw std::runtime_error("connection configuration: missing '" + key + "'");
    return object.at(key).stringValue();
}

uint64_t requiredUnsigned(const JsonValue& object, const std::string& key, uint64_t maximum)
{
    if (!object.has(key))
        throw std::runtime_error("connection configuration: missing '" + key + "'");
    double value = object.at(key).numberValue();
    /* JSON numbers are doubles: above 2^53 they silently lose integer
     * precision, so the projection refuses them rather than guessing. */
    const double kJsonExactIntegerLimit = 9007199254740992.0;
    if (value < 0 || value != value
        || value > kJsonExactIntegerLimit
        || value > static_cast<double>(maximum))
        throw std::runtime_error("connection configuration: '" + key + "' out of range");
    if (value != static_cast<double>(static_cast<uint64_t>(value)))
        throw std::runtime_error("connection configuration: '" + key + "' is not an integer");
    return static_cast<uint64_t>(value);
}

/* Endpoint objects are address-space nodes and node creation is permanent —
 * without a ceiling a looping client could grow the address space without
 * bound. Distinct connection names beyond the ceiling are refused; closed
 * endpoints are reused by name. */
const size_t kMaxEndpointsPerComponent = 64;

std::string detail(const std::string& state, const std::string& message)
{
    JsonValue out = JsonValue::makeObject();
    out.set("status", JsonValue::makeString(state));
    if (!message.empty())
        out.set("diagnostic", JsonValue::makeString(message));
    return out.serialize();
}

const char* publisherIdTypeName(PubSub::PublisherIdType type)
{
    switch (type)
    {
        case PubSub::PublisherIdByte:   return "Byte";
        case PubSub::PublisherIdUInt16: return "UInt16";
        case PubSub::PublisherIdUInt32: return "UInt32";
        case PubSub::PublisherIdUInt64: return "UInt64";
    }
    return "UInt16";
}

}

Engine::Engine(): m_nodeManager(0), m_automationComponent(0), m_started(false)
{
}

Engine& Engine::instance()
{
    static Engine engine;
    return engine;
}

void Engine::stageConfiguration(const Configuration& configuration)
{
    if (m_started)
        throw std::runtime_error("Fx: engine is already started");
    m_staged.reset(new Configuration(configuration));
}

void Engine::startIfStaged(AddressSpace::ASNodeManager* nodeManager)
{
    if (!m_staged)
    {
        LOG(Log::INF) << "Fx: no Fx section in the configuration, FX stays inactive";
        return;
    }
    if (m_started)
        throw std::runtime_error("Fx: engine is already started");

    m_configuration = *m_staged;
    m_staged.reset();
    m_nodeManager = nodeManager;

    buildModel();
    PubSub::Engine::instance().ensureStarted(nodeManager);
    m_started = true;

    size_t outputs = 0;
    size_t inputs = 0;
    for (size_t i = 0; i < m_configuration.entities.size(); i++)
    {
        outputs += m_configuration.entities[i].outputs.size();
        inputs += m_configuration.entities[i].inputs.size();
    }
    LOG(Log::INF) << "Fx: automation component '" << m_configuration.automationComponent
                  << "' online (" << m_configuration.entities.size() << " functional entity(ies), "
                  << outputs << " output dataset(s), " << inputs << " input dataset(s))";
}

void Engine::shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_started)
        return;
    for (std::map<std::string, Endpoint>::iterator it = m_endpoints.begin(); it != m_endpoints.end(); ++it)
    {
        if (it->second.active)
        {
            try
            {
                PubSub::Engine::instance().removeDynamic(it->second.ownerTag);
            }
            catch (const std::exception& e)
            {
                LOG(Log::WRN) << "Fx: while closing connection '" << it->first << "' at shutdown: " << e.what();
            }
            it->second.active = false;
        }
    }
    m_started = false;
    LOG(Log::INF) << "Fx: engine stopped";
}

UaNodeId Engine::addPlainObject(const UaNodeId& parentNodeId, const std::string& name)
{
    OpcUa::BaseObjectType* node = new OpcUa::BaseObjectType(
        m_nodeManager->makeChildNodeId(parentNodeId, name.c_str()),
        name.c_str(),
        m_nodeManager->getNameSpaceIndex(),
        m_nodeManager);
    UaStatus status = m_nodeManager->addNodeAndReference(parentNodeId, node, OpcUaId_Organizes);
    if (!status.isGood())
        throw std::runtime_error("Fx: adding node '" + name + "' failed: " + std::string(status.toString().toUtf8()));
    return node->nodeId();
}

AddressSpace::ChangeNotifyingVariable* Engine::addVariable(
    const UaNodeId&    parentNodeId,
    const std::string& name,
    const UaVariant&   initialValue,
    OpcUa_BuiltInType  dataType)
{
    AddressSpace::ChangeNotifyingVariable* variable = new AddressSpace::ChangeNotifyingVariable(
        m_nodeManager->makeChildNodeId(parentNodeId, name.c_str()),
        name.c_str(),
        m_nodeManager->getNameSpaceIndex(),
        initialValue,
        OpcUa_AccessLevels_CurrentRead,
        m_nodeManager);
    variable->setDataType(UaNodeId(dataType, 0));
    UaDataValue dataValue(initialValue, OpcUa_Good, UaDateTime::now(), UaDateTime::now());
    variable->setValue(0, dataValue, OpcUa_False);
    UaStatus status = m_nodeManager->addNodeAndReference(parentNodeId, variable, OpcUaId_HasComponent);
    if (!status.isGood())
        throw std::runtime_error("Fx: adding variable '" + name + "' failed: " + std::string(status.toString().toUtf8()));
    return variable;
}

void Engine::referenceDatasetFields(const UaNodeId& datasetNodeId, const std::vector<DatasetField>& fields)
{
    for (size_t f = 0; f < fields.size(); f++)
    {
        UaNodeId variableNodeId(UaString(fields[f].address.c_str()), m_nodeManager->getNameSpaceIndex());
        if (!m_nodeManager->getNode(variableNodeId))
            throw std::runtime_error("Fx: dataset field '" + fields[f].name + "' maps to '"
                                     + fields[f].address + "' which does not exist in the address space");
        UaVariant mappedAddress;
        mappedAddress.setString(UaString(fields[f].address.c_str()));
        addVariable(datasetNodeId, fields[f].name, mappedAddress, OpcUaType_String);
    }
}

void Engine::buildModel()
{
    const UaNodeId objectsFolder(OpcUaId_ObjectsFolder, 0);

    m_automationComponent = new AutomationComponentNode(
        m_nodeManager->makeChildNodeId(objectsFolder, m_configuration.automationComponent.c_str()),
        m_configuration.automationComponent.c_str(),
        m_nodeManager->getNameSpaceIndex());
    UaStatus status = m_nodeManager->addNodeAndReference(
        objectsFolder, m_automationComponent, OpcUaId_Organizes);
    if (!status.isGood())
        throw std::runtime_error("Fx: adding the automation component failed: " + std::string(status.toString().toUtf8()));

    const UaNodeId acNodeId = m_automationComponent->nodeId();
    const UaNodeId entitiesFolder = addPlainObject(acNodeId, "FunctionalEntities");

    for (size_t e = 0; e < m_configuration.entities.size(); e++)
    {
        const FunctionalEntityConfig& entity = m_configuration.entities[e];
        const UaNodeId entityNode = addPlainObject(entitiesFolder, entity.name);
        const UaNodeId outputData = addPlainObject(entityNode, "OutputData");
        const UaNodeId inputData = addPlainObject(entityNode, "InputData");
        m_endpointsFolderOfEntity[entity.name] = addPlainObject(entityNode, "ConnectionEndpoints");

        for (size_t d = 0; d < entity.outputs.size(); d++)
        {
            const UaNodeId datasetNode = addPlainObject(outputData, entity.outputs[d].name);
            referenceDatasetFields(datasetNode, entity.outputs[d].fields);
        }
        for (size_t d = 0; d < entity.inputs.size(); d++)
        {
            const UaNodeId datasetNode = addPlainObject(inputData, entity.inputs[d].name);
            referenceDatasetFields(datasetNode, entity.inputs[d].fields);
        }
    }

    /* Argument properties must be referenced from a method node BEFORE the
     * method itself is added to its parent: the open62541 backend registers
     * the method with the stack at that moment and reads the signature from
     * the already-referenced properties (the generated classes follow the
     * same order). */
    AddressSpace::ASDelegatingMethod<AutomationComponentNode>* establish =
        new AddressSpace::ASDelegatingMethod<AutomationComponentNode>(
            m_nodeManager->makeChildNodeId(acNodeId, "EstablishConnections"),
            "EstablishConnections",
            m_nodeManager->getNameSpaceIndex());
    establish->assignHandler(m_automationComponent, &AutomationComponentNode::callEstablishConnections);

    {
        UaUInt32Array dimensions;
        UaPropertyMethodArgument* arguments = new UaPropertyMethodArgument(
            m_nodeManager->makeChildNodeId(establish->nodeId(), "args"),
            OpcUa_AccessLevels_CurrentRead,
            1,
            UaPropertyMethodArgument::INARGUMENTS);
        arguments->setArgument(
            0, UaString("connectionConfiguration"), UaNodeId(OpcUaType_String, 0), -1, dimensions,
            UaLocalizedText("en_US", "connectionConfiguration"));
        m_nodeManager->addNodeAndReferenceThrows(
            establish, arguments, OpcUaId_HasProperty, establish->nodeId(), arguments->nodeId());

        UaPropertyMethodArgument* returns = new UaPropertyMethodArgument(
            m_nodeManager->makeChildNodeId(establish->nodeId(), "return_values"),
            OpcUa_AccessLevels_CurrentRead,
            2,
            UaPropertyMethodArgument::OUTARGUMENTS);
        returns->setArgument(
            0, UaString("connectionId"), UaNodeId(OpcUaType_String, 0), -1, dimensions,
            UaLocalizedText("en_US", "connectionId"));
        returns->setArgument(
            1, UaString("detail"), UaNodeId(OpcUaType_String, 0), -1, dimensions,
            UaLocalizedText("en_US", "detail"));
        m_nodeManager->addNodeAndReferenceThrows(
            establish, returns, OpcUaId_HasProperty, establish->nodeId(), returns->nodeId());
    }
    m_nodeManager->addNodeAndReferenceThrows(
        m_automationComponent, establish, OpcUaId_HasComponent,
        m_automationComponent->nodeId(), establish->nodeId());

    AddressSpace::ASDelegatingMethod<AutomationComponentNode>* close =
        new AddressSpace::ASDelegatingMethod<AutomationComponentNode>(
            m_nodeManager->makeChildNodeId(acNodeId, "CloseConnections"),
            "CloseConnections",
            m_nodeManager->getNameSpaceIndex());
    close->assignHandler(m_automationComponent, &AutomationComponentNode::callCloseConnections);

    {
        UaUInt32Array dimensions;
        UaPropertyMethodArgument* arguments = new UaPropertyMethodArgument(
            m_nodeManager->makeChildNodeId(close->nodeId(), "args"),
            OpcUa_AccessLevels_CurrentRead,
            1,
            UaPropertyMethodArgument::INARGUMENTS);
        arguments->setArgument(
            0, UaString("connectionId"), UaNodeId(OpcUaType_String, 0), -1, dimensions,
            UaLocalizedText("en_US", "connectionId"));
        m_nodeManager->addNodeAndReferenceThrows(
            close, arguments, OpcUaId_HasProperty, close->nodeId(), arguments->nodeId());

        UaPropertyMethodArgument* returns = new UaPropertyMethodArgument(
            m_nodeManager->makeChildNodeId(close->nodeId(), "return_values"),
            OpcUa_AccessLevels_CurrentRead,
            1,
            UaPropertyMethodArgument::OUTARGUMENTS);
        returns->setArgument(
            0, UaString("detail"), UaNodeId(OpcUaType_String, 0), -1, dimensions,
            UaLocalizedText("en_US", "detail"));
        m_nodeManager->addNodeAndReferenceThrows(
            close, returns, OpcUaId_HasProperty, close->nodeId(), returns->nodeId());
    }
    m_nodeManager->addNodeAndReferenceThrows(
        m_automationComponent, close, OpcUaId_HasComponent,
        m_automationComponent->nodeId(), close->nodeId());
}

const FunctionalEntityConfig* Engine::findEntity(const std::string& name) const
{
    for (size_t i = 0; i < m_configuration.entities.size(); i++)
        if (m_configuration.entities[i].name == name)
            return &m_configuration.entities[i];
    return 0;
}

const OutputDataset* Engine::findOutput(const FunctionalEntityConfig& entity, const std::string& name)
{
    for (size_t i = 0; i < entity.outputs.size(); i++)
        if (entity.outputs[i].name == name)
            return &entity.outputs[i];
    return 0;
}

const InputDataset* Engine::findInput(const FunctionalEntityConfig& entity, const std::string& name)
{
    for (size_t i = 0; i < entity.inputs.size(); i++)
        if (entity.inputs[i].name == name)
            return &entity.inputs[i];
    return 0;
}

void Engine::setEndpointStatus(Endpoint& endpoint, EndpointStatus status)
{
    if (!endpoint.statusVariable)
        return;
    UaVariant value;
    value.setInt32(static_cast<OpcUa_Int32>(status));
    UaDataValue dataValue(value, OpcUa_Good, UaDateTime::now(), UaDateTime::now());
    endpoint.statusVariable->setValue(0, dataValue, OpcUa_False);
}

UaStatus Engine::establishConnections(
    const std::string& requestJson, std::string& connectionId, std::string& detailJson)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    connectionId.clear();
    try
    {
        if (!m_started)
            throw std::runtime_error("FX is not active on this server");

        const JsonValue request = parseJsonObject(requestJson);
        const std::string entityName = requiredString(request, "functionalEntity");
        const std::string role = requiredString(request, "role");
        const std::string datasetName = requiredString(request, "dataset");
        const std::string address = requiredString(request, "address");
        std::string name = request.has("connectionName")
            ? request.at("connectionName").stringValue() : std::string();

        const FunctionalEntityConfig* entity = findEntity(entityName);
        if (!entity)
            throw std::runtime_error("unknown functional entity '" + entityName + "'");

        if (name.empty())
        {
            std::ostringstream generated;
            generated << "cep-" << (m_endpoints.size() + 1);
            name = generated.str();
        }

        std::map<std::string, Endpoint>::iterator existing = m_endpoints.find(name);
        if (existing != m_endpoints.end() && existing->second.active)
            throw std::runtime_error("connection '" + name + "' is already established");
        if (existing == m_endpoints.end() && m_endpoints.size() >= kMaxEndpointsPerComponent)
            throw std::runtime_error("connection endpoint limit reached ("
                                     + std::to_string(kMaxEndpointsPerComponent)
                                     + "); close and reuse an existing connection name");

        PubSub::ConnectionConfig connection;
        PubSub::parseNetworkAddress(address, connection.host, connection.port);
        if (request.has("ttl"))
            connection.ttl = static_cast<uint8_t>(requiredUnsigned(request, "ttl", 255));

        PubSub::PublisherIdType publisherIdType = entity->publisherIdType;
        uint64_t publisherId = entity->publisherId;

        if (role == "publisher")
        {
            const OutputDataset* dataset = findOutput(*entity, datasetName);
            if (!dataset)
                throw std::runtime_error("functional entity '" + entityName
                                         + "' has no output dataset '" + datasetName + "'");
            PubSub::WriterGroupConfig group;
            group.id = dataset->writerGroupId;
            group.publishingIntervalMs = request.has("publishingIntervalMs")
                ? request.at("publishingIntervalMs").numberValue() : dataset->publishingIntervalMs;
            if (group.publishingIntervalMs <= 0)
                throw std::runtime_error("publishingIntervalMs must be positive");
            PubSub::DataSetWriterConfig writer;
            writer.id = dataset->dataSetWriterId;
            for (size_t f = 0; f < dataset->fields.size(); f++)
            {
                PubSub::FieldConfig field;
                field.address = dataset->fields[f].address;
                writer.fields.push_back(field);
            }
            group.writers.push_back(writer);
            connection.writerGroups.push_back(group);
        }
        else if (role == "subscriber")
        {
            const InputDataset* dataset = findInput(*entity, datasetName);
            if (!dataset)
                throw std::runtime_error("functional entity '" + entityName
                                         + "' has no input dataset '" + datasetName + "'");
            if (!request.has("peer"))
                throw std::runtime_error("subscriber connections need a 'peer' object");
            const JsonValue& peer = request.at("peer");
            PubSub::DataSetReaderConfig reader;
            reader.publisherIdType = PubSub::parsePublisherIdType(
                peer.has("publisherIdType") ? peer.at("publisherIdType").stringValue() : "UInt16");
            reader.publisherId = requiredUnsigned(peer, "publisherId", 0xFFFFFFFFFFFFFFFFull);
            reader.writerGroupId = static_cast<uint16_t>(requiredUnsigned(peer, "writerGroupId", 0xFFFF));
            reader.dataSetWriterId = static_cast<uint16_t>(requiredUnsigned(peer, "dataSetWriterId", 0xFFFF));
            for (size_t f = 0; f < dataset->fields.size(); f++)
            {
                PubSub::FieldConfig field;
                field.address = dataset->fields[f].address;
                reader.targets.push_back(field);
            }
            connection.readers.push_back(reader);
        }
        else
            throw std::runtime_error("role must be 'publisher' or 'subscriber'");

        Endpoint endpoint;
        if (existing != m_endpoints.end())
            endpoint = existing->second;
        endpoint.entity = entityName;
        endpoint.dataset = datasetName;
        endpoint.role = role;
        endpoint.ownerTag = "fx:" + m_configuration.automationComponent + "/" + entityName + "/" + name;

        if (!endpoint.statusVariable)
        {
            const UaNodeId endpointsFolder = m_endpointsFolderOfEntity[entityName];
            const UaNodeId endpointNode = addPlainObject(endpointsFolder, name);
            UaVariant initialStatus;
            initialStatus.setInt32(EndpointInitial);
            endpoint.statusVariable = addVariable(endpointNode, "Status", initialStatus, OpcUaType_Int32);
            UaVariant addressValue;
            addressValue.setString(UaString(address.c_str()));
            addVariable(endpointNode, "Address", addressValue, OpcUaType_String);
            UaVariant datasetValue;
            datasetValue.setString(UaString((role + ":" + entityName + "." + datasetName).c_str()));
            addVariable(endpointNode, "Dataset", datasetValue, OpcUaType_String);
        }

        PubSub::Engine::instance().addDynamic(endpoint.ownerTag, publisherIdType, publisherId, connection);
        endpoint.active = true;
        setEndpointStatus(endpoint, EndpointOperational);
        m_endpoints[name] = endpoint;

        connectionId = name;
        JsonValue detailValue = JsonValue::makeObject();
        detailValue.set("status", JsonValue::makeString(endpointStatusName(EndpointOperational)));
        detailValue.set("address", JsonValue::makeString(address));
        if (role == "publisher")
        {
            const OutputDataset* dataset = findOutput(*entity, datasetName);
            JsonValue coordinates = JsonValue::makeObject();
            coordinates.set("publisherIdType", JsonValue::makeString(publisherIdTypeName(publisherIdType)));
            coordinates.set("publisherId", JsonValue::makeNumber(static_cast<double>(publisherId)));
            coordinates.set("writerGroupId", JsonValue::makeNumber(dataset->writerGroupId));
            coordinates.set("dataSetWriterId", JsonValue::makeNumber(dataset->dataSetWriterId));
            detailValue.set("coordinates", coordinates);
        }
        detailJson = detailValue.serialize();
        LOG(Log::INF) << "Fx: connection '" << name << "' established ("
                      << role << " of " << entityName << "." << datasetName << " on " << address << ")";
        return UaStatus(OpcUa_Good);
    }
    catch (const std::exception& e)
    {
        detailJson = detail(endpointStatusName(EndpointError), e.what());
        LOG(Log::WRN) << "Fx: EstablishConnections refused: " << e.what();
        return UaStatus(OpcUa_BadInvalidArgument);
    }
}

UaStatus Engine::closeConnections(const std::string& requestText, std::string& detailJson)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    try
    {
        if (!m_started)
            throw std::runtime_error("FX is not active on this server");

        std::string name = requestText;
        if (!name.empty() && name[0] == '{')
            name = requiredString(parseJsonObject(requestText), "connectionId");
        if (name.empty())
            throw std::runtime_error("empty connectionId");

        std::map<std::string, Endpoint>::iterator it = m_endpoints.find(name);
        if (it == m_endpoints.end())
            throw std::runtime_error("unknown connection '" + name + "'");
        if (!it->second.active)
            throw std::runtime_error("connection '" + name + "' is not established");

        PubSub::Engine::instance().removeDynamic(it->second.ownerTag);
        it->second.active = false;
        setEndpointStatus(it->second, EndpointInitial);

        detailJson = detail(endpointStatusName(EndpointInitial), "");
        LOG(Log::INF) << "Fx: connection '" << name << "' closed";
        return UaStatus(OpcUa_Good);
    }
    catch (const std::exception& e)
    {
        detailJson = detail(endpointStatusName(EndpointError), e.what());
        LOG(Log::WRN) << "Fx: CloseConnections refused: " << e.what();
        return UaStatus(OpcUa_BadInvalidArgument);
    }
}

AutomationComponentNode::AutomationComponentNode(
    const UaNodeId& nodeId,
    const UaString& name,
    OpcUa_UInt16    browseNameNameSpaceIndex):
    OpcUa::BaseObjectType(nodeId, name, browseNameNameSpaceIndex, 0)
{
}

UaStatus AutomationComponentNode::beginCall(
    MethodManagerCallback* pCallback,
    const ServiceContext&,
    OpcUa_UInt32           callbackHandle,
    MethodHandle*          pMethodHandle,
    const UaVariantArray&  inputArguments)
{
    MethodHandleUaNode* handle = dynamic_cast<MethodHandleUaNode*>(pMethodHandle);
    if (!handle)
        return OpcUa_BadInternalError;
    AddressSpace::ASDelegatingMethod<AutomationComponentNode>* method =
        static_cast< AddressSpace::ASDelegatingMethod<AutomationComponentNode>* >(handle->pUaMethod());
    if (!method)
        return OpcUa_BadInternalError;
    return method->call(pCallback, callbackHandle, inputArguments);
}

namespace
{

bool extractStringArgument(const UaVariantArray& inputArguments, std::string& out)
{
    if (inputArguments.length() != 1)
        return false;
    UaVariant value(inputArguments[0]);
    if (value.type() != OpcUaType_String)
        return false;
    UaString text = value.toString();
    out = text.toUtf8() ? text.toUtf8() : "";
    return true;
}

void finishWithStrings(
    MethodManagerCallback* pCallback,
    OpcUa_UInt32           callbackHandle,
    const std::vector<std::string>& outputs,
    const UaStatus&        status)
{
    UaStatusCodeArray inputArgumentResults;
    UaDiagnosticInfos inputArgumentDiag;
    UaVariantArray    outputArguments;
    outputArguments.create(static_cast<OpcUa_UInt32>(outputs.size()));
    for (size_t i = 0; i < outputs.size(); i++)
    {
        UaVariant helper;
        helper.setString(UaString(outputs[i].c_str()));
        helper.copyTo(&outputArguments[static_cast<OpcUa_UInt32>(i)]);
    }
    UaStatus finishStatus = status;
    pCallback->finishCall(callbackHandle, inputArgumentResults, inputArgumentDiag, outputArguments, finishStatus);
}

}

UaStatus AutomationComponentNode::callEstablishConnections(
    MethodManagerCallback* pCallback,
    OpcUa_UInt32           callbackHandle,
    const UaVariantArray&  inputArguments)
{
    std::string request;
    std::vector<std::string> outputs(2);
    if (!extractStringArgument(inputArguments, request))
    {
        outputs[1] = "{\"status\":\"Error\",\"diagnostic\":\"expected one String argument: connectionConfiguration\"}";
        finishWithStrings(pCallback, callbackHandle, outputs, UaStatus(OpcUa_BadInvalidArgument));
        return OpcUa_Good;
    }
    std::string connectionId;
    std::string detailJson;
    UaStatus status = Engine::instance().establishConnections(request, connectionId, detailJson);
    outputs[0] = connectionId;
    outputs[1] = detailJson;
    finishWithStrings(pCallback, callbackHandle, outputs, status);
    return OpcUa_Good;
}

UaStatus AutomationComponentNode::callCloseConnections(
    MethodManagerCallback* pCallback,
    OpcUa_UInt32           callbackHandle,
    const UaVariantArray&  inputArguments)
{
    std::string request;
    std::vector<std::string> outputs(1);
    if (!extractStringArgument(inputArguments, request))
    {
        outputs[0] = "{\"status\":\"Error\",\"diagnostic\":\"expected one String argument: connectionId\"}";
        finishWithStrings(pCallback, callbackHandle, outputs, UaStatus(OpcUa_BadInvalidArgument));
        return OpcUa_Good;
    }
    std::string detailJson;
    UaStatus status = Engine::instance().closeConnections(request, detailJson);
    outputs[0] = detailJson;
    finishWithStrings(pCallback, callbackHandle, outputs, status);
    return OpcUa_Good;
}

}
