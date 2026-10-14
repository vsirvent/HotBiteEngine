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

#include "ShadowRays.h"

using namespace HotBite::Engine::Core;

ShadowRays::ShadowRays() : tbvh(MAX_SCENE_OBJECTS) {
}

ShadowRays::~ShadowRays() {
	Release();
}

bool ShadowRays::Init() {
	Release();
	if (FAILED(tlas_buffer.Prepare(MAX_FRAME_NODES)) ||
		FAILED(object_buffer.Prepare(MAX_FRAME_OBJECTS)) ||
		FAILED(scene_buffer.Prepare(MAX_SCENES))) {
		Release();
		return false;
	}
	nodes.reserve(MAX_FRAME_NODES);
	objects.reserve(MAX_FRAME_OBJECTS);
	scenes.reserve(MAX_SCENES);
	ready = true;
	return true;
}

void ShadowRays::Release() {
	tlas_buffer.Unprepare();
	object_buffer.Unprepare();
	scene_buffer.Unprepare();
	Begin();
	ready = false;
}

void ShadowRays::Begin() {
	nodes.clear();
	objects.clear();
	scenes.clear();
	dropped = 0;
}

uint32_t ShadowRays::AddScene(const ObjectInfo* in, uint32_t n) {
	if (!ready || n == 0) {
		return NO_SCENE;
	}
	if (n > MAX_SCENE_OBJECTS) {
		n = MAX_SCENE_OBJECTS;
	}
	//A hierarchy over n objects has 2n - 1 nodes at most.
	if (scenes.size() >= MAX_SCENES || objects.size() + n > MAX_FRAME_OBJECTS || nodes.size() + 2 * n - 1 > MAX_FRAME_NODES) {
		++dropped;
		return NO_SCENE;
	}
	tbvh.Load(in, (int)n);
	ShadowScene scene;
	scene.node_offset = (uint32_t)nodes.size();
	scene.object_offset = (uint32_t)objects.size();
	scene.object_count = n;
	//The hierarchy's links are local to it and its leaves name an object by its place in `in`; the
	//shader adds the two offsets.
	nodes.insert(nodes.end(), tbvh.Root(), tbvh.Root() + tbvh.Size());
	objects.insert(objects.end(), in, in + n);
	scenes.push_back(scene);
	return (uint32_t)scenes.size() - 1;
}

void ShadowRays::Upload() {
	if (!ready || scenes.empty()) {
		return;
	}
	tlas_buffer.Refresh(nodes.data(), 0, (uint32_t)nodes.size());
	object_buffer.Refresh(objects.data(), 0, (uint32_t)objects.size());
	scene_buffer.Refresh(scenes.data(), 0, (uint32_t)scenes.size());
}