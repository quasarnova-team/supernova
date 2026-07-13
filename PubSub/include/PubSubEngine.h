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

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace AddressSpace
{
class ASNodeManager;
class ChangeNotifyingVariable;
}

namespace PubSub
{

/* Dynamic reconfiguration (used by the Fx module): entities added at runtime
 * carry already-resolved cache-variable pointers — the caller resolves them
 * once at its own startup, so no address-space lookups happen on the hot
 * establish path. The address string stays alongside for diagnostics. */
struct DynamicField
{
    DynamicField(): variable(0) {}
    std::string address;
    AddressSpace::ChangeNotifyingVariable* variable;
};

struct DynamicWriterGroup
{
    DynamicWriterGroup(): id(0), dataSetWriterId(0), publishingIntervalMs(100.0),
                          port(0), ttl(1), loopback(true) {}
    uint16_t id;
    uint16_t dataSetWriterId;
    double publishingIntervalMs;
    std::vector<DynamicField> fields;
    std::string host;
    uint16_t port;
    uint8_t ttl;
    bool loopback;
};

struct DynamicReader
{
    DynamicReader(): publisherIdType(PublisherIdUInt16), publisherId(0),
                     writerGroupId(0), dataSetWriterId(0), port(0) {}
    PublisherIdType publisherIdType;
    uint64_t publisherId;
    uint16_t writerGroupId;
    uint16_t dataSetWriterId;
    std::vector<DynamicField> targets;
    std::string host;
    uint16_t port;
    /* Invoked on the io thread when the first matching DataSetMessage has
     * been applied to the targets; return true when handled (it is retried
     * on subsequent messages until it returns true). Used by Fx to move a
     * subscribing connection endpoint PreOperational -> Operational. */
    std::function<bool()> onFirstData;
};

class Engine
{
public:
    static Engine& instance();

    void stageConfiguration(const Configuration& configuration);
    void startIfStaged(AddressSpace::ASNodeManager* nodeManager);

    /* Starts the engine with no configured entities if it is not running yet
     * (no-op otherwise) — the io thread comes up so dynamic entities can be
     * added later. */
    void ensureStarted();

    /* Add/remove dynamic entities. Mutations run on the engine's io thread
     * (single-thread confinement of all runtime state); failures — duplicate
     * wire coordinates, an unbindable address — throw std::runtime_error and
     * leave the engine exactly as it was. Entities added under one ownerTag
     * are removed together by removeDynamic(ownerTag). The dynamic API is
     * serialized against shutdown by an internal lifecycle mutex; do not call
     * it from the engine's own io thread. */
    void addDynamicWriterGroup(
        const std::string& ownerTag,
        PublisherIdType    publisherIdType,
        uint64_t           publisherId,
        const DynamicWriterGroup& group);
    void addDynamicReader(const std::string& ownerTag, const DynamicReader& reader);
    void removeDynamic(const std::string& ownerTag);
    /* True when any reader under ownerTag has applied at least one
     * DataSetMessage — lets a caller that missed the onFirstData callback
     * (it fires best-effort) reconcile the state it derives from it. */
    bool readerDataSeen(const std::string& ownerTag);

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
        WriterGroupRuntime(): sequenceNumber(0), publisherIdType(PublisherIdUInt16), publisherId(0),
                              stopped(false) {}
        WriterGroupConfig config;
        uint16_t sequenceNumber;
        PublisherIdType publisherIdType;
        uint64_t publisherId;
        std::string ownerTag;  /* empty = static (configured at startup) */
        /* Set (on the io thread) when the group is removed: a timer completion
         * that was already queued when cancel() ran arrives with success and
         * would otherwise publish and re-arm a group that no container
         * references any more. The handler checks this and lets go. */
        bool stopped;
        std::shared_ptr<UdpTransmitter> transmitter;
        std::shared_ptr<boost::asio::steady_timer> timer;
        std::vector<WriterRuntime> writers;
    };

    struct ReaderRuntime
    {
        ReaderRuntime(): port(0), firstDataSignalled(false), dataSeen(false) {}
        DataSetReaderConfig config;
        std::string ownerTag;  /* empty = static */
        std::string host;      /* receiver endpoint this reader listens on */
        uint16_t port;
        std::vector<AddressSpace::ChangeNotifyingVariable*> targets;
        std::function<bool()> onFirstData;
        bool firstDataSignalled;
        bool dataSeen;
    };

    /* One UDP receiver per (host, port), shared by all readers listening
     * there; useCount drops to zero → socket closed and entry erased. */
    struct ReceiverEntry
    {
        ReceiverEntry(): useCount(0) {}
        std::shared_ptr<UdpReceiver> receiver;
        size_t useCount;
    };

    typedef std::pair<std::string, uint16_t> ReceiverKey;
    typedef std::tuple<uint64_t, uint16_t, uint16_t> ReaderKey;

    void buildRuntimes(const Configuration& configuration, AddressSpace::ASNodeManager* nodeManager);
    AddressSpace::ChangeNotifyingVariable* resolveVariable(
        AddressSpace::ASNodeManager* nodeManager,
        const std::string& address) const;
    void startIoThread();
    /* Runs fn on the io thread and waits for it; executes inline when already
     * on the io thread. Exceptions propagate to the caller. */
    void runOnIoThread(const std::function<void()>& fn);
    void acquireReceiver(const std::string& host, uint16_t port, bool armImmediately);
    void releaseReceiver(const std::string& host, uint16_t port);
    void scheduleGroup(const std::shared_ptr<WriterGroupRuntime>& group);
    void publishGroup(const std::shared_ptr<WriterGroupRuntime>& group);
    void handleDatagram(const uint8_t* data, size_t size);

    std::unique_ptr<Configuration> m_staged;
    std::unique_ptr<boost::asio::io_context> m_ioContext;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type> > m_workGuard;
    std::thread m_thread;
    std::vector< std::shared_ptr<WriterGroupRuntime> > m_writerGroups;
    std::map<ReceiverKey, ReceiverEntry> m_receivers;
    std::map<ReaderKey, std::shared_ptr<ReaderRuntime> > m_readers;
    /* Serializes the dynamic API and ensureStarted against shutdown, so a
     * caller can never post onto an io_context being torn down. */
    std::mutex m_lifecycleMutex;
    std::atomic<bool> m_running;
};

}

#endif /* PUBSUB_INCLUDE_PUBSUBENGINE_H_ */
