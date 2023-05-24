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

#include "LockStepServer.h"

namespace HotBite {
	namespace Engine {
		namespace Network {
			namespace LockStep {
				bool LSServer::init = false;

				// Destructor
				LSServer::~LSServer()
				{
					Stop();
					enet_host_destroy(server);
					enet_packet_destroy(packet);
				}

				bool LSServer::Init(uint16_t srv_port,
					uint8_t num_clients,
					uint32_t tick_period_nsec)
				{
					bool ret = false;
					nplayers = num_clients;
					tick_period = tick_period_nsec;
					// Initialize enet
					if (!init) {
						if (enet_initialize() != 0)
						{
							printf("Error initializing enet\n");
							goto end;
						}
						init = true;
					}
					// Create the server address
					address.host = ENET_HOST_ANY;
					address.port = srv_port;
					server_tick.tick_period = tick_period_nsec;
					server_tick.server_ts = Core::Scheduler::Get()->GetElapsedNanoSeconds();
					// Create the server host
					server = enet_host_create(&address, num_clients, 1, 0, 0);
					if (!server)
					{
						printf("Error creating server host\n");
						goto end;
					}
					ret = true;
				end:
					return ret;
				}

				void LSServer::Run() {
					rx_mutex.lock();
					if (!rx_running) {
						rx_running = true;
						rx_thread = std::thread([this, &run = this->rx_running]() {
							while (run) {
								ReceiveData();
							}
						});
						Core::Scheduler::Get()->RemoveTimerAsync(tick_timer);
						tick_timer = Core::Scheduler::Get()->RegisterTimer(tick_period, [this, &run = this->rx_running](const Core::Scheduler::TimerData& ts) {							
							std::vector<std::shared_ptr<ClientData>> clients;							
							clients_data_mutex.lock();
							clients.reserve(clients_data.size());
							for (auto const& kv : clients_data) {
								if (kv.second->ack.frame == server_tick.frame) {
									clients.push_back(kv.second);									
								}
							}
							clients_data_mutex.unlock();
							if (clients.size() == nplayers) {
								tick_timeouts = 0;
								//Generate tick packet
								server_tick.server_ts = Core::Scheduler::Get()->GetElapsedNanoSeconds();
								server_tick.commands.clear();
								for (auto const& c : clients) {
									c->ack.m.lock();
									server_tick.commands.insert(server_tick.commands.end(), c->ack.commands.begin(), c->ack.commands.end());
									c->ack.m.unlock();
								}
								server_tick.frame++;
								//Send tick packet to each client
								for (auto const& c : clients) {
									c->ack.m.lock();
									c->last_tick_ts = Core::Scheduler::Get()->GetElapsedNanoSeconds();
									server_tick.ping = c->ping;
									SendTick(c->peer);
									c->ack.m.unlock();
								}
							}
							else {
								if (tick_timeouts++ > MAX_TICK_TIMEOUTS) {
									tick_timeouts = 0;
									printf("Warning, player ack timeout, resend server_tick...\n");
									//Resend tick to clients that have not
									//reply with ack, just in case
									for (auto const& kv : clients_data) {
										if (kv.second->ack.frame < server_tick.frame) {
											//Send tick packet to each client
											for (auto const& c : clients) {
												c->ack.m.lock();
												SendTick(c->peer);
												c->ack.m.unlock();
											}
										}
									}
								}
							}
							return run;
						});
					}
					rx_mutex.unlock();
				}

				void LSServer::SendTick(ENetPeer* peer) {
					server_tick_mutex.lock();
					size_t size = server_tick.WriteTo(pkt_data, MAX_PKT_SIZE);
					assert(size <= MAX_PKT_SIZE && "Bad packet size");
					packet = enet_packet_create(pkt_data, size, ENET_PACKET_FLAG_RELIABLE);
					enet_peer_send(peer, 0, packet);
					server_tick_mutex.unlock();
				}

				void LSServer::Stop() {
					rx_mutex.lock();
					if (rx_running) {
						rx_running = false;
						rx_thread.join();
						Core::Scheduler::Get()->RemoveTimer(tick_timer);
					}
					rx_mutex.unlock();
				}

				void LSServer::ReceiveData()
				{
					// Process incoming messages
					ENetEvent ev;
					if (enet_host_service(server, &ev, 1) > 0)
					{
						switch (ev.type)
						{
						
						// New client connected
						case ENET_EVENT_TYPE_CONNECT:
						{
							printf("A new client connected from %x:%u.\n",
								ev.peer->address.host,
								ev.peer->address.port);
							std::pair<uint32_t, uint32_t> k(ev.peer->address.host, ev.peer->address.port);
							clients_data_mutex.lock();
							auto it = clients_data.find(k);
							if (it == clients_data.end()) {
								clients_data[k] = std::make_shared<ClientData>();
								clients_data[k]->peer = ev.peer;
								//We don't allow re-connections, game starts at frame 0
								server_tick.frame = 0;
							}
							else {
								printf("Client was already connected.");
							}
							SendTick(ev.peer);
							clients_data_mutex.unlock();
							break;
						}
						// Client disconnected
						case ENET_EVENT_TYPE_DISCONNECT:
						{
							printf("Client disconnected\n");
							std::pair<uint32_t, uint32_t> k(ev.peer->address.host, ev.peer->address.port);
							clients_data_mutex.lock();
							auto it = clients_data.find(k);
							if (it != clients_data.end()) {
								std::shared_ptr<ClientData> cd = it->second;
								cd->ack.m.lock();
								clients_data.erase(k);
								cd->ack.m.unlock();
							}
							else {
								printf("Client not found.");
							}
							clients_data_mutex.unlock();
							break;
						}
						// Message received
						case ENET_EVENT_TYPE_RECEIVE:
						{
							// Process the client's inputs and update the game state
							std::pair<uint32_t, uint32_t> k(ev.peer->address.host, ev.peer->address.port);
							std::shared_ptr<ClientData> client = nullptr;
							{
								clients_data_mutex.lock();
								auto it = clients_data.find(k);
								if (it != clients_data.end()) {
									client = it->second;
								}
								clients_data_mutex.unlock();
							}
							if (client != nullptr) {
								client->ack.m.lock();
								uint64_t now = Core::Scheduler::Get()->GetElapsedNanoSeconds();
								client->last_ack_ts = now;
								if (client->last_tick_ts > 0) {
									client->ping = now - client->last_tick_ts;
								}
								client->frame = server_tick.frame;
								size_t size = client->ack.ReadFrom(ev.packet->data, ev.packet->dataLength);
								client->ack.m.unlock();
								static int n = 0;
								if (n++ % 100 == 0) {
									printf("client: ping = %llu msec. frame = %llu\n", client->ping / 1000000, client->frame);
								}
							}
							break;
						}
						}
					}
				}
			}
		}
	}
}