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
#include <memory>
#include <mutex>

namespace AddressSpace
{
class ASNodeManager;
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

/* The FX engine: owns the staged <Fx> configuration, builds the automation
 * component's information model at startup, and serves the connection
 * services. Lifecycle mirrors the PubSub engine: the generated Configurator
 * stages, BaseQuasarServer starts after the address space exists and shuts
 * down before the PubSub engine (FX teardown uses the PubSub io thread). */
class Engine
{
public:
    static Engine& instance();

    void stageConfiguration(const Configuration& configuration);
    void startIfStaged(AddressSpace::ASNodeManager* nodeManager);
    void shutdown();
    bool isStarted() const { return m_started; }

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

private:
    Engine();

    std::unique_ptr<Configuration> m_staged;
    Configuration m_configuration;
    AddressSpace::ASNodeManager* m_nodeManager;
    std::mutex m_mutex;
    std::atomic<bool> m_started;
};

}

#endif /* FX_INCLUDE_FXENGINE_H_ */
