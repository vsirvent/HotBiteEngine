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

#include <queue>
#include <enet\enet.h>
#include <Core\DXCore.h>
#include <Core\Scheduler.h>
#include <ECS\Coordinator.h>
#include "Commons.h"
#include <Core\LockingQueue.h>

namespace HotBite {
	namespace Engine {
		namespace Network {
			namespace LockStep {

				/**
				 * @class LSClient
				 * @brief Represents a client in a lockstep network architecture
				 *
				 * The LSClient class is used to connect to a server, send commands to the server,
				 * and receive updates from the server. It uses a lockstep network architecture, which means that the
				 * client and server update at fixed intervals (called ticks) and send and receive data at the beginning 
				 * of each tick. The client sends commands to the server at the beginning of each tick, and the server sends
				 * updates to the client at the end of each tick.
				 *
				 * The LSClient class is an implementation of the IEventSender and EventListener interfaces, which means it
				 * can send and receive events. It sends a "server tick" event to the coordinator at the beginning of each tick,
				 * which can be used by other systems to update the game state. It also listens for "new command" events,
				 * which can be used to send commands to the server.
				 */
				class LSClient : public ECS::IEventSender, public ECS::EventListener {
				private:
					/// Flag indicating whether ENet has been initialized
					static bool init;

					/**
					 * @brief Initializes ENet
					 * @return True if initialization was successful, false otherwise
					 */
					bool EnetInit();


				protected:
					/// Alpha value for the jitter calculator
					static constexpr float JITTER_ALPHA = 0.1f;
					/// Maximum size of a packet in bytes
					static constexpr size_t MAX_PKT_SIZE = 64*1024;
					/// Receive timeout in milliseconds
					static constexpr uint32_t RX_TIMEOUT = 1000;
					/// Jitter calculator object
					JitterCalculator jitter_calc{ JITTER_ALPHA };

					/// "Server tick" event object
					ECS::Event tick_event{ this, EVENT_ID_SERVER_TICK };
					/// Shared pointer to a ServerTick object
					std::shared_ptr<ServerTick> server_tick;
					/// Queue of ServerTick objects
					Core::LockingQueue<std::shared_ptr<ServerTick>> server_ticks;
					/// Acknowledgement packet
					ClientAck ack_pkt;
					/// Server address
					std::string addr;
					/// Ping to server
					int ping = -1;
					/// Server port
					uint16_t port;
					/// ENet host object for the client
					ENetHost* client = nullptr;
					/// ENet peer object for the server
					ENetPeer* server = nullptr;
					/// Data buffer for packets
					uint8_t pkt_data[MAX_PKT_SIZE];
					/// ENet packet object
					ENetPacket* packet;
					/// Receive thread
					std::thread rx_thread;
					/// Timer for the current tick
					uint32_t tick_timer = Core::Scheduler::INVALID_TIMER_ID;
					/// Timer for reconnection attempts
					std::atomic<uint32_t> reconnect_timer = 0;
					/// Flag indicating whether the receive thread is running
					bool rx_running = false;
					/// Mutex for synchronizing access to the receive thread
					std::mutex rx_mutex;
					/// Pointer to the ECS coordinator object
					ECS::Coordinator* coordinator = nullptr;
					/// Current jitter value
					uint64_t current_jitter = 0;
					/// Tick period in nanoseconds
					int64_t tick_period = 0;
					/// Tick buffer to have smooth game
					std::atomic<int> required_buffer = 0;
					/**
					 * @brief Receives data from the server
					 *
					 * This method runs in a separate thread and waits for data from the server.
					 * When data is received, it processes it and sends a "server tick" event to the coordinator.
					 */
					void ReceiveData();
					/**
					 * @brief Sends an acknowledgement to the server
					 *
					 * This method sends an acknowledgement packet to the server,
					 * indicating that the client has received and processed the latest update from the server.
					 * This ack includes all the new commands generated by the client since last tick.
					 */
					void SendAck();
					/**
					 * @brief Handles a "new command" event
					 * @param ev "New command" event object
					 *
					 * This method is called when a "new command" event is received. It sends the command to the server.
					 */
					void OnNewCommand(ECS::Event& ev);
				public:
					/**
					 * @brief ID for the "server tick" event
					 */
					static inline ECS::EventId EVENT_ID_SERVER_TICK = ECS::GetEventId<LSClient>(0x00);
					/**
					 * @brief ID for the parameter containing the ServerTick object in the "server tick" event
					 */
					static inline ECS::ParamId EVENT_PARAM_TICK = 0x00;

					/**
					 * @brief Constructs an LSClient object
					 */
					LSClient();
					/**
					 * @brief Destructs an LSClient object
					 */
					virtual ~LSClient();

					/**
					* @brief Initializes the LSClient object
					* @param c Pointer to the ECS coordinator object
					* @param srv_addr Server address
					* @param srv_port Server port
					* @param tick_period_nsec Tick period in nanoseconds(optional, default is 1000000000 / 10)
					* @return True if initialization was successful, false otherwise
					*
					* This method initializes the LSClient object and connects to the server. 
					*/
					bool Init(ECS::Coordinator * c,
						const std::string & srv_addr,
						uint16_t srv_port,
						uint32_t tick_period_nsec = 1000000000 / 10); //10Hz in nanoseconds

					/**
					* @brief Connects to the server
					* @return True if the connection was successful, false otherwise
					*/
					bool Connect();
					/**
					 * @brief Disconnects from the server
					 * @return True if the disconnection was successful, false otherwise
					 */
					bool Disconnect();
					/**
					 * @brief Runs the client
					 *
					 * This method starts the receive thread and begins the main loop of the client.
					 */
					void Run();
					/**
					 * @brief Stops the client
					 *
					 * This method stops the main loop of the client and terminates the receive thread.
					 */
					void Stop();	
					/**
					 * @brief Get current ping in msec.
					 *
					 * This method gets the current ping to the sever in milliseconds.
					 * @return Current ping or -1 of not connected.
					 */
					const int& GetPing() { return ping;  };
				};
			}
		}
	}
}