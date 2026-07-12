/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * PubSubUdp.h
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

#ifndef PUBSUB_INCLUDE_PUBSUBUDP_H_
#define PUBSUB_INCLUDE_PUBSUBUDP_H_

#include <stdint.h>
#include <functional>
#include <string>
#include <vector>

#include <boost/asio.hpp>

namespace PubSub
{

class UdpTransmitter
{
public:
    UdpTransmitter(
        boost::asio::io_context& io,
        const std::string&       host,
        uint16_t                 port,
        uint8_t                  ttl,
        bool                     loopback);

    void send(const std::vector<uint8_t>& payload);

private:
    boost::asio::ip::udp::socket   m_socket;
    boost::asio::ip::udp::endpoint m_destination;
};

class UdpReceiver
{
public:
    typedef std::function<void (const uint8_t* data, size_t size)> DatagramHandler;

    UdpReceiver(
        boost::asio::io_context& io,
        const std::string&       host,
        uint16_t                 port,
        DatagramHandler          handler);

    void start();
    void stop();

private:
    void armReceive();

    boost::asio::ip::udp::socket   m_socket;
    boost::asio::ip::udp::endpoint m_sender;
    std::vector<uint8_t>           m_buffer;
    DatagramHandler                m_handler;
};

}

#endif /* PUBSUB_INCLUDE_PUBSUBUDP_H_ */
