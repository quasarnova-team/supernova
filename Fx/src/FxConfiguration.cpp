/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * FxConfiguration.cpp
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

#include <FxConfiguration.h>

#include <set>
#include <sstream>
#include <stdexcept>

namespace Fx
{

namespace
{

void refuse(const std::string& what)
{
    throw std::runtime_error("Fx configuration: " + what);
}

void requireUnique(std::set<std::string>& seen, const std::string& name, const std::string& what)
{
    if (name.empty())
        refuse("empty " + what + " name");
    if (!seen.insert(name).second)
        refuse("duplicate " + what + " '" + name + "'");
}

std::string leafOf(const std::string& address)
{
    const size_t dot = address.find_last_of('.');
    return dot == std::string::npos ? address : address.substr(dot + 1);
}

void finalizeFields(
    std::vector<DatasetField>& fields, const std::string& datasetName, const char* direction)
{
    if (fields.empty())
        refuse(std::string(direction) + " '" + datasetName + "' has no Fields");
    std::set<std::string> names;
    for (size_t f = 0; f < fields.size(); f++)
    {
        if (fields[f].address.empty())
            refuse(std::string(direction) + " '" + datasetName + "' has a Field with an empty address");
        if (fields[f].name.empty())
            fields[f].name = leafOf(fields[f].address);
        if (fields[f].name.empty())
            refuse(std::string(direction) + " '" + datasetName + "' has a Field with an empty name");
        if (!names.insert(fields[f].name).second)
            refuse(std::string(direction) + " '" + datasetName + "' has two Fields named '"
                   + fields[f].name + "' (give one an explicit name=\"...\")");
    }
}

}

void finalizeAndValidate(Configuration& configuration)
{
    if (configuration.automationComponent.empty())
        refuse("empty automationComponent name");
    if (configuration.publisherId > PubSub::publisherIdMaximum(configuration.publisherIdType))
    {
        std::ostringstream message;
        message << "publisherId " << configuration.publisherId
                << " does not fit publisherIdType "
                << PubSub::publisherIdTypeName(configuration.publisherIdType);
        refuse(message.str());
    }
    if (configuration.entities.empty())
        refuse("no FunctionalEntity elements");

    std::set<std::string> entityNames;
    std::set<unsigned int> writerGroupIds;
    for (size_t e = 0; e < configuration.entities.size(); e++)
    {
        FunctionalEntityConfig& entity = configuration.entities[e];
        requireUnique(entityNames, entity.name, "FunctionalEntity");

        std::set<std::string> outputNames;
        for (size_t d = 0; d < entity.outputs.size(); d++)
        {
            OutputDatasetConfig& dataset = entity.outputs[d];
            requireUnique(outputNames, dataset.name, "OutputDataset");
            /* Each output dataset owns its writer group: two datasets sharing
             * a group id would interleave independent sequence-number streams
             * under one group id on the wire. */
            if (!writerGroupIds.insert(dataset.writerGroupId).second)
            {
                std::ostringstream message;
                message << "writerGroupId " << dataset.writerGroupId
                        << " is used by more than one OutputDataset";
                refuse(message.str());
            }
            if (!(dataset.publishingIntervalMs > 0))
                refuse("OutputDataset '" + dataset.name + "': publishingIntervalMs must be positive");
            finalizeFields(dataset.fields, dataset.name, "OutputDataset");
        }

        std::set<std::string> inputNames;
        for (size_t d = 0; d < entity.inputs.size(); d++)
        {
            InputDatasetConfig& dataset = entity.inputs[d];
            requireUnique(inputNames, dataset.name, "InputDataset");
            finalizeFields(dataset.fields, dataset.name, "InputDataset");
        }

        if (entity.outputs.empty() && entity.inputs.empty())
            refuse("FunctionalEntity '" + entity.name + "' declares no datasets");
    }
}

}
