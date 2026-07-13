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

#include <stdexcept>

#include <Configuration.hxx>

namespace Fx
{

namespace
{

/* The source/target discipline is a property of the XML representation
 * (which attribute carries the address, per dataset direction); everything
 * representation-independent is validated by finalizeAndValidate. */
template <typename XsdField>
DatasetField bindField(const XsdField& xsdField, bool isOutput, const std::string& datasetName)
{
    const bool hasSource = xsdField.source().present();
    const bool hasTarget = xsdField.target().present();
    if (isOutput && (!hasSource || hasTarget))
        throw std::runtime_error("Fx configuration: OutputDataset '" + datasetName
            + "': Fields take a 'source' attribute (and no 'target')");
    if (!isOutput && (!hasTarget || hasSource))
        throw std::runtime_error("Fx configuration: InputDataset '" + datasetName
            + "': Fields take a 'target' attribute (and no 'source')");

    DatasetField field;
    field.address = isOutput ? xsdField.source().get() : xsdField.target().get();
    if (xsdField.name().present())
        field.name = xsdField.name().get();
    return field;
}

}

Configuration configurationFromXsd(const ::Configuration::Fx& xsd)
{
    Configuration configuration;
    configuration.automationComponent = xsd.automationComponent();
    configuration.publisherIdType = PubSub::parsePublisherIdType(xsd.publisherIdType());
    configuration.publisherId = xsd.publisherId();

    for (const auto& xsdEntity : xsd.FunctionalEntity())
    {
        FunctionalEntityConfig entity;
        entity.name = xsdEntity.name();

        for (const auto& xsdOutput : xsdEntity.OutputDataset())
        {
            OutputDatasetConfig dataset;
            dataset.name = xsdOutput.name();
            dataset.writerGroupId = xsdOutput.writerGroupId();
            dataset.dataSetWriterId = xsdOutput.dataSetWriterId();
            dataset.publishingIntervalMs = xsdOutput.publishingIntervalMs();
            for (const auto& xsdField : xsdOutput.Field())
                dataset.fields.push_back(bindField(xsdField, true, dataset.name));
            entity.outputs.push_back(dataset);
        }
        for (const auto& xsdInput : xsdEntity.InputDataset())
        {
            InputDatasetConfig dataset;
            dataset.name = xsdInput.name();
            for (const auto& xsdField : xsdInput.Field())
                dataset.fields.push_back(bindField(xsdField, false, dataset.name));
            entity.inputs.push_back(dataset);
        }
        configuration.entities.push_back(entity);
    }

    finalizeAndValidate(configuration);
    return configuration;
}

}
