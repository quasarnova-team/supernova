/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * PubSubConfiguration.cpp
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

#include <PubSubConfiguration.h>

#include <stdexcept>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace PubSub
{

namespace
{

typedef boost::property_tree::ptree Ptree;

FieldConfig parseField(const Ptree& node, const char* attribute)
{
    FieldConfig field;
    field.address = node.get<std::string>(std::string("<xmlattr>.") + attribute);
    if (field.address.empty())
        throw std::runtime_error("PubSub configuration: empty Field address");
    return field;
}

DataSetWriterConfig parseDataSetWriter(const Ptree& node)
{
    DataSetWriterConfig writer;
    writer.id = node.get<uint16_t>("<xmlattr>.id");
    for (Ptree::const_iterator it = node.begin(); it != node.end(); ++it)
    {
        if (it->first == "Field")
            writer.fields.push_back(parseField(it->second, "source"));
    }
    if (writer.fields.empty())
        throw std::runtime_error("PubSub configuration: DataSetWriter without Fields");
    return writer;
}

WriterGroupConfig parseWriterGroup(const Ptree& node)
{
    WriterGroupConfig group;
    group.id = node.get<uint16_t>("<xmlattr>.id");
    group.publishingIntervalMs = node.get<double>("<xmlattr>.publishingIntervalMs");
    if (group.publishingIntervalMs <= 0)
        throw std::runtime_error("PubSub configuration: publishingIntervalMs must be positive");
    for (Ptree::const_iterator it = node.begin(); it != node.end(); ++it)
    {
        if (it->first == "DataSetWriter")
            group.writers.push_back(parseDataSetWriter(it->second));
    }
    if (group.writers.empty())
        throw std::runtime_error("PubSub configuration: WriterGroup without DataSetWriters");
    return group;
}

DataSetReaderConfig parseDataSetReader(const Ptree& node)
{
    DataSetReaderConfig reader;
    reader.publisherId = node.get<uint64_t>("<xmlattr>.publisherId");
    reader.publisherIdType = parsePublisherIdType(node.get<std::string>("<xmlattr>.publisherIdType", "UInt16"));
    reader.writerGroupId = node.get<uint16_t>("<xmlattr>.writerGroupId");
    reader.dataSetWriterId = node.get<uint16_t>("<xmlattr>.dataSetWriterId");
    for (Ptree::const_iterator it = node.begin(); it != node.end(); ++it)
    {
        if (it->first == "Field")
            reader.targets.push_back(parseField(it->second, "target"));
    }
    if (reader.targets.empty())
        throw std::runtime_error("PubSub configuration: DataSetReader without Fields");
    return reader;
}

ConnectionConfig parseConnection(const Ptree& node)
{
    ConnectionConfig connection;
    parseNetworkAddress(node.get<std::string>("<xmlattr>.address"), connection.host, connection.port);
    connection.ttl = node.get<uint8_t>("<xmlattr>.ttl", 1);
    connection.loopback = node.get<bool>("<xmlattr>.loopback", true);
    for (Ptree::const_iterator it = node.begin(); it != node.end(); ++it)
    {
        if (it->first == "WriterGroup")
            connection.writerGroups.push_back(parseWriterGroup(it->second));
        else if (it->first == "DataSetReader")
            connection.readers.push_back(parseDataSetReader(it->second));
    }
    if (connection.writerGroups.empty() && connection.readers.empty())
        throw std::runtime_error("PubSub configuration: Connection with neither WriterGroups nor DataSetReaders");
    return connection;
}

}

PublisherIdType parsePublisherIdType(const std::string& text)
{
    if (text == "Byte")   return PublisherIdByte;
    if (text == "UInt16") return PublisherIdUInt16;
    if (text == "UInt32") return PublisherIdUInt32;
    if (text == "UInt64") return PublisherIdUInt64;
    throw std::runtime_error("PubSub configuration: unknown publisherIdType '" + text + "' (expected Byte, UInt16, UInt32 or UInt64)");
}

void parseNetworkAddress(const std::string& address, std::string& host, uint16_t& port)
{
    const std::string scheme = "opc.udp://";
    if (address.compare(0, scheme.size(), scheme) != 0)
        throw std::runtime_error("PubSub configuration: address '" + address + "' does not start with opc.udp://");
    std::string rest = address.substr(scheme.size());
    while (!rest.empty() && rest[rest.size() - 1] == '/')
        rest.erase(rest.size() - 1);
    size_t colon = rest.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon == rest.size() - 1)
        throw std::runtime_error("PubSub configuration: address '" + address + "' lacks a :port");
    host = rest.substr(0, colon);
    std::string portText = rest.substr(colon + 1);
    int parsed = 0;
    for (size_t i = 0; i < portText.size(); i++)
    {
        if (portText[i] < '0' || portText[i] > '9')
            throw std::runtime_error("PubSub configuration: non-numeric port in address '" + address + "'");
        parsed = parsed * 10 + (portText[i] - '0');
        if (parsed > 65535)
            throw std::runtime_error("PubSub configuration: port out of range in address '" + address + "'");
    }
    if (parsed == 0)
        throw std::runtime_error("PubSub configuration: port out of range in address '" + address + "'");
    port = static_cast<uint16_t>(parsed);
}

Configuration loadConfiguration(const std::string& path)
{
    Ptree document;
    try
    {
        boost::property_tree::read_xml(path, document, boost::property_tree::xml_parser::trim_whitespace);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error("PubSub configuration: cannot parse " + path + ": " + e.what());
    }

    boost::optional<Ptree&> root = document.get_child_optional("PubSub");
    if (!root)
        throw std::runtime_error("PubSub configuration: no <PubSub> root element in " + path);

    Configuration configuration;
    configuration.publisherId = root->get<uint64_t>("<xmlattr>.publisherId");
    configuration.publisherIdType = parsePublisherIdType(root->get<std::string>("<xmlattr>.publisherIdType", "UInt16"));

    for (Ptree::const_iterator it = root->begin(); it != root->end(); ++it)
    {
        if (it->first == "Connection")
            configuration.connections.push_back(parseConnection(it->second));
    }
    if (configuration.connections.empty())
        throw std::runtime_error("PubSub configuration: no <Connection> elements in " + path);

    return configuration;
}

}
