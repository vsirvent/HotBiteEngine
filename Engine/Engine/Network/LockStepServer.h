/*
The HotBite Game Engine

Copyright(c) 2023 Vicente Sirvent Orts

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "enet/enet.h"
#include "Commons.h"
#include <Core\Scheduler.h>
#include <thread>
#include <mutex>

/**
 * @brief A hash function for std::pair objects
 */
struct pair_hash
{
    /**
     * @brief Hashes a std::pair object
     * @tparam T1 Type of the first element in the pair
     * @tparam T2 Type of the second element in the pair
     * @param p The pair to be hashed
     * @return The hash of the pair
     */
    template <class T1, class T2>
    std::size_t operator() (const std::pair<T1, T2>& p) const {
        return std::hash<T1>()(p.first) ^ std::hash<T2>()(p.second);
    }
};

namespace HotBite {
    namespace Engine {
        namespace Network {
            namespace LockStep {
                /**
                 * @brief A lock-step server implementation
                 *
                 * This class implements a lock-step server, which uses a fixed tick rate to synchronize the state of connected clients.
                 * The server sends updates to clients at the beginning of each tick, and waits for
                 * acknowledgement from each client before proceeding to the next tick.
                 */
                class LSServer
                {
                private:
                    /// Flag indicating whether the server has been initialized
                    static bool init;

                    /// Server address
                    ENetAddress address;
                    /// Server host
                    ENetHost* server = nullptr;
                    /// Tick period in nanoseconds
                    int64_t tick_period = 0;
                    int32_t tick_timeouts = 0;
                    static constexpr int32_t MAX_TICK_TIMEOUTS = 20;
                    /// Receive thread
                    std::thread rx_thread;
                    /// Flag indicating whether the receive thread is running
                    bool rx_running = false;
                    /// Mutex for synchronizing access to the receive thread
                    std::mutex rx_mutex;
                    /// Timer for the current tick
                    uint32_t tick_timer = Core::Scheduler::INVALID_TIMER_ID;

                    /// Server tick object
                    ServerTick tick;

                    /// Maximum packet size
                    static constexpr size_t MAX_PKT_SIZE = 64 * 1024;
                    /// Receive timeout value in milliseconds
                    static constexpr uint32_t RX_TIMEOUT = 1000;
                    /// Server tick object
                    ServerTick server_tick;
                    /// Mutex for synchronizing access to the server tick object
                    std::mutex server_tick_mutex;
                    /// Data buffer for packets
                    uint8_t pkt_data[MAX_PKT_SIZE];
                    /// ENet packet object
                    ENetPacket* packet;
                    /// Number of players currently connected to the server
                    uint32_t nplayers;

                    /**
                     * @brief Structure for storing client data
                     */
                    struct ClientData {
                        /// Ping value for the client
                        uint64_t ping = 0;
                        /// Timestamp of the last tick received from the client
                        uint64_t last_tick_ts = 0;
                        /// Timestamp of the last acknowledgement received from the client
                        uint64_t last_ack_ts = 0;
                        /// Current frame number for the client
                        uint64_t frame = 0;
                        /// Client acknowledgement packet
                        ClientAck ack;
                        /// ENet peer for the client
                        ENetPeer* peer = nullptr;
                    };
                    /// Mutex for synchronizing access to the client data map
                    std::mutex clients_data_mutex;
                    /// Map of client data, keyed by the pair (address, port) of the client's ENet peer
                    std::unordered_map<std::pair<uint32_t, uint32_t>, std::shared_ptr<ClientData>, pair_hash> clients_data;

                    /**
                     * @brief Sends a tick update to a client
                     * @param peer ENet peer of the client to receive the update
                     *
                     * This method sends a tick update packet to the specified client.
                     * The tick update contains commands received from the client since the last tick.
                     */
                    void SendTick(ENetPeer* peer);
                    /**
                     * @brief Receives data from connected clients
                     *
                     * This method listens for incoming data from the clients connected to the server, and processes the received data as appropriate.
                     * This includes handling client input commands and acknowledgement packets.
                     */
                    void ReceiveData();
                public:
                    /// Constructor
                    LSServer() = default;
                    /// Destructor
                    ~LSServer();

                    /**
                     * @brief Initializes the lock-step server
                     * @param srv_port Port number for the server
                     * @param num_clients Maximum number of clients that can connect to the server
                     * @param tick_period_nsec Tick period in nanoseconds
                     * @return `true` if the server was successfully initialized, `false` otherwise
                     *
                     * This method initializes the lock-step server, setting up the ENet host.
                     */
                    bool Init(uint16_t srv_port,
                        uint8_t num_clients,
                        uint32_t tick_period_nsec = 1000000000 / 10);
                    /**
                     * @brief Runs the lock-step server
                     *
                     * This method runs the main loop thread of the lock-step server,
                     * sending tick updates to clients and processing incoming data.
                     */
                    void Run();
                    /**
                     * @brief Stops the lock-step server
                     *
                     * This method stops the lock-step server, terminating the receive thread.
                     */
                    void Stop();
                };
			}
		}
	}
}