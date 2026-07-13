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
#include <sstream>
#include <stdexcept>

#include <Configuration.hxx>

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

uint64_t publisherIdCeiling(PubSub::PublisherIdType type)
{
    switch (type)
    {
        case PubSub::PublisherIdByte:   return 0xFFull;
        case PubSub::PublisherIdUInt16: return 0xFFFFull;
        case PubSub::PublisherIdUInt32: return 0xFFFFFFFFull;
        case PubSub::PublisherIdUInt64: return 0xFFFFFFFFFFFFFFFFull;
    }
    return 0xFFFFull;
}

template <typename XsdField>
DatasetField bindField(
    const XsdField&        xsdField,
    bool                   isOutput,
    const std::string&     datasetName,
    std::set<std::string>& fieldNames)
{
    DatasetField field;

    const bool hasSource = xsdField.source().present();
    const bool hasTarget = xsdField.target().present();
    if (isOutput && (!hasSource || hasTarget))
        refuse("OutputDataset '" + datasetName + "': Fields take a 'source' attribute (and no 'target')");
    if (!isOutput && (!hasTarget || hasSource))
        refuse("InputDataset '" + datasetName + "': Fields take a 'target' attribute (and no 'source')");

    field.address = isOutput ? xsdField.source().get() : xsdField.target().get();
    if (field.address.empty())
        refuse("dataset '" + datasetName + "' has a Field with an empty address");

    field.name = xsdField.name().present() ? std::string(xsdField.name().get()) : leafOf(field.address);
    if (field.name.empty())
        refuse("dataset '" + datasetName + "' has a Field with an empty name");
    if (!fieldNames.insert(field.name).second)
        refuse("dataset '" + datasetName + "' has two Fields named '" + field.name
               + "' (give one an explicit name=\"...\")");
    return field;
}

}

Configuration configurationFromXsd(const ::Configuration::Fx& xsd)
{
    Configuration configuration;

    configuration.automationComponent = xsd.automationComponent();
    if (configuration.automationComponent.empty())
        refuse("empty automationComponent name");

    configuration.publisherIdType = PubSub::parsePublisherIdType(xsd.publisherIdType());
    configuration.publisherId = xsd.publisherId();
    if (configuration.publisherId > publisherIdCeiling(configuration.publisherIdType))
    {
        std::ostringstream message;
        message << "publisherId " << configuration.publisherId
                << " does not fit publisherIdType " << xsd.publisherIdType();
        refuse(message.str());
    }

    std::set<std::string> entityNames;
    std::set<unsigned int> writerGroupIds;

    for (const auto& xsdEntity : xsd.FunctionalEntity())
    {
        FunctionalEntityConfig entity;
        entity.name = xsdEntity.name();
        requireUnique(entityNames, entity.name, "FunctionalEntity");

        std::set<std::string> outputNames;
        std::set<std::string> inputNames;

        for (const auto& xsdOutput : xsdEntity.OutputDataset())
        {
            OutputDatasetConfig dataset;
            dataset.name = xsdOutput.name();
            requireUnique(outputNames, dataset.name, "OutputDataset");

            dataset.writerGroupId = xsdOutput.writerGroupId();
            dataset.dataSetWriterId = xsdOutput.dataSetWriterId();
            /* Each output dataset owns its writer group: two datasets sharing
             * a group id would interleave independent sequence-number streams
             * under one group on the wire. */
            if (!writerGroupIds.insert(dataset.writerGroupId).second)
            {
                std::ostringstream message;
                message << "writerGroupId " << dataset.writerGroupId
                        << " is used by more than one OutputDataset";
                refuse(message.str());
            }

            dataset.publishingIntervalMs = xsdOutput.publishingIntervalMs();
            if (!(dataset.publishingIntervalMs > 0))
                refuse("OutputDataset '" + dataset.name + "': publishingIntervalMs must be positive");

            std::set<std::string> fieldNames;
            for (const auto& xsdField : xsdOutput.Field())
                dataset.fields.push_back(bindField(xsdField, true, dataset.name, fieldNames));
            entity.outputs.push_back(dataset);
        }

        for (const auto& xsdInput : xsdEntity.InputDataset())
        {
            InputDatasetConfig dataset;
            dataset.name = xsdInput.name();
            requireUnique(inputNames, dataset.name, "InputDataset");

            std::set<std::string> fieldNames;
            for (const auto& xsdField : xsdInput.Field())
                dataset.fields.push_back(bindField(xsdField, false, dataset.name, fieldNames));
            entity.inputs.push_back(dataset);
        }

        if (entity.outputs.empty() && entity.inputs.empty())
            refuse("FunctionalEntity '" + entity.name + "' declares no datasets");
        configuration.entities.push_back(entity);
    }

    if (configuration.entities.empty())
        refuse("no FunctionalEntity elements");
    return configuration;
}

}
