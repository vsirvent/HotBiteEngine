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

#include "LockStepClient.h"

namespace HotBite {
	namespace Engine {
		namespace Network {
			namespace LockStep {
				bool LSClient::init = false;

				bool LSClient::EnetInit() {
					return (enet_initialize() == 0);
				}

				LSClient::LSClient() {
					server_tick = std::make_shared<ServerTick>();
				}

				LSClient::~LSClient() {
					Stop();
					enet_packet_destroy(packet);
					enet_host_destroy(client);
				}

				bool LSClient::Init(ECS::Coordinator* c,
					const std::string& srv_addr,
					uint16_t srv_port,
					uint32_t tick_period_nsec) {
					tick_period = tick_period_nsec;
					addr = srv_addr;
					port = srv_port;
					coordinator = c;
					EventListener::Init(c);
					bool ret = false;
					if (!init) {
						if (!EnetInit()) {
							return false;
						}
					}
					// Create a host for the client
					ENetAddress address;
					address.host = ENET_HOST_ANY;
					address.port = 0;
					client = enet_host_create(&address, 1, 1, 0, 0);
					if (client == NULL) {
						throw "An error occurred while trying to create an ENet client host.";
					}
					packet = enet_packet_create(pkt_data, MAX_PKT_SIZE, ENET_PACKET_FLAG_RELIABLE);
					AddEventListener(Command::EVENT_ID_NEW_CLIENT_COMMAND, std::bind(&LSClient::OnNewCommand, this, std::placeholders::_1));
					ret = true;
					return ret;
				}



				bool LSClient::Connect() {
					bool ret = false;
					if (server != nullptr) {
						Disconnect();
					}
					// Connect to the server
					ENetAddress serverAddress;
					enet_address_set_host(&serverAddress, addr.c_str());
					serverAddress.port = port;
					server = enet_host_connect(client, &serverAddress, 1, 0);

					if (server != nullptr) {
						ret = true;
					}
					return ret;
				}

				bool LSClient::Disconnect() {
					bool ret = false;
					if (server != nullptr) {
						enet_peer_disconnect(server, 0);
						ENetEvent ev;
						while (enet_host_service(client, &ev, RX_TIMEOUT) > 0) {
							switch (ev.type) {
							case ENET_EVENT_TYPE_DISCONNECT:
								enet_peer_reset(server);
								server = nullptr;
								ret = true;
								break;
							default:
								break;
							}
						}
					}
					return ret;
				}

				void LSClient::Run() {
					rx_mutex.lock();
					if (!rx_running) {
						rx_running = true;
						rx_thread = std::thread([this, &run = this->rx_running](){
							while (run) {
								ReceiveData();
							}
						});
						Core::Scheduler::Get(Core::DXCore::LOCKSTEP_TICK_THREAD)->RemoveTimerAsync(tick_timer);
						tick_timer = Core::Scheduler::Get(Core::DXCore::LOCKSTEP_TICK_THREAD)->RegisterTimer(tick_period, [this, &run = this->rx_running](const Core::Scheduler::TimerData& ts) {
							required_buffer = (int)(jitter_calc.GetJitter() / jitter_calc.GetPeriod());
							while (server_ticks.Size() > required_buffer) {
								tick_event.SetParam<std::shared_ptr<ServerTick>>(EVENT_PARAM_TICK, server_ticks.Pop());
								coordinator->SendEvent(tick_event);
							}
							return run;
						});
					}
					rx_mutex.unlock();
				}

				void LSClient::Stop() {
					rx_mutex.lock();
					if (rx_running) {
						rx_running = false;
						rx_thread.join();
						Core::Scheduler::Get(Core::DXCore::LOCKSTEP_TICK_THREAD)->RemoveTimer(tick_timer);
						if (reconnect_timer != 0) {
							Core::Scheduler::Get(Core::DXCore::LOCKSTEP_TICK_THREAD)->RemoveTimer(reconnect_timer);
						}
					}
					rx_mutex.unlock();
				}

				void LSClient::OnNewCommand(ECS::Event& ev) {
					std::shared_ptr<Command> new_command = ev.GetParam<std::shared_ptr<Command>>(Command::EVENT_PARAM_COMMAND);
					ack_pkt.m.lock();
					ack_pkt.commands.push_back(new_command);
					ack_pkt.m.unlock();
				}

				void LSClient::ReceiveData() {
					ENetEvent ev;
					if (enet_host_service(client, &ev, 1) > 0) {
						switch (ev.type) {
						case ENET_EVENT_TYPE_CONNECT: {
							//Send instant ack with available data
							SendAck();
						}break;
						case ENET_EVENT_TYPE_RECEIVE: {
							server_tick->m.lock();
							size_t size = server_tick->ReadFrom(ev.packet->data, ev.packet->dataLength);
							server_tick->m.unlock();
							//Send instant ack with available data
							SendAck();
							//Push to rx queue
							server_ticks.Push(server_tick);
							//Create a new tick packe for reception
							server_tick = std::make_shared<ServerTick>();
						}break;
						case ENET_EVENT_TYPE_DISCONNECT: {
							printf("LSClient: Disconnected from server.\n");
							if (reconnect_timer == 0) {
								reconnect_timer = Core::Scheduler::Get(Core::DXCore::LOCKSTEP_TICK_THREAD)->RegisterTimer(1000000000 / 10, [this, &run = this->rx_running](const Core::Scheduler::TimerData& ts) {
									bool retry = !Connect();
									if (!retry) {
										reconnect_timer = 0;
									}
									return retry;
								});
							}
							enet_peer_reset(server);
							server = nullptr;
						}break;
						default:
							break;
						}
					}
				}

				void LSClient::SendAck() {
					//We don't send ack until the tick buffer is processed 
					while (server_ticks.Size() > required_buffer) { Sleep(1); }
					ack_pkt.m.lock();
					ack_pkt.server_ts = server_tick->server_ts;
					ack_pkt.client_ts = Core::Scheduler::Get(Core::DXCore::LOCKSTEP_TICK_THREAD)->GetElapsedNanoSeconds();
					ack_pkt.frame = server_tick->frame;
					jitter_calc.RecordEvent((int64_t)ack_pkt.client_ts);
					ack_pkt.jitter = (uint64_t)jitter_calc.GetJitter();
					if (ack_pkt.commands.size() > 100) {
						printf("Too many commands!\n");
					}
					size_t size = ack_pkt.WriteTo(pkt_data, MAX_PKT_SIZE);
					//Reset commands for next tick
					ack_pkt.commands.clear();
					ack_pkt.m.unlock();
					assert(size <= MAX_PKT_SIZE && "Bad packet size");
					packet = enet_packet_create(pkt_data, size, ENET_PACKET_FLAG_RELIABLE);
					enet_peer_send(server, 0, packet);
					enet_host_flush(client);
					ping = (int)(server_tick->ping / 1000000);
					static int n = 0;
					if (n++ % 100 == 0) {
						printf("Client send ACK: frame = %llu, jitter = %llu msec., ping = %llu msec., period = %f msec.\n", ack_pkt.frame, ack_pkt.jitter / 1000000, server_tick->ping / 1000000, jitter_calc.GetPeriod() / 1000000.0f);
					}
				}
			}
		}
	}
}