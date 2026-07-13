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
#include <uadatavariablecache.h>

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

/* Endpoint objects are permanent address-space nodes on both backends —
 * without a ceiling a looping connection manager could grow the address
 * space without bound. Distinct names beyond the ceiling are refused;
 * closed endpoints are reused by name. */
const size_t kMaxEndpointsPerComponent = 64;

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

std::string statusDetail(const char* status, const std::string& diagnostic)
{
    JsonValue detail = JsonValue::object();
    detail.setMember("status", JsonValue::string(status));
    if (!diagnostic.empty())
        detail.setMember("diagnostic", JsonValue::string(diagnostic));
    return detail.serialize();
}

/* The projection is strict about its vocabulary: an unknown member is a
 * refused request, not a silently ignored one — typos surface immediately. */
void requireOnlyMembers(
    const JsonValue& object, const char* const* allowed, size_t allowedCount, const char* context)
{
    const std::map<std::string, JsonValue>& members = object.members();
    for (std::map<std::string, JsonValue>::const_iterator it = members.begin();
         it != members.end(); ++it)
    {
        bool known = false;
        for (size_t i = 0; i < allowedCount; i++)
            if (it->first == allowed[i])
            {
                known = true;
                break;
            }
        if (!known)
            throw std::runtime_error(
                std::string("unknown member '") + it->first + "' in " + context);
    }
}

std::string requiredString(const JsonValue& object, const std::string& key)
{
    if (!object.hasMember(key))
        throw std::runtime_error("missing '" + key + "'");
    if (object.member(key).kind() != JsonValue::KindString)
        throw std::runtime_error("'" + key + "' must be a string");
    return object.member(key).asString();
}

uint64_t requiredUnsigned(const JsonValue& object, const std::string& key, uint64_t maximum)
{
    if (!object.hasMember(key))
        throw std::runtime_error("missing '" + key + "'");
    if (object.member(key).kind() != JsonValue::KindNumber)
        throw std::runtime_error("'" + key + "' must be a number");
    const double value = object.member(key).asNumber();
    /* JSON numbers are doubles: beyond 2^53 they silently lose integer
     * precision, so the projection refuses them rather than guessing. */
    const double kJsonExactIntegerLimit = 9007199254740992.0;
    if (!(value >= 0) || value > kJsonExactIntegerLimit
        || value != static_cast<double>(static_cast<uint64_t>(value)))
        throw std::runtime_error("'" + key + "' must be a non-negative integer (at most 2^53)");
    if (static_cast<uint64_t>(value) > maximum)
    {
        std::ostringstream message;
        message << "'" << key << "' out of range (maximum " << maximum << ")";
        throw std::runtime_error(message.str());
    }
    return static_cast<uint64_t>(value);
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

    buildRuntimes();
    buildModel();
    PubSub::Engine::instance().ensureStarted();
    m_started = true;

    size_t outputs = 0;
    size_t inputs = 0;
    for (size_t i = 0; i < m_entities.size(); i++)
    {
        outputs += m_entities[i].outputs.size();
        inputs += m_entities[i].inputs.size();
    }
    LOG(Log::INF) << "Fx: automation component '" << m_configuration.automationComponent
                  << "' online (" << m_entities.size() << " functional entity(ies), "
                  << outputs << " output dataset(s), " << inputs << " input dataset(s))";
}

void Engine::shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_started)
        return;
    for (std::map<std::string, Endpoint>::iterator it = m_endpoints.begin();
         it != m_endpoints.end(); ++it)
    {
        if (!it->second.active)
            continue;
        try
        {
            PubSub::Engine::instance().removeDynamic(it->second.ownerTag);
        }
        catch (const std::exception& e)
        {
            LOG(Log::WRN) << "Fx: closing connection '" << it->first << "' at shutdown: " << e.what();
        }
        it->second.active = false;
        setEndpointStatus(it->second, EndpointInitial);
    }
    m_started = false;
    LOG(Log::INF) << "Fx: engine stopped";
}

