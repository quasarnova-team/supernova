/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * FxXsdBinding.cpp
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

#include <FxXsdBinding.h>

#include <set>
#include <stdexcept>

#include <Configuration.hxx>

namespace Fx
{

namespace
{

void requireUniqueName(std::set<std::string>& seen, const std::string& name, const char* what)
{
    if (name.empty())
        throw std::runtime_error(std::string("Fx configuration: empty ") + what + " name");
    if (!seen.insert(name).second)
        throw std::runtime_error(std::string("Fx configuration: duplicate ") + what + " '" + name + "'");
}

}

Configuration configurationFromXsd(const ::Configuration::Fx& xsd)
{
    Configuration configuration;
    configuration.automationComponent = xsd.automationComponent();
    if (configuration.automationComponent.empty())
        throw std::runtime_error("Fx configuration: empty automationComponent name");

    std::set<std::string> entityNames;
    for (const auto& xsdEntity : xsd.FunctionalEntity())
    {
        FunctionalEntityConfig entity;
        entity.name = xsdEntity.name();
        requireUniqueName(entityNames, entity.name, "FunctionalEntity");
        entity.publisherId = xsdEntity.publisherId();
        entity.publisherIdType = PubSub::parsePublisherIdType(xsdEntity.publisherIdType());

        std::set<std::string> datasetNames;
        std::set<unsigned int> writerCoordinates;
        for (const auto& xsdOutput : xsdEntity.OutputDataset())
        {
            OutputDataset dataset;
            dataset.name = xsdOutput.name();
            requireUniqueName(datasetNames, dataset.name, "dataset");
            dataset.writerGroupId = xsdOutput.writerGroupId();
            dataset.dataSetWriterId = xsdOutput.dataSetWriterId();
            unsigned int coordinate =
                (static_cast<unsigned int>(dataset.writerGroupId) << 16) | dataset.dataSetWriterId;
            if (!writerCoordinates.insert(coordinate).second)
                throw std::runtime_error(
                    "Fx configuration: duplicate writerGroupId/dataSetWriterId pair in FunctionalEntity '"
                    + entity.name + "'");
            dataset.publishingIntervalMs = xsdOutput.publishingIntervalMs();
            if (dataset.publishingIntervalMs <= 0)
                throw std::runtime_error("Fx configuration: publishingIntervalMs must be positive");
            std::set<std::string> fieldNames;
            for (const auto& xsdField : xsdOutput.Field())
            {
                DatasetField field;
                field.name = xsdField.name();
                requireUniqueName(fieldNames, field.name, "field");
                if (!xsdField.source().present() || xsdField.target().present())
                    throw std::runtime_error("Fx configuration: OutputDataset Fields take a 'source' attribute (and no 'target')");
                field.address = xsdField.source().get();
                if (field.address.empty())
                    throw std::runtime_error("Fx configuration: empty Field source");
                dataset.fields.push_back(field);
            }
            if (dataset.fields.empty())
                throw std::runtime_error("Fx configuration: OutputDataset '" + dataset.name + "' has no Fields");
            entity.outputs.push_back(dataset);
        }

        for (const auto& xsdInput : xsdEntity.InputDataset())
        {
            InputDataset dataset;
            dataset.name = xsdInput.name();
            requireUniqueName(datasetNames, dataset.name, "dataset");
            std::set<std::string> fieldNames;
            for (const auto& xsdField : xsdInput.Field())
            {
                DatasetField field;
                field.name = xsdField.name();
                requireUniqueName(fieldNames, field.name, "field");
                if (!xsdField.target().present() || xsdField.source().present())
                    throw std::runtime_error("Fx configuration: InputDataset Fields take a 'target' attribute (and no 'source')");
                field.address = xsdField.target().get();
                if (field.address.empty())
                    throw std::runtime_error("Fx configuration: empty Field target");
                dataset.fields.push_back(field);
            }
            if (dataset.fields.empty())
                throw std::runtime_error("Fx configuration: InputDataset '" + dataset.name + "' has no Fields");
            entity.inputs.push_back(dataset);
        }

        if (entity.outputs.empty() && entity.inputs.empty())
            throw std::runtime_error(
                "Fx configuration: FunctionalEntity '" + entity.name + "' declares no datasets");
        configuration.entities.push_back(entity);
    }

    if (configuration.entities.empty())
        throw std::runtime_error("Fx configuration: no FunctionalEntity elements");
    return configuration;
}

}
