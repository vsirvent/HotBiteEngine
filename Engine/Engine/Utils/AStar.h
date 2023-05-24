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

#include <map>
#include <unordered_set>
#include <array>
#include <mutex>

#include <Defines.h>

namespace HotBite {
	namespace Engine {
		namespace Core {

			class SquareGrid {
			private:
				struct Cell {
					Int2 position;
					const Cell* parent = nullptr;
					float cost = 0.0f;
					float to_target = 0.0f;
					bool wall = false;

					Cell() {}

					Cell(int xpos, int ypos, bool is_wall) :
						position(xpos, ypos), wall(is_wall) {}

					bool operator==(const Cell& c) const {
						return position == c.position;
					}
				};
				struct Direction {
					int dx;
					int dy;
					float cost;
				};

				static std::array<Direction, 8> DIRS;
				int step = 1;
				int width = 0;
				int height = 0;
				std::vector<std::vector<Cell>> cells;
				std::array<Cell*, 8> neighbors = {};
				std::map<float /*cost*/, std::unordered_set <Cell*> > to_visit;
				std::unordered_set<Cell*> visited;
				std::mutex m;

				bool InBounds(const Int2& pos) const;
				int RefreshNeighbors(const Cell* cell, const Cell* target);
				std::list<Int2> ReversePath(const Cell* c, const Int2& offset);
				Cell* GetValidCell(Cell* orig, Cell* dest);
				bool Connected(const Cell* orig, const Cell* dest);

			public:

				SquareGrid();
				void Init(int w, int h, int step_size, const std::vector<std::vector<uint8_t>>& limits);
				//Check if cell is valid
				bool IsValid(Int2 pos, const Int2& offset) const;
				std::list<Int2> GetPath(Int2 orig, Int2 dest, const Int2& offset);
			};
		}
	}
}