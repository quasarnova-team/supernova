/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * PubSubUdp.cpp
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

#include <PubSubUdp.h>

#include <LogIt.h>

namespace PubSub
{

using boost::asio::ip::udp;
using boost::asio::ip::address;

UdpTransmitter::UdpTransmitter(
    boost::asio::io_context& io,
    const std::string&       host,
    uint16_t                 port,
    uint8_t                  ttl,
    bool                     loopback):
    m_socket(io),
    m_destination(address::from_string(host), port)
{
    m_socket.open(udp::v4());
    if (m_destination.address().is_multicast())
    {
        m_socket.set_option(boost::asio::ip::multicast::hops(ttl));
        m_socket.set_option(boost::asio::ip::multicast::enable_loopback(loopback));
    }
}

void UdpTransmitter::send(const std::vector<uint8_t>& payload)
{
    boost::system::error_code error;
    m_socket.send_to(boost::asio::buffer(payload), m_destination, 0, error);
    if (error)
        LOG(Log::WRN) << "PubSub: UDP send to " << m_destination.address().to_string()
                      << ":" << m_destination.port() << " failed: " << error.message();
}

UdpReceiver::UdpReceiver(
    boost::asio::io_context& io,
    const std::string&       host,
    uint16_t                 port,
    DatagramHandler          handler):
    m_socket(io),
    m_buffer(65536),
    m_handler(handler)
{
    address groupAddress = address::from_string(host);
    m_socket.open(udp::v4());
    m_socket.set_option(udp::socket::reuse_address(true));
    m_socket.bind(udp::endpoint(udp::v4(), port));
    if (groupAddress.is_multicast())
        m_socket.set_option(boost::asio::ip::multicast::join_group(groupAddress));
}

void UdpReceiver::start()
{
    armReceive();
}

void UdpReceiver::stop()
{
    boost::system::error_code ignored;
    m_socket.close(ignored);
}

void UdpReceiver::armReceive()
{
    m_socket.async_receive_from(
        boost::asio::buffer(m_buffer),
        m_sender,
        [this](const boost::system::error_code& error, size_t bytes)
        {
            if (error == boost::asio::error::operation_aborted)
                return;
            if (error)
            {
                LOG(Log::WRN) << "PubSub: UDP receive failed: " << error.message();
            }
            else if (bytes > 0)
            {
                m_handler(&m_buffer[0], bytes);
            }
            armReceive();
        });
}

}
