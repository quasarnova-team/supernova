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

#include <functional>
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

    void stageConfiguration(const Configuration& configuration);
    void startIfStaged(AddressSpace::ASNodeManager* nodeManager);
    void shutdown();
    bool isRunning() const { return m_running; }

    /* Dynamic (runtime) reconfiguration — the engine can start with no static
     * configuration and have publishing/subscribing added and removed while
     * running. Entities added dynamically are tagged with an owner id so a
     * whole set can be torn down in one call (the FX module drives this).
     * All mutations run on the engine's io thread; calls block until applied
     * and throw std::runtime_error on failure. */
    void ensureStarted(AddressSpace::ASNodeManager* nodeManager);
    void addDynamic(
        const std::string&      ownerTag,
        PublisherIdType         publisherIdType,
        uint64_t                publisherId,
        const ConnectionConfig& connection);
    void removeDynamic(const std::string& ownerTag);

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
        std::string ownerTag;
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
        std::string ownerTag;
        DataSetReaderConfig config;
        std::vector<AddressSpace::ChangeNotifyingVariable*> targets;
    };

    struct ReceiverRuntime
    {
        std::string ownerTag;
        std::shared_ptr<UdpReceiver> receiver;
    };

    void buildRuntimes(const Configuration& configuration, AddressSpace::ASNodeManager* nodeManager);
    void attachConnection(
        const std::string&      ownerTag,
        PublisherIdType         publisherIdType,
        uint64_t                publisherId,
        const ConnectionConfig& connection,
        AddressSpace::ASNodeManager* nodeManager,
        bool startNow);
    void detachOwner(const std::string& ownerTag);
    void startIoThread();
    void runOnIoThread(const std::function<void()>& work);
    AddressSpace::ChangeNotifyingVariable* resolveVariable(
        AddressSpace::ASNodeManager* nodeManager,
        const std::string& address) const;
    void scheduleGroup(const std::shared_ptr<WriterGroupRuntime>& group);
    void publishGroup(const std::shared_ptr<WriterGroupRuntime>& group);
    void handleDatagram(const uint8_t* data, size_t size);

    typedef std::tuple<uint64_t, uint16_t, uint16_t> ReaderKey;

    std::unique_ptr<Configuration> m_staged;
    std::unique_ptr<boost::asio::io_context> m_ioContext;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type> > m_workGuard;
    std::thread m_thread;
    std::vector< std::shared_ptr<WriterGroupRuntime> > m_writerGroups;
    std::vector<ReceiverRuntime> m_receivers;
    std::map<ReaderKey, std::shared_ptr<ReaderRuntime> > m_readers;
    AddressSpace::ASNodeManager* m_nodeManager;
    bool m_running;
};

}

#endif /* PUBSUB_INCLUDE_PUBSUBENGINE_H_ */
