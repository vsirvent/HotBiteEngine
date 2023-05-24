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

#include "AStar.h"

namespace HotBite {
	namespace Engine {
		namespace Core {

			std::array<SquareGrid::Direction, 8> SquareGrid::DIRS = {
				Direction{-1,  1, 1.4f}, Direction{0, 1, 1.0f}, Direction{1,  1, 1.4f},
				Direction{-1,  0, 1.0f},                        Direction{1,  0, 1.0f},
				Direction{-1, -1, 1.4f}, Direction{0,-1, 1.0f}, Direction{1, -1, 1.4f}
			};

			bool SquareGrid::InBounds(const Int2& pos) const {
				return (0 <= pos.x && pos.x < width
					&& 0 <= pos.y && pos.y < height);
			}

			int SquareGrid::RefreshNeighbors(const Cell* cell, const Cell* target) {
				int n = 0;
				neighbors.fill(nullptr);
				for (auto dir : DIRS) {
					//Move "step" units per step, except if we are close to target, minimum is 1 unit
					int unit = max(min(step, (int)Int2::Dist(cell->position, target->position)), 1);
					dir.cost *= unit;
					dir.dx *= unit;
					dir.dy *= unit;
					Int2 next_pos(cell->position.x + dir.dx, cell->position.y + dir.dy);
					if (InBounds(next_pos)) {
						Cell* next = &(cells[cell->position.x + dir.dx][cell->position.y + dir.dy]);
						bool process = true;
						float cost = cell->cost + dir.cost;
						auto v = visited.find(next);
						if (v != visited.end()) {
							if ((*v)->cost <= cost) {
								process = false;
							}
						}
						if (process) {
							if (!next->wall) {
								next->parent = cell;
								next->cost = cost;
								next->to_target = Int2::Dist(next->position, target->position);
								neighbors[n++] = next;
							}
						}
					}
				}
				return n;
			}

			bool SquareGrid::Connected(const Cell* orig, const Cell* dest) {
				if (orig == dest) {
					return true;
				}
				bool connected = true;
				float2 dest_pos = { (float)dest->position.x, (float)dest->position.y };
				float2 dir = (dest->position - orig->position);
				if (LENGHT_F2(dir) > 1.0f) {
					dir = UNIT_F2(dir);
					float2 pos = orig->position;
					while (true) {
						pos = ADD_F2_F2(pos, dir);
						if (LENGHT_SQUARE_F2(pos - dest_pos) <= 1.5f) {
							break;
						}
						if (cells[(int)pos.x][(int)pos.y].wall) {
							connected = false;
							break;
						}
					}
				}
				return connected;
			}

			std::list<Int2> SquareGrid::ReversePath(const Cell* c, const Int2& offset) {
				std::list<Int2> ret;
				const Cell* current = c;
				std::map<float, const Cell*> path;
				while (current->parent != nullptr) {
					path[current->cost] = current;
					current = current->parent;
				}
				path[current->cost] = current;
				//Now that we have the path ordered by cost, let's smooth it
				auto it = path.begin();
				ret.push_back({ it->second->position.x - offset.x,  it->second->position.y - offset.y });
				for (auto it2 = path.rbegin();
					it2 != path.rend() &&
					it != path.end() &&
					it->second != path.rbegin()->second; ++it2) {

					if (Connected(it->second, it2->second)) {
						ret.push_back({ it2->second->position.x - offset.x,  it2->second->position.y - offset.y });
						if (it->second != it2->second) {
							it = path.find(it2->first);
						}
						else {
							it++;
						}
						it2 = path.rbegin();
					}
				}
				return ret;
			}

			SquareGrid::SquareGrid() {}

			bool SquareGrid::IsValid(Int2 pos, const Int2& offset) const {
				bool valid = false;
				pos.x += offset.x;
				pos.y += offset.y;
				if (InBounds(pos)) {
					valid = !cells[pos.x][pos.y].wall;
				}
				return valid;
			}

