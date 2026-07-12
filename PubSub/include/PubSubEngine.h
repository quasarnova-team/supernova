/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * PubSubEngine.h
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

#ifndef PUBSUB_INCLUDE_PUBSUBENGINE_H_
#define PUBSUB_INCLUDE_PUBSUBENGINE_H_

#include <PubSubConfiguration.h>
#include <PubSubUdp.h>
#include <PubSubWire.h>

#include <map>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace AddressSpace
{
class ASNodeManager;
class ChangeNotifyingVariable;
}

namespace PubSub
{

class Engine
{
public:
    static Engine& instance();

    void initialize(const std::string& serverConfigFilePath, AddressSpace::ASNodeManager* nodeManager);
    void initializeFromFile(const std::string& pubSubConfigPath, AddressSpace::ASNodeManager* nodeManager);
    void shutdown();
    bool isRunning() const { return m_running; }

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

private:
    Engine();

    struct WriterRuntime
    {
        WriterRuntime(): sequenceNumber(0) {}
        DataSetWriterConfig config;
        uint16_t sequenceNumber;
        std::vector<AddressSpace::ChangeNotifyingVariable*> variables;
    };

    struct WriterGroupRuntime
    {
        WriterGroupRuntime(): sequenceNumber(0), publisherIdType(PublisherIdUInt16), publisherId(0) {}
        WriterGroupConfig config;
        uint16_t sequenceNumber;
        PublisherIdType publisherIdType;
        uint64_t publisherId;
        std::shared_ptr<UdpTransmitter> transmitter;
        std::shared_ptr<boost::asio::steady_timer> timer;
        std::vector<WriterRuntime> writers;
    };

    struct ReaderRuntime
    {
        DataSetReaderConfig config;
        std::vector<AddressSpace::ChangeNotifyingVariable*> targets;
    };

    void buildRuntimes(const Configuration& configuration, AddressSpace::ASNodeManager* nodeManager);
    AddressSpace::ChangeNotifyingVariable* resolveVariable(
        AddressSpace::ASNodeManager* nodeManager,
        const std::string& address) const;
    void scheduleGroup(const std::shared_ptr<WriterGroupRuntime>& group);
    void publishGroup(const std::shared_ptr<WriterGroupRuntime>& group);
    void handleDatagram(const uint8_t* data, size_t size);

    typedef std::tuple<uint64_t, uint16_t, uint16_t> ReaderKey;

    std::unique_ptr<boost::asio::io_context> m_ioContext;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type> > m_workGuard;
    std::thread m_thread;
    std::vector< std::shared_ptr<WriterGroupRuntime> > m_writerGroups;
    std::vector< std::shared_ptr<UdpReceiver> > m_receivers;
    std::map<ReaderKey, std::shared_ptr<ReaderRuntime> > m_readers;
    bool m_running;
};

}

#endif /* PUBSUB_INCLUDE_PUBSUBENGINE_H_ */
