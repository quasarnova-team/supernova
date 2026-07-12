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

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <opcua_baseobjecttype.h>

namespace AddressSpace
{
class ASNodeManager;
class ChangeNotifyingVariable;
}

namespace Fx
{

/* ConnectionEndpointStatusEnum, opc.ua.fx.ac 1.00.03 */
enum EndpointStatus
{
    EndpointInitial        = 0,
    EndpointReady          = 1,
    EndpointPreOperational = 2,
    EndpointOperational    = 3,
    EndpointError          = 4
};

class AutomationComponentNode;

class Engine
{
public:
    static Engine& instance();

    void stageConfiguration(const Configuration& configuration);
    void startIfStaged(AddressSpace::ASNodeManager* nodeManager);
    void shutdown();
    bool isStarted() const { return m_started; }

    UaStatus establishConnections(const std::string& requestJson, std::string& connectionId, std::string& detailJson);
    UaStatus closeConnections(const std::string& requestJson, std::string& detailJson);

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

private:
    Engine();

    struct Endpoint
    {
        Endpoint(): active(false), statusVariable(0) {}
        std::string entity;
        std::string dataset;
        std::string role;
        std::string ownerTag;
        bool active;
        AddressSpace::ChangeNotifyingVariable* statusVariable;
    };

    void buildModel();
    UaNodeId addPlainObject(const UaNodeId& parentNodeId, const std::string& name);
    AddressSpace::ChangeNotifyingVariable* addVariable(
        const UaNodeId&    parentNodeId,
        const std::string& name,
        const UaVariant&   initialValue,
        OpcUa_BuiltInType  dataType);
    void referenceDatasetFields(const UaNodeId& datasetNodeId, const std::vector<DatasetField>& fields);

    const FunctionalEntityConfig* findEntity(const std::string& name) const;
    static const OutputDataset* findOutput(const FunctionalEntityConfig& entity, const std::string& name);
    static const InputDataset* findInput(const FunctionalEntityConfig& entity, const std::string& name);
    void setEndpointStatus(Endpoint& endpoint, EndpointStatus status);

    std::unique_ptr<Configuration> m_staged;
    Configuration m_configuration;
    AddressSpace::ASNodeManager* m_nodeManager;
    AutomationComponentNode* m_automationComponent;
    std::map<std::string, UaNodeId> m_endpointsFolderOfEntity;
    std::map<std::string, Endpoint> m_endpoints;
    std::mutex m_mutex;
    bool m_started;
};

class AutomationComponentNode: public OpcUa::BaseObjectType
{
public:
    AutomationComponentNode(
        const UaNodeId& nodeId,
        const UaString& name,
        OpcUa_UInt16    browseNameNameSpaceIndex);

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