AddressSpace::ChangeNotifyingVariable* Engine::resolveVariable(
    const std::string& address, const std::string& diagnosticContext) const
{
    UaNodeId nodeId(UaString(address.c_str()), m_nodeManager->getNameSpaceIndex());
    UaNode* node = m_nodeManager->getNode(nodeId);
    if (!node)
        throw std::runtime_error("Fx: " + diagnosticContext + ": address '" + address
                                 + "' does not exist in the address space");
    AddressSpace::ChangeNotifyingVariable* variable =
        dynamic_cast<AddressSpace::ChangeNotifyingVariable*>(node);
    if (!variable)
        throw std::runtime_error("Fx: " + diagnosticContext + ": address '" + address
                                 + "' is not a cache variable");
    return variable;
}

void Engine::buildRuntimes()
{
    for (size_t e = 0; e < m_configuration.entities.size(); e++)
    {
        const FunctionalEntityConfig& entityConfig = m_configuration.entities[e];
        EntityRuntime entity;
        entity.name = entityConfig.name;

        for (size_t d = 0; d < entityConfig.outputs.size(); d++)
        {
            const OutputDatasetConfig& datasetConfig = entityConfig.outputs[d];
            DatasetRuntime dataset;
            dataset.name = datasetConfig.name;
            dataset.writerGroupId = datasetConfig.writerGroupId;
            dataset.dataSetWriterId = datasetConfig.dataSetWriterId;
            dataset.publishingIntervalMs = datasetConfig.publishingIntervalMs;
            dataset.fields = datasetConfig.fields;
            for (size_t f = 0; f < dataset.fields.size(); f++)
                dataset.variables.push_back(resolveVariable(
                    dataset.fields[f].address,
                    "FunctionalEntity '" + entity.name + "' OutputDataset '" + dataset.name
                        + "' Field '" + dataset.fields[f].name + "'"));
            entity.outputs.push_back(dataset);
        }
        for (size_t d = 0; d < entityConfig.inputs.size(); d++)
        {
            const InputDatasetConfig& datasetConfig = entityConfig.inputs[d];
            DatasetRuntime dataset;
            dataset.name = datasetConfig.name;
            dataset.fields = datasetConfig.fields;
            for (size_t f = 0; f < dataset.fields.size(); f++)
                dataset.variables.push_back(resolveVariable(
                    dataset.fields[f].address,
                    "FunctionalEntity '" + entity.name + "' InputDataset '" + dataset.name
                        + "' Field '" + dataset.fields[f].name + "'"));
            entity.inputs.push_back(dataset);
        }
        m_entities.push_back(entity);
    }
}

UaNodeId Engine::addObject(
    const UaNodeId& parentNodeId, const std::string& name, bool underObjectsFolder)
{
    OpcUa::BaseObjectType* node = new OpcUa::BaseObjectType(
        m_nodeManager->makeChildNodeId(parentNodeId, name.c_str()),
        name.c_str(),
        m_nodeManager->getNameSpaceIndex(),
        m_nodeManager);
    m_nodeManager->addNodeAndReferenceThrows(
        parentNodeId,
        node,
        underObjectsFolder ? UaNodeId(OpcUaId_Organizes) : UaNodeId(OpcUaId_HasComponent),
        node->nodeId());
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
    m_nodeManager->addNodeAndReferenceThrows(
        parentNodeId, variable, UaNodeId(OpcUaId_HasComponent), variable->nodeId());
    return variable;
}

void Engine::addMethods(const UaNodeId& parentNodeId)
{
    /* The canonical framework order (same as the generated classes): create
     * the method, reference its argument properties, and only then reference
     * the method from its parent — the open62541 backend registers the method
     * with the stack at that last step and harvests the signature from the
     * already-referenced properties. */
    AddressSpace::ASDelegatingMethod<AutomationComponentNode>* establish =
        new AddressSpace::ASDelegatingMethod<AutomationComponentNode>(
            m_nodeManager->makeChildNodeId(parentNodeId, "EstablishConnections"),
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
        parentNodeId, establish, UaNodeId(OpcUaId_HasComponent), establish->nodeId());

    AddressSpace::ASDelegatingMethod<AutomationComponentNode>* close =
        new AddressSpace::ASDelegatingMethod<AutomationComponentNode>(
            m_nodeManager->makeChildNodeId(parentNodeId, "CloseConnections"),
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
        parentNodeId, close, UaNodeId(OpcUaId_HasComponent), close->nodeId());
}

void Engine::buildModel()
{
    const UaNodeId objectsFolder(OpcUaId_ObjectsFolder, 0);

    m_automationComponent = new AutomationComponentNode(
        m_nodeManager->makeChildNodeId(objectsFolder, m_configuration.automationComponent.c_str()),
        m_configuration.automationComponent.c_str(),
        m_nodeManager->getNameSpaceIndex(),
        m_nodeManager);
    m_nodeManager->addNodeAndReferenceThrows(
        objectsFolder, m_automationComponent, UaNodeId(OpcUaId_Organizes),
        m_automationComponent->nodeId());

    const UaNodeId acNodeId = m_automationComponent->nodeId();
    addMethods(acNodeId);

    const UaNodeId entitiesFolder = addObject(acNodeId, "FunctionalEntities", false);
    for (size_t e = 0; e < m_entities.size(); e++)
    {
        EntityRuntime& entity = m_entities[e];
        const UaNodeId entityNode = addObject(entitiesFolder, entity.name, false);
        const UaNodeId outputData = addObject(entityNode, "OutputData", false);
        const UaNodeId inputData = addObject(entityNode, "InputData", false);
        entity.endpointsFolder = addObject(entityNode, "ConnectionEndpoints", false);

        /* Dataset views are descriptors: each field is a read-only String
         * variable whose value names the mapped cache variable (the live
         * values stay at their canonical addresses — see FX-ARCHITECTURE.md,
         * consequence C3). */
        for (size_t d = 0; d < entity.outputs.size(); d++)
        {
            const UaNodeId datasetNode = addObject(outputData, entity.outputs[d].name, false);
            for (size_t f = 0; f < entity.outputs[d].fields.size(); f++)
            {
                UaVariant mappedAddress;
                mappedAddress.setString(UaString(entity.outputs[d].fields[f].address.c_str()));
                addVariable(datasetNode, entity.outputs[d].fields[f].name, mappedAddress, OpcUaType_String);
            }
        }
        for (size_t d = 0; d < entity.inputs.size(); d++)
        {
            const UaNodeId datasetNode = addObject(inputData, entity.inputs[d].name, false);
            for (size_t f = 0; f < entity.inputs[d].fields.size(); f++)
            {
                UaVariant mappedAddress;
                mappedAddress.setString(UaString(entity.inputs[d].fields[f].address.c_str()));
                addVariable(datasetNode, entity.inputs[d].fields[f].name, mappedAddress, OpcUaType_String);
            }
        }
    }
}

Engine::EntityRuntime* Engine::findEntity(const std::string& name)
{
    for (size_t i = 0; i < m_entities.size(); i++)
        if (m_entities[i].name == name)
            return &m_entities[i];
    return 0;
}

Engine::DatasetRuntime* Engine::findDataset(
    std::vector<DatasetRuntime>& datasets, const std::string& name)
{
    for (size_t i = 0; i < datasets.size(); i++)
        if (datasets[i].name == name)
            return &datasets[i];
    return 0;
}

std::string Engine::activeConnectionNameFor(
    const std::string& entity, const std::string& role, const std::string& dataset) const
{
    for (std::map<std::string, Endpoint>::const_iterator it = m_endpoints.begin();
         it != m_endpoints.end(); ++it)
        if (it->second.active && it->second.entity == entity
            && it->second.role == role && it->second.dataset == dataset)
            return it->first;
    return std::string();
}

