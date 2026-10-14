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

ShadowRays::ShadowRays() : tbvh(MAX_SHADOW_OBJECTS) {
}

ShadowRays::~ShadowRays() {
	Release();
}

bool ShadowRays::Init() {
	Release();
	if (FAILED(tlas_buffer.Prepare(MAX_SHADOW_OBJECTS * 2 - 1)) ||
		FAILED(object_buffer.Prepare(MAX_SHADOW_OBJECTS))) {
		Release();
		return false;
	}
	ready = true;
	return true;
}

void ShadowRays::Release() {
	tlas_buffer.Unprepare();
	object_buffer.Unprepare();
	count = 0;
	ready = false;
}

void ShadowRays::Load(const ObjectInfo* objects, uint32_t n) {
	count = 0;
	if (!ready || n == 0) {
		return;
	}
	count = n < MAX_SHADOW_OBJECTS ? n : MAX_SHADOW_OBJECTS;
	tbvh.Load(objects, (int)count);
	tlas_buffer.Refresh(tbvh.Root(), 0, tbvh.Size());
	object_buffer.Refresh(objects, 0, count);
}
