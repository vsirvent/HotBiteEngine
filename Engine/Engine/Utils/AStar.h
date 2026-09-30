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
#include <set>
#include <unordered_set>
#include <array>
#include <functional>
#include <list>
#include <mutex>
#include <vector>

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
					//Mutable: a wall provider fills these in the first time a cell is looked at, and
					//that happens from const queries too.
					mutable bool wall = false;
					mutable bool checked = false;

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

				//Cells with equal cost are taken in a fixed order (by position). They used to sit in
				//an unordered_set of pointers, so which of two equal paths A* returned depended on
				//where the heap happened to put the cells - not something two runs, or two peers of a
				//lock-step game, can agree on.
				struct CellLess {
					bool operator()(const Cell* a, const Cell* b) const {
						return a->position.x != b->position.x ? a->position.x < b->position.x : a->position.y < b->position.y;
					}
				};

				static std::array<Direction, 8> DIRS;
				int step = 1;
				int width = 0;
				int height = 0;
				std::vector<std::vector<Cell>> cells;
				std::array<Cell*, 8> neighbors = {};
				std::map<float /*cost*/, std::set<Cell*, CellLess> > to_visit;
				std::unordered_set<Cell*> visited;
				std::mutex m;
				//Optional: decides lazily (once per cell, on first look) whether a cell is a wall, so a
				//big map need not be classified up front.
				std::function<bool(int, int)> wall_provider;
				//Most cells one search may expand (0 = no limit).
				int node_limit = 0;

				bool IsWall(const Cell* cell) const;

				bool InBounds(const Int2& pos) const;
				int RefreshNeighbors(const Cell* cell, const Cell* target);
				std::list<Int2> ReversePath(const Cell* c, const Int2& offset);
				Cell* GetValidCell(Cell* orig, Cell* dest);
				bool Connected(const Cell* orig, const Cell* dest);

			public:

				SquareGrid();
				void Init(int w, int h, int step_size, const std::vector<std::vector<uint8_t>>& limits);
				//After Init: `is_wall(x, y)` (grid coordinates, 0 <= x < width) is asked the first time a
				//cell is looked at and remembered. Replaces the walls Init took from `limits`.
				void SetWallProvider(std::function<bool(int, int)> is_wall);
				//A search that has expanded this many cells gives up and returns the path to the closest
				//cell it reached, so one click on far or unreachable ground cannot stall the game.
				void SetNodeLimit(int max_expanded) { node_limit = max_expanded; }
				int GetWidth() const { return width; }
				int GetHeight() const { return height; }
				//Check if cell is valid
				bool IsValid(Int2 pos, const Int2& offset) const;
				std::list<Int2> GetPath(Int2 orig, Int2 dest, const Int2& offset);
			};
		}
	}
}