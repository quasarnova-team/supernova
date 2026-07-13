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

#include <stdexcept>

#include <LogIt.h>

namespace Fx
{

Engine::Engine(): m_nodeManager(0), m_started(false)
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
    m_started = false;
    LOG(Log::INF) << "Fx: engine stopped";
}

}
