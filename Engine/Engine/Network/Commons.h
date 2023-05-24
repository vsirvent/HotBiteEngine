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

#include <iostream>
#include <map>
#include <sstream>
#include <utility>
#include <vector>
#include <algorithm>
#include <limits>
#include <list>
#include <deque>
#include <mutex>
#include <ECS\Coordinator.h>
#include <Core\SpinLock.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "enet.lib")

namespace HotBite {
	namespace Engine {
		namespace Network {
			namespace LockStep {

				/**
				 * This is a jitter calculator class, used by the client to pass network commands to the application
				 * in periodic intervals to avoid buffer underflows and game mini blocks.
				 */
				class JitterCalculator {
				private:
					int64_t last_ts = 0;
					float period = 0.0f;
					float jitter = 0.0f;
					float alpha;
				public:
					JitterCalculator(float w);

					void RecordEvent(int64_t ts);
					float GetPeriod() const;
					float GetJitter() const;
				};

				size_t WriteData(uint8_t* dest, size_t& dst_offset, size_t max_size, const uint8_t* source, size_t src_size);
				size_t ReadData(uint8_t* dest, const uint8_t* source, size_t& src_offset, size_t src_size, size_t src_max_size);

				class LSClient;
				class LSServer;
				/**
				 * This is a Game Command, every command of the game that is interchanged between client and server
				 * is encapsulated in this class.
				 * This is an abstract class, in the game every command type needs it's own implementation for serialization
				 * and extraction of specific data.
				 */
				class Command {
				public:
					static inline ECS::EventId EVENT_ID_NEW_CLIENT_COMMAND = ECS::GetEventId<Command>(0x00);
					static inline ECS::EventId EVENT_ID_NEW_SERVER_COMMAND = ECS::GetEventId<Command>(0x01);
					static inline ECS::ParamId EVENT_PARAM_COMMAND = 0x00;
				protected:
					uint16_t type;
					ECS::Entity entity = ECS::INVALID_ENTITY_ID;
				public:
					Command(uint16_t t, ECS::Entity e = ECS::INVALID_ENTITY_ID);

					uint16_t GetType() const { return type; }
					uint16_t GetEntity() const { return entity; }
					// Flush a command to memory
					virtual size_t WriteTo(uint8_t* dest, size_t max_size);
					// Parse command from memory
					virtual size_t ReadFrom(const uint8_t* source, size_t max_size);
				};

				/**
				 * This is the CommandParser, needs to be provided by the application to parse and create the
				 * Commands used in the specific application that are unknown to the library.
				 * This parser needs to be set before using LockStep system.
				 */
				class CommandParser {
				private:
					static std::shared_ptr<CommandParser> cmd_parser;
				public:
					template<class T>
					static void Init() { cmd_parser = std::make_shared<T>(); }
					static std::shared_ptr<CommandParser> Get() { return cmd_parser; }
					virtual size_t Parse(std::shared_ptr<Command>& cmd,const uint8_t* source, size_t& src_offset, size_t max_size) = 0;
				};
				
				/**
				 * This is a Game Packet, every packet sent in the game is encapsulated in this class.
				 * Serialization is not endianess ready. Only prepared for x64 little endian.
				 * As the engine is only ready for x64 windows this is fine. Multiplatform requires rework of serialization.
				 */
				class Packet {
				protected:
					enum ePacketType {
						SERVER_TICK,
						CLIENT_ACK
					};

					//To lock packet for use
					std::mutex m;
					uint16_t type;
					uint64_t frame = 0;

				public:

					ePacketType GetType() const { return (ePacketType)type;  }
					uint64_t GetFrame() const { return frame; }
					
					Packet(ePacketType t);
					// Flush a command to memory
					virtual size_t WriteTo(uint8_t* dest, size_t max_size);
					// Parse command from memory
					virtual size_t ReadFrom(const uint8_t* source, size_t max_size);
				};

				class ServerTick : public Packet {
				private:
					//tick period
					uint64_t tick_period = 0;
					//current server time
					uint64_t server_ts = 0;
					//elapsed time between last client ack and server tick,
					uint64_t ping = 0;
					std::list<std::shared_ptr<Command>> commands;
				public:
					ServerTick();
					uint64_t GetTickPeriod() const { return tick_period;  }
					uint64_t GetPing() const { return ping; }
					uint64_t GetServerTS() const { return server_ts; }
					const std::list<std::shared_ptr<Command>>& GetCommands() const { return commands; }

					// Flush a command to memory
					virtual size_t WriteTo(uint8_t* dest, size_t max_size) override;
					// Parse command from memory
					virtual size_t ReadFrom(const uint8_t* source, size_t max_size) override;

					friend class LSClient;
					friend class LSServer;
				};

				class ClientAck : public Packet {
				private:
					//client time when tick was received
					uint64_t client_ts = 0;
					//server time of the tick received
					uint64_t server_ts = 0;
					//detected jitter between ticks
					uint64_t jitter = 0;
					std::list<std::shared_ptr<Command>> commands;
				
				public:
					ClientAck();
					uint64_t GetClientTS() const { return client_ts; }
					uint64_t GetServerTS() const { return server_ts; }
					uint64_t GetJitter() const { return jitter; }
					const std::list<std::shared_ptr<Command>>& GetCommands() const { return commands; }

					// Flush a command to memory
					virtual size_t WriteTo(uint8_t* dest, size_t max_size) override;
					// Parse command from memory
					virtual size_t ReadFrom(const uint8_t* source, size_t max_size) override;

					friend class LSClient;
					friend class LSServer;
				};
			}
		}
	}
}