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

#include "Commons.h"

namespace HotBite {
	namespace Engine {
		namespace Network {
			namespace LockStep {
				std::shared_ptr<CommandParser> CommandParser::cmd_parser = nullptr;

				size_t WriteData(uint8_t* dest, size_t& dst_offset, size_t max_size, const uint8_t* source, size_t src_size) {
					if (dst_offset + src_size > max_size) return 0;
					memcpy(&dest[dst_offset], source, src_size); dst_offset += src_size;
					return src_size;
				}

				size_t ReadData(uint8_t* dest, const uint8_t* source, size_t& src_offset, size_t src_size, size_t src_max_size) {
					if (src_offset + src_size > src_max_size) return 0;
					memcpy(dest, &source[src_offset], src_size); src_offset += src_size;
					return src_size;
				}

				JitterCalculator::JitterCalculator(float w) : alpha(w) {}

				void JitterCalculator::RecordEvent(int64_t ts) {
					//Don't count packets with bad ts
					if (last_ts != 0) {
						assert(ts > last_ts && "Bad timestamp");
						int64_t ip = ts - last_ts;
						float p = (float)(ip);
						period = period * (1.0f - alpha) + alpha * p;
						float j = (float)(abs(p - period));
						jitter = jitter * (1.0f - alpha) + alpha * j;
					}
					last_ts = ts;
				}

				float JitterCalculator::GetPeriod() const {
					return period;
				}

				float JitterCalculator::GetJitter() const {
					return jitter;
				}
				Command::Command(uint16_t t, ECS::Entity e) : type(t), entity(e) {}

				// Flush a command to memory
				size_t Command::WriteTo(uint8_t* dest, size_t max_size) {
					size_t offset = 0;
					WriteData(dest, offset, max_size, (uint8_t*)&type, sizeof(type));
					WriteData(dest, offset, max_size, (uint8_t*)&entity, sizeof(entity));
					return offset;
				}
				// Parse command from memory
				size_t Command::ReadFrom(const uint8_t* source, size_t max_size) {
					size_t offset = 0;
					if (ReadData((uint8_t*)&type, source, offset, sizeof(type), max_size) != sizeof(type)) { goto end; }
					if (ReadData((uint8_t*)&entity, source, offset, sizeof(entity), max_size) != sizeof(entity)) { goto end; }
				end:
					return offset;
				}

				Packet::Packet(ePacketType t) :type((uint16_t)t) {}

				size_t Packet::WriteTo(uint8_t* dest, size_t max_size) {
					size_t offset = 0;
					WriteData(dest, offset, max_size, (uint8_t*)&type, sizeof(type));
					WriteData(dest, offset, max_size, (uint8_t*)&frame, sizeof(frame));
					return offset;
				}

				size_t Packet::ReadFrom(const uint8_t* source, size_t max_size) {
					size_t offset = 0;
					ReadData((uint8_t*)&type, source, offset, sizeof(type), max_size);
					ReadData((uint8_t*)&frame, source, offset, sizeof(frame), max_size);
					return offset;
				}


				ServerTick::ServerTick() : Packet(ePacketType::SERVER_TICK) {}

				size_t ServerTick::WriteTo(uint8_t* dest, size_t max_size) {
					size_t offset = Packet::WriteTo(dest, max_size);
					WriteData(dest, offset, max_size, (uint8_t*)&tick_period, sizeof(tick_period));
					WriteData(dest, offset, max_size, (uint8_t*)&server_ts, sizeof(server_ts));
					WriteData(dest, offset, max_size, (uint8_t*)&ping, sizeof(ping));
					for (auto const& cmd : commands) {
						offset += cmd->WriteTo(dest + offset, max_size);
					}
					return offset;
				}

				size_t ServerTick::ReadFrom(const uint8_t* source, size_t max_size) {
					size_t offset = Packet::ReadFrom(source, max_size);
					ReadData((uint8_t*)&tick_period, source, offset, sizeof(tick_period), max_size);
					ReadData((uint8_t*)&server_ts, source, offset, sizeof(server_ts), max_size);
					ReadData((uint8_t*)&ping, source, offset, sizeof(ping), max_size);
					commands.clear();
					while (true) {
						std::shared_ptr<Command> cmd = nullptr;
						CommandParser::Get()->Parse(cmd, source, offset, max_size);
						if (cmd == nullptr) {
							break;
						}
						commands.emplace_back(std::move(cmd));
					}
					return offset;
				};

				ClientAck::ClientAck() : Packet(ePacketType::CLIENT_ACK) {}

				size_t ClientAck::WriteTo(uint8_t* dest, size_t max_size) {
					size_t offset = Packet::WriteTo(dest, max_size);
					WriteData(dest, offset, max_size, (uint8_t*)&client_ts, sizeof(client_ts));
					WriteData(dest, offset, max_size, (uint8_t*)&server_ts, sizeof(server_ts));
					WriteData(dest, offset, max_size, (uint8_t*)&jitter, sizeof(jitter));
					int n = 0;
					for (auto const& cmd : commands) {
						offset += cmd->WriteTo(dest + offset, max_size);
						n++;
					}
					if (n > 0) {
						printf("ACK with %d commands sent\n", n);
					}
					return offset;
				}

				size_t ClientAck::ReadFrom(const uint8_t* source, size_t max_size) {
					size_t offset = Packet::ReadFrom(source, max_size);
					ReadData((uint8_t*)&client_ts, source, offset, sizeof(client_ts), max_size);
					ReadData((uint8_t*)&server_ts, source, offset, sizeof(server_ts), max_size);
					ReadData((uint8_t*)&jitter, source, offset, sizeof(jitter), max_size);
					commands.clear();
					while (true) {
						std::shared_ptr<Command> cmd = nullptr;
						CommandParser::Get()->Parse(cmd, source, offset, max_size);
						if (cmd == nullptr) {
							break;
						}
						commands.emplace_back(std::move(cmd));
					}
					return offset;
				};
			}
		}
	}
}