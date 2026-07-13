/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * FxEngine.h
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

#ifndef FX_INCLUDE_FXENGINE_H_
#define FX_INCLUDE_FXENGINE_H_

#include <FxConfiguration.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <opcua_baseobjecttype.h>

namespace AddressSpace
{
class ASNodeManager;
class ChangeNotifyingVariable;
}

namespace Fx
{

/* ConnectionEndpointStatusEnum, OPC 10000-81 §10.17 */
enum EndpointStatus
{
    EndpointInitial        = 0,
    EndpointReady          = 1,
    EndpointPreOperational = 2,
    EndpointOperational    = 3,
    EndpointError          = 4
};

class AutomationComponentNode;

/* The FX engine: owns the staged <Fx> configuration, builds the automation
 * component's information model at startup (every dataset field resolved to
 * its cache variable, fail-fast), and serves EstablishConnections /
 * CloseConnections in the spec's preconfigured-datasets discipline — a
 * connection manager can only activate what the configuration declared.
 * Lifecycle mirrors the PubSub engine: the generated Configurator stages,
 * BaseQuasarServer starts after the address space exists and shuts FX down
 * before the PubSub engine (FX teardown posts onto the PubSub io thread). */
class Engine
{
public:
    static Engine& instance();

    void stageConfiguration(const Configuration& configuration);
    void startIfStaged(AddressSpace::ASNodeManager* nodeManager);
    void shutdown();
    bool isStarted() const { return m_started; }

    UaStatus establishConnections(
        const std::string& requestJson, std::string& connectionId, std::string& detailJson);
    UaStatus closeConnections(const std::string& requestText, std::string& detailJson);

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

private:
    Engine();

    /* A configured dataset with its fields resolved to cache variables
     * (resolution happens once, at startup). Writer coordinates are used by
     * outputs only. */
    struct DatasetRuntime
    {
        DatasetRuntime(): writerGroupId(0), dataSetWriterId(0), publishingIntervalMs(100.0) {}
        std::string name;
        uint16_t writerGroupId;
        uint16_t dataSetWriterId;
        double publishingIntervalMs;
        std::vector<DatasetField> fields;
        std::vector<AddressSpace::ChangeNotifyingVariable*> variables;
    };

    struct EntityRuntime
    {
        std::string name;
        std::vector<DatasetRuntime> outputs;
        std::vector<DatasetRuntime> inputs;
        UaNodeId endpointsFolder;
    };

    /* Address-space nodes are permanent on both backends, so endpoints are
     * kept and reused by name; the display variables are refreshed on every
     * (re-)establish. */
    struct Endpoint
    {
        Endpoint(): active(false), statusVariable(0), addressVariable(0), datasetVariable(0) {}
        std::string entity;
        std::string dataset;
        std::string role;
        std::string address;
        std::string ownerTag;
        bool active;
        AddressSpace::ChangeNotifyingVariable* statusVariable;
        AddressSpace::ChangeNotifyingVariable* addressVariable;
        AddressSpace::ChangeNotifyingVariable* datasetVariable;
    };

    void buildRuntimes();
    void buildModel();
    void addMethods(const UaNodeId& parentNodeId);
    AddressSpace::ChangeNotifyingVariable* resolveVariable(
        const std::string& address, const std::string& diagnosticContext) const;
    UaNodeId addObject(const UaNodeId& parentNodeId, const std::string& name, bool underObjectsFolder);
    AddressSpace::ChangeNotifyingVariable* addVariable(
        const UaNodeId&    parentNodeId,
        const std::string& name,
        const UaVariant&   initialValue,
        OpcUa_BuiltInType  dataType);

    EntityRuntime* findEntity(const std::string& name);
    static DatasetRuntime* findDataset(std::vector<DatasetRuntime>& datasets, const std::string& name);
    std::string activeConnectionNameFor(
        const std::string& entity, const std::string& role, const std::string& dataset) const;
    size_t endpointCountOf(const std::string& entityName) const;
    std::string firstFreeAutoName(const std::string& entityName) const;
    void setEndpointStatus(Endpoint& endpoint, EndpointStatus status);
    void refreshEndpointView(Endpoint& endpoint);
    /* Called from the PubSub io thread when a subscribing connection's first
     * data arrived; returns false (retry on the next message) if the engine
     * lock is momentarily held. */
    bool markSubscriberOperational(const std::string& connectionName);

    std::unique_ptr<Configuration> m_staged;
    Configuration m_configuration;
    AddressSpace::ASNodeManager* m_nodeManager;
    AutomationComponentNode* m_automationComponent;
    std::vector<EntityRuntime> m_entities;
    std::map<std::string, Endpoint> m_endpoints;
    /* Names whose endpoint view failed half-way through node creation (nodes
     * are permanent, so the name cannot be rebuilt); never auto-picked and
     * refused explicitly with a precise reason. */
    std::set<std::string> m_poisonedNames;
    std::mutex m_mutex;
    std::atomic<bool> m_started;
};

/* The automation component's object node: hosts EstablishConnections and
 * CloseConnections (OPC 10000-81 §6.2.4/§6.2.5 placement) and dispatches
 * their calls to the engine. */
class AutomationComponentNode: public OpcUa::BaseObjectType
{
public:
    AutomationComponentNode(
        const UaNodeId& nodeId,
        const UaString& name,
        OpcUa_UInt16    browseNameNameSpaceIndex,
        AddressSpace::ASNodeManager* nodeManager);

    virtual UaStatus beginCall(
        MethodManagerCallback* pCallback,
        const ServiceContext&  serviceContext,
        OpcUa_UInt32           callbackHandle,
        MethodHandle*          pMethodHandle,
        const UaVariantArray&  inputArguments) override;

    UaStatus callEstablishConnections(
        MethodManagerCallback* pCallback,
        OpcUa_UInt32           callbackHandle,
        const UaVariantArray&  inputArguments);
    UaStatus callCloseConnections(
        MethodManagerCallback* pCallback,
        OpcUa_UInt32           callbackHandle,
        const UaVariantArray&  inputArguments);
};

}

#endif /* FX_INCLUDE_FXENGINE_H_ */