			void SquareGrid::Init(int w, int h, int step_size, const std::vector<std::vector<uint8_t>>& limits) {
				width = w;
				height = h;
				step = step_size;
				cells.resize(width);
				to_visit.clear();
				visited.clear();
				float lx = 0.0f;
				float ly = 0.0f;
				if (!limits.empty()) {
					lx = (float)limits.size();
					ly = (float)limits[0].size();
				}
				for (int x = 0; x < width; ++x) {
					cells[x].resize(height);
					int tx = -1;
					if (lx > 0.0f) {
						float fx = (float)x;
						tx = (int)(((width - fx) * lx) / width);
					}
					for (int y = 0; y < height; ++y) {
						bool is_wall = false;
						if (ly > 0.0f) {
							float fy = (float)y;
							int ty = (int)((fy * ly) / height);
							if (tx >= 0 && tx < lx &&
								ty >= 0 && ty < ly) {
								is_wall = (limits[(int)tx][(int)ty] == 0);
							}
						}
						cells[x][y] = Cell(x, y, is_wall);
					}
				}
#if 0
				//Print the wall map, to make sure it's loaded correctly...
				//widht, height orientation is tricky
				for (int y = 0; y < height; ++y) {
					printf("%d:", y);
					for (int x = 0; x < width; x += 7) {
						printf("%c", cells[x][y].wall ? 'X' : '_');
					}
					printf("\n");
				}
#endif
			}

			SquareGrid::Cell* SquareGrid::GetValidCell(Cell* orig, Cell* dest) {
				Cell* target = dest;
				float2 dir = (dest->position - orig->position);
				float2 dir_unit = DIV_F2(dir, LENGHT_F2(dir));
				float2 p = dest->position;
				while (target != orig && target->wall) {
					p.x -= dir_unit.x;
					p.y -= dir_unit.y;
					if (InBounds(p)) {
						target = &cells[(int)p.x][(int)p.y];
					}
					else {
						break;
					}
				}
				return target;
			}

			std::list<Int2> SquareGrid::GetPath(Int2 orig, Int2 dest, const Int2& offset) {
				std::list<Int2> ret;
				m.lock();
				to_visit.clear();
				visited.clear();
				orig.x += offset.x;
				orig.y += offset.y;
				dest.x += offset.x;
				dest.y += offset.y;
				if (InBounds(orig) && InBounds(dest)) {
					Cell* target_cell = &cells[dest.x][dest.y];
					Cell* origin_cell = &cells[orig.x][orig.y];
					Cell* current = nullptr;
					printf("SquareGrid::Calculate path from %d,%d to %d,%d\n", orig.x, orig.y, dest.x, dest.y);
					if (target_cell->wall) {
						//Get closest valid cell as target
						printf("SquareGrid::Target is a WALL, find near valid target\n");
						target_cell = GetValidCell(origin_cell, target_cell);
					}
					origin_cell->parent = nullptr;
					origin_cell->cost = 0;
					if (origin_cell == target_cell) {
						ret = ReversePath(origin_cell, offset);
						goto end;
					}
					to_visit[0].insert(origin_cell);
					while (!to_visit.empty()) {

						current = *(to_visit.begin()->second.begin());
						to_visit.begin()->second.erase(to_visit.begin()->second.begin());

						if (to_visit.begin()->second.empty()) {
							to_visit.erase(to_visit.begin());
						}

						int n = RefreshNeighbors(current, target_cell);

						visited.insert(current);
						for (int i = 0; i < n; ++i) {
							Cell* cn = neighbors[i];
							if (cn == target_cell) {
								ret = ReversePath(cn, offset);
								goto end;
							}
							else if (cn != nullptr) {
								to_visit[cn->cost + cn->to_target].insert(cn);
							}
						}
					}
					//We did not find a path to target, 
					//so make a path to the closest target found
					if (current != nullptr) {
						ret = ReversePath(current, offset);
					}
				}
			end:
				m.unlock();
				return ret;

			}
		}
	}
}