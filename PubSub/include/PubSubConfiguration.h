/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * PubSubConfiguration.h
 *
 *  Created on: 12 Jul 2026
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

#ifndef PUBSUB_INCLUDE_PUBSUBCONFIGURATION_H_
#define PUBSUB_INCLUDE_PUBSUBCONFIGURATION_H_

#include <PubSubWire.h>

#include <stdint.h>
#include <string>
#include <vector>

namespace PubSub
{

struct FieldConfig
{
    std::string address;
};

struct DataSetWriterConfig
{
    DataSetWriterConfig(): id(0) {}
    uint16_t id;
    std::vector<FieldConfig> fields;
};

struct WriterGroupConfig
{
    WriterGroupConfig(): id(0), publishingIntervalMs(1000.0) {}
    uint16_t id;
    double publishingIntervalMs;
    std::vector<DataSetWriterConfig> writers;
};

struct DataSetReaderConfig
{
    DataSetReaderConfig(): publisherIdType(PublisherIdUInt16), publisherId(0), writerGroupId(0), dataSetWriterId(0) {}
    PublisherIdType publisherIdType;
    uint64_t publisherId;
    uint16_t writerGroupId;
    uint16_t dataSetWriterId;
    std::vector<FieldConfig> targets;
};

struct ConnectionConfig
{
    ConnectionConfig(): port(0), ttl(1), loopback(true) {}
    std::string host;
    uint16_t port;
    uint8_t ttl;
    bool loopback;
    std::vector<WriterGroupConfig> writerGroups;
    std::vector<DataSetReaderConfig> readers;
};

struct Configuration
{
    Configuration(): publisherIdType(PublisherIdUInt16), publisherId(0) {}
    PublisherIdType publisherIdType;
    uint64_t publisherId;
    std::vector<ConnectionConfig> connections;
};

void parseNetworkAddress(const std::string& address, std::string& host, uint16_t& port);

PublisherIdType parsePublisherIdType(const std::string& text);

}

#endif /* PUBSUB_INCLUDE_PUBSUBCONFIGURATION_H_ */
