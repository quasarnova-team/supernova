/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * PubSubXsdBinding.cpp
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

#include <PubSubXsdBinding.h>

#include <stdexcept>

#include <Configuration.hxx>

namespace PubSub
{

namespace
{

FieldConfig fieldFromXsd(const ::Configuration::PubSubField& xsd, bool wantSource)
{
    FieldConfig field;
    if (wantSource)
    {
        if (!xsd.source().present() || xsd.target().present())
            throw std::runtime_error("PubSub configuration: DataSetWriter Fields take a 'source' attribute (and no 'target')");
        field.address = xsd.source().get();
    }
    else
    {
        if (!xsd.target().present() || xsd.source().present())
            throw std::runtime_error("PubSub configuration: DataSetReader Fields take a 'target' attribute (and no 'source')");
        field.address = xsd.target().get();
    }
    if (field.address.empty())
        throw std::runtime_error("PubSub configuration: empty Field address");
    return field;
}

}

Configuration configurationFromXsd(const ::Configuration::PubSub& xsd)
{
    Configuration configuration;
    configuration.publisherId = xsd.publisherId();
    configuration.publisherIdType = parsePublisherIdType(xsd.publisherIdType());

    for (const auto& xsdConnection : xsd.Connection())
    {
        ConnectionConfig connection;
        parseNetworkAddress(xsdConnection.address(), connection.host, connection.port);
        connection.ttl = xsdConnection.ttl();
        connection.loopback = xsdConnection.loopback();

        for (const auto& xsdGroup : xsdConnection.WriterGroup())
        {
            WriterGroupConfig group;
            group.id = xsdGroup.id();
            group.publishingIntervalMs = xsdGroup.publishingIntervalMs();
            if (group.publishingIntervalMs <= 0)
                throw std::runtime_error("PubSub configuration: publishingIntervalMs must be positive");
            for (const auto& xsdWriter : xsdGroup.DataSetWriter())
            {
                DataSetWriterConfig writer;
                writer.id = xsdWriter.id();
                for (const auto& xsdField : xsdWriter.Field())
                    writer.fields.push_back(fieldFromXsd(xsdField, true));
                group.writers.push_back(writer);
            }
            connection.writerGroups.push_back(group);
        }

        for (const auto& xsdReader : xsdConnection.DataSetReader())
        {
            DataSetReaderConfig reader;
            reader.publisherId = xsdReader.publisherId();
            reader.publisherIdType = parsePublisherIdType(xsdReader.publisherIdType());
            reader.writerGroupId = xsdReader.writerGroupId();
            reader.dataSetWriterId = xsdReader.dataSetWriterId();
            for (const auto& xsdField : xsdReader.Field())
                reader.targets.push_back(fieldFromXsd(xsdField, false));
            connection.readers.push_back(reader);
        }

        if (connection.writerGroups.empty() && connection.readers.empty())
            throw std::runtime_error("PubSub configuration: Connection with neither WriterGroups nor DataSetReaders");
        configuration.connections.push_back(connection);
    }
    if (configuration.connections.empty())
        throw std::runtime_error("PubSub configuration: no Connection elements");

    return configuration;
}

}
