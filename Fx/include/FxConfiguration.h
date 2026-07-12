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

struct DatasetField
{
    std::string name;
    std::string address;
};

struct OutputDataset
{
    OutputDataset(): writerGroupId(0), dataSetWriterId(0), publishingIntervalMs(100.0) {}
    std::string name;
    uint16_t writerGroupId;
    uint16_t dataSetWriterId;
    double publishingIntervalMs;
    std::vector<DatasetField> fields;
};

struct InputDataset
{
    std::string name;
    std::vector<DatasetField> fields;
};

struct FunctionalEntityConfig
{
    FunctionalEntityConfig(): publisherIdType(PubSub::PublisherIdUInt16), publisherId(0) {}
    std::string name;
    PubSub::PublisherIdType publisherIdType;
    uint64_t publisherId;
    std::vector<OutputDataset> outputs;
    std::vector<InputDataset> inputs;
};

struct Configuration
{
    std::string automationComponent;
    std::vector<FunctionalEntityConfig> entities;
};

}

#endif /* FX_INCLUDE_FXCONFIGURATION_H_ */
