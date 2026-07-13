/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * FxConfiguration.h
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

#ifndef FX_INCLUDE_FXCONFIGURATION_H_
#define FX_INCLUDE_FXCONFIGURATION_H_

#include <PubSubConfiguration.h>

#include <stdint.h>
#include <string>
#include <vector>

namespace Fx
{

/* The staged image of the <Fx> configuration section: what this server's
 * automation component offers (output datasets it can publish, input
 * datasets it can receive into) — the spec's "preconfigured datasets"
 * discipline. A connection manager can activate exactly this, nothing else. */

struct DatasetField
{
    std::string name;     /* field name in the dataset view */
    std::string address;  /* dotted cache-variable address, e.g. "FX1.temperature" */
};

struct OutputDatasetConfig
{
    OutputDatasetConfig(): writerGroupId(0), dataSetWriterId(0), publishingIntervalMs(100.0) {}
    std::string name;
    uint16_t writerGroupId;
    uint16_t dataSetWriterId;
    double publishingIntervalMs;  /* default interval; establish may override */
    std::vector<DatasetField> fields;
};

struct InputDatasetConfig
{
    std::string name;
    std::vector<DatasetField> fields;
};

struct FunctionalEntityConfig
{
    std::string name;
    std::vector<OutputDatasetConfig> outputs;
    std::vector<InputDatasetConfig> inputs;
};

struct Configuration
{
    Configuration(): publisherIdType(PubSub::PublisherIdUInt16), publisherId(0) {}
    std::string automationComponent;
    PubSub::PublisherIdType publisherIdType;  /* one publisher identity per component, */
    uint64_t publisherId;                     /* mirroring the <PubSub> element's shape */
    std::vector<FunctionalEntityConfig> entities;
};

/* Defaults empty field names from the leaf of their address
 * ("FX1.temperature" -> "temperature"), then validates everything the XSD
 * alone cannot express: unique names at every level, component-wide unique
 * writer groups, publisherId within its wire type, positive intervals,
 * non-empty datasets and addresses. Throws std::runtime_error with a message
 * precise enough to fix the configuration file on first read. */
void finalizeAndValidate(Configuration& configuration);

}

#endif /* FX_INCLUDE_FXCONFIGURATION_H_ */