std::string Engine::firstFreeAutoName() const
{
    for (size_t n = 1; n <= kMaxEndpointsPerComponent + 1; n++)
    {
        std::ostringstream candidate;
        candidate << "cep-" << n;
        std::map<std::string, Endpoint>::const_iterator it = m_endpoints.find(candidate.str());
        if (it == m_endpoints.end() || !it->second.active)
            return candidate.str();
    }
    /* Unreachable: the ceiling refuses first. */
    return "cep-overflow";
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

void Engine::refreshEndpointView(Endpoint& endpoint)
{
    if (endpoint.addressVariable)
    {
        UaVariant value;
        value.setString(UaString(endpoint.address.c_str()));
        UaDataValue dataValue(value, OpcUa_Good, UaDateTime::now(), UaDateTime::now());
        endpoint.addressVariable->setValue(0, dataValue, OpcUa_False);
    }
    if (endpoint.datasetVariable)
    {
        UaVariant value;
        value.setString(UaString(
            (endpoint.role + ":" + endpoint.entity + "." + endpoint.dataset).c_str()));
        UaDataValue dataValue(value, OpcUa_Good, UaDateTime::now(), UaDateTime::now());
        endpoint.datasetVariable->setValue(0, dataValue, OpcUa_False);
    }
}

bool Engine::markSubscriberOperational(const std::string& connectionName)
{
    /* Called on the PubSub io thread. An establish in progress holds m_mutex
     * while waiting for that same io thread, so blocking here would deadlock:
     * try once and let the next DataSetMessage retry. */
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    std::map<std::string, Endpoint>::iterator it = m_endpoints.find(connectionName);
    if (it == m_endpoints.end() || !it->second.active || it->second.role != "subscriber")
        return true;  /* closed or repurposed meanwhile — nothing to signal */
    setEndpointStatus(it->second, EndpointOperational);
    LOG(Log::INF) << "Fx: connection '" << connectionName << "' received its first data — Operational";
    return true;
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
        static const char* const kAllowed[] = {
            "functionalEntity", "role", "dataset", "address",
            "connectionName", "publishingIntervalMs", "ttl", "peer" };
        requireOnlyMembers(request, kAllowed, sizeof kAllowed / sizeof kAllowed[0],
                           "the connection configuration");

        const std::string entityName = requiredString(request, "functionalEntity");
        const std::string role = requiredString(request, "role");
        const std::string datasetName = requiredString(request, "dataset");
        const std::string address = requiredString(request, "address");

        EntityRuntime* entity = findEntity(entityName);
        if (!entity)
            throw std::runtime_error("unknown functional entity '" + entityName + "'");
        if (role != "publisher" && role != "subscriber")
            throw std::runtime_error("role must be 'publisher' or 'subscriber'");

        std::string name;
        if (request.hasMember("connectionName"))
        {
            if (request.member("connectionName").kind() != JsonValue::KindString)
                throw std::runtime_error("'connectionName' must be a string");
            name = request.member("connectionName").asString();
            if (name.empty())
                throw std::runtime_error("'connectionName' must not be empty");
        }
        else
            name = firstFreeAutoName();

        std::map<std::string, Endpoint>::iterator existing = m_endpoints.find(name);
        if (existing != m_endpoints.end())
        {
            if (existing->second.active)
                throw std::runtime_error("connection '" + name + "' is already established");
            if (existing->second.entity != entityName)
                throw std::runtime_error("connection name '" + name
                    + "' belongs to functional entity '" + existing->second.entity + "'");
        }
        else if (m_endpoints.size() >= kMaxEndpointsPerComponent)
        {
            std::ostringstream message;
            message << "connection endpoint limit reached (" << kMaxEndpointsPerComponent
                    << "); close and reuse an existing connection name";
            throw std::runtime_error(message.str());
        }

        const std::string busy = activeConnectionNameFor(entityName, role, datasetName);
        if (!busy.empty())
            throw std::runtime_error("dataset '" + datasetName + "' is already connected as '"
                                     + busy + "' — close that connection first");

        std::string host;
        uint16_t port = 0;
        PubSub::parseNetworkAddress(address, host, port);

        Endpoint endpoint;
        if (existing != m_endpoints.end())
            endpoint = existing->second;
        endpoint.entity = entityName;
        endpoint.dataset = datasetName;
        endpoint.role = role;
        endpoint.address = address;
        endpoint.ownerTag = "fx:" + m_configuration.automationComponent + "/" + entityName + "/" + name;

        EndpointStatus initialStatus = EndpointOperational;

        /* Activate the data plane FIRST: the dynamic add is the step that can
         * fail (colliding wire coordinates, an unbindable address), and
         * address-space nodes are permanent. Ordering the throwing step
         * before any node creation makes a refused establish a clean no-op. */
        if (role == "publisher")
        {
            if (request.hasMember("peer"))
                throw std::runtime_error("'peer' applies to subscriber connections only");

            DatasetRuntime* dataset = findDataset(entity->outputs, datasetName);
            if (!dataset)
                throw std::runtime_error("functional entity '" + entityName
                                         + "' has no output dataset '" + datasetName + "'");

            PubSub::DynamicWriterGroup group;
            group.id = dataset->writerGroupId;
            group.dataSetWriterId = dataset->dataSetWriterId;
            group.publishingIntervalMs = dataset->publishingIntervalMs;
            if (request.hasMember("publishingIntervalMs"))
            {
                if (request.member("publishingIntervalMs").kind() != JsonValue::KindNumber)
                    throw std::runtime_error("'publishingIntervalMs' must be a number");
                group.publishingIntervalMs = request.member("publishingIntervalMs").asNumber();
                if (!(group.publishingIntervalMs > 0))
                    throw std::runtime_error("'publishingIntervalMs' must be positive");
            }
            if (request.hasMember("ttl"))
                group.ttl = static_cast<uint8_t>(requiredUnsigned(request, "ttl", 255));
            group.host = host;
            group.port = port;
            for (size_t f = 0; f < dataset->fields.size(); f++)
            {
                PubSub::DynamicField field;
                field.address = dataset->fields[f].address;
                field.variable = dataset->variables[f];
                group.fields.push_back(field);
            }
            PubSub::Engine::instance().addDynamicWriterGroup(
                endpoint.ownerTag, m_configuration.publisherIdType,
                m_configuration.publisherId, group);
        }
        else
        {
            if (request.hasMember("publishingIntervalMs") || request.hasMember("ttl"))
                throw std::runtime_error(
                    "'publishingIntervalMs'/'ttl' apply to publisher connections only");

            DatasetRuntime* dataset = findDataset(entity->inputs, datasetName);
            if (!dataset)
                throw std::runtime_error("functional entity '" + entityName
                                         + "' has no input dataset '" + datasetName + "'");
            if (!request.hasMember("peer"))
                throw std::runtime_error("subscriber connections need a 'peer' object "
                                         "(the publishing side's wire coordinates)");
            const JsonValue& peer = request.member("peer");
            if (peer.kind() != JsonValue::KindObject)
                throw std::runtime_error("'peer' must be an object");
            static const char* const kPeerAllowed[] = {
                "publisherId", "publisherIdType", "writerGroupId", "dataSetWriterId" };
            requireOnlyMembers(peer, kPeerAllowed, 4, "'peer'");

            PubSub::DynamicReader reader;
            reader.publisherIdType = PubSub::parsePublisherIdType(
                peer.hasMember("publisherIdType")
                    ? requiredString(peer, "publisherIdType") : std::string("UInt16"));
            reader.publisherId = requiredUnsigned(
                peer, "publisherId", PubSub::publisherIdMaximum(reader.publisherIdType));
            reader.writerGroupId = static_cast<uint16_t>(requiredUnsigned(peer, "writerGroupId", 0xFFFF));
            reader.dataSetWriterId = static_cast<uint16_t>(requiredUnsigned(peer, "dataSetWriterId", 0xFFFF));
            reader.host = host;
            reader.port = port;
            for (size_t f = 0; f < dataset->fields.size(); f++)
            {
                PubSub::DynamicField field;
                field.address = dataset->fields[f].address;
                field.variable = dataset->variables[f];
                reader.targets.push_back(field);
            }
            const std::string connectionName = name;
            reader.onFirstData = [connectionName]()
            {
                return Engine::instance().markSubscriberOperational(connectionName);
            };
            PubSub::Engine::instance().addDynamicReader(endpoint.ownerTag, reader);
            initialStatus = EndpointPreOperational;
        }

        if (!endpoint.statusVariable)
        {
            try
            {
                UaVariant zero;
                zero.setInt32(EndpointInitial);
                const UaNodeId endpointNode = addObject(entity->endpointsFolder, name, false);
                endpoint.statusVariable = addVariable(endpointNode, "Status", zero, OpcUaType_Int32);
                UaVariant empty;
                empty.setString(UaString(""));
                endpoint.addressVariable = addVariable(endpointNode, "Address", empty, OpcUaType_String);
                endpoint.datasetVariable = addVariable(endpointNode, "Dataset", empty, OpcUaType_String);
            }
            catch (...)
            {
                /* The endpoint view could not be built after the data plane
                 * came up — undo the data plane so nothing is left half
                 * alive, then report the failure. */
                PubSub::Engine::instance().removeDynamic(endpoint.ownerTag);
                throw;
            }
        }

        endpoint.active = true;
        Endpoint& stored = (m_endpoints[name] = endpoint);
        refreshEndpointView(stored);
        setEndpointStatus(stored, initialStatus);

        connectionId = name;
        JsonValue detail = JsonValue::object();
        detail.setMember("status", JsonValue::string(endpointStatusName(initialStatus)));
        detail.setMember("address", JsonValue::string(address));
        if (role == "publisher")
        {
            const DatasetRuntime* dataset = findDataset(entity->outputs, datasetName);
            JsonValue coordinates = JsonValue::object();
            coordinates.setMember("publisherIdType", JsonValue::string(
                PubSub::publisherIdTypeName(m_configuration.publisherIdType)));
            coordinates.setMember("publisherId", JsonValue::number(
                static_cast<double>(m_configuration.publisherId)));
            coordinates.setMember("writerGroupId", JsonValue::number(dataset->writerGroupId));
            coordinates.setMember("dataSetWriterId", JsonValue::number(dataset->dataSetWriterId));
            detail.setMember("coordinates", coordinates);
        }
        detailJson = detail.serialize();
        LOG(Log::INF) << "Fx: connection '" << name << "' established (" << role
                      << " of " << entityName << "." << datasetName << " on " << address << ")";
        return UaStatus(OpcUa_Good);
    }
    catch (const std::exception& e)
    {
        detailJson = statusDetail(endpointStatusName(EndpointError), e.what());
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

        /* Accept the bare connection id, or a JSON object for symmetry with
         * EstablishConnections. */
        std::string name = requestText;
        if (!name.empty() && name[0] == '{')
        {
            const JsonValue request = parseJsonObject(requestText);
            static const char* const kAllowed[] = { "connectionId" };
            requireOnlyMembers(request, kAllowed, 1, "the close request");
            name = requiredString(request, "connectionId");
        }
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

        detailJson = statusDetail(endpointStatusName(EndpointInitial), "");
        LOG(Log::INF) << "Fx: connection '" << name << "' closed";
        return UaStatus(OpcUa_Good);
    }
    catch (const std::exception& e)
    {
        detailJson = statusDetail(endpointStatusName(EndpointError), e.what());
        LOG(Log::WRN) << "Fx: CloseConnections refused: " << e.what();
        return UaStatus(OpcUa_BadInvalidArgument);
    }
}

AutomationComponentNode::AutomationComponentNode(
    const UaNodeId& nodeId,
    const UaString& name,
    OpcUa_UInt16    browseNameNameSpaceIndex,
    AddressSpace::ASNodeManager* nodeManager):
    OpcUa::BaseObjectType(nodeId, name, browseNameNameSpaceIndex, nodeManager)
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
    pCallback->finishCall(callbackHandle, inputArgumentResults, inputArgumentDiag,
                          outputArguments, finishStatus);
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
        outputs[1] = statusDetail("Error", "expected one String argument: connectionConfiguration");
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
        outputs[0] = statusDetail("Error", "expected one String argument: connectionId");
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
