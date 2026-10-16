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

#include "AccelStructure.h"
#include "DXCore.h"
#include "GfxContext.h"
#include "Log.h"

#include <algorithm>
#include <chrono>

using namespace HotBite::Engine::Core;

namespace {
	//Bytes of one vertex of Core::Vertex, whose first member is the position.
	constexpr UINT64 VERTEX_STRIDE = 96;
	//Where an acceleration structure and the memory of its builds must start.
	constexpr UINT64 AS_ALIGNMENT = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
	//(The scratch memory of a build starts on the same boundary: 256 bytes.)
	constexpr UINT64 SCRATCH_ALIGNMENT = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
	//A top level is made for at least this many instances, and for a power of two of them, so that its size does not follow
	//the count from one frame to the next.
	constexpr uint32_t MIN_TOP_CAPACITY = 256;

	UINT64 AlignUp(UINT64 value, UINT64 alignment) {
		return (value + alignment - 1) & ~(alignment - 1);
	}

	uint32_t NextPowerOfTwo(uint32_t value) {
		uint32_t p = 1;
		while (p < value) {
			p <<= 1;
		}
		return p;
	}
}

AccelStructure::~AccelStructure() {
	Release();
}

bool AccelStructure::Init() {
	Release();
	GfxDevice* gfx = DXCore::Get()->device;
	supported = gfx != nullptr && gfx->RayQuerySupported();
	return supported;
}

void AccelStructure::Reset() {
	for (auto& entry : bottoms) {
		if (entry.second.buffer != nullptr) {
			entry.second.buffer->Release();
		}
	}
	bottoms.clear();
	geometries = 0;
	geometry_bytes = 0;
	for (TopSet& set : sets) {
		set.ready = false;
		set.stats = Stats{};
	}
}

void AccelStructure::ReleaseTop(TopLevel& top) {
	if (top.srv != nullptr) {
		top.srv->Release();
		top.srv = nullptr;
	}
	if (top.buffer != nullptr) {
		top.buffer->Release();
		top.buffer = nullptr;
	}
	top.capacity = 0;
}

void AccelStructure::Release() {
	Reset();
	for (TopSet& set : sets) {
		for (TopLevel& top : set.tops) {
			ReleaseTop(top);
		}
		set.tops.clear();
		set.current_srv = nullptr;
	}
	if (scratch != nullptr) {
		scratch->Release();
		scratch = nullptr;
	}
	supported = false;
}

void AccelStructure::MakeGeometry(const Key& key, GfxBuffer* vertex, GfxBuffer* index, D3D12_RAYTRACING_GEOMETRY_DESC& out) const {
	out = {};
	out.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	//Opaque: the first triangle on the way is the answer, nothing is asked of a shader about it.
	out.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
	out.Triangles.Transform3x4 = 0;
	out.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
	out.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	out.Triangles.IndexCount = key.index_count;
	out.Triangles.VertexCount = key.vertex_count;
	out.Triangles.IndexBuffer = index->GpuAddress() + (UINT64)key.index_offset * sizeof(uint32_t);
	//The indices of a mesh count from its own first vertex.
	out.Triangles.VertexBuffer.StartAddress = vertex->GpuAddress() + (UINT64)key.vertex_offset * VERTEX_STRIDE;
	out.Triangles.VertexBuffer.StrideInBytes = VERTEX_STRIDE;
}

bool AccelStructure::CreateBottom(const Key& key, GfxBuffer* vertex, GfxBuffer* index, Bottom& bottom) {
	GfxDevice* gfx = DXCore::Get()->device;
	D3D12_RAYTRACING_GEOMETRY_DESC geometry;
	MakeGeometry(key, vertex, index, geometry);
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	//Built once and traced many times a frame.
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.NumDescs = 1;
	inputs.pGeometryDescs = &geometry;
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
	gfx->Device5()->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
	if (info.ResultDataMaxSizeInBytes == 0) {
		return false;
	}
	if (FAILED(gfx->CreateAccelerationBuffer(AlignUp(info.ResultDataMaxSizeInBytes, AS_ALIGNMENT), false, &bottom.buffer))) {
		bottom.buffer = nullptr;
		return false;
	}
	bottom.address = bottom.buffer->GpuAddress();
	bottom.scratch_size = AlignUp(info.ScratchDataSizeInBytes, SCRATCH_ALIGNMENT);
	geometry_bytes += bottom.buffer->size;
	return true;
}

AccelStructure::TopLevel* AccelStructure::AcquireTop(TopSet& set, uint32_t count) {
	GfxDevice* gfx = DXCore::Get()->device;
	const uint32_t capacity = NextPowerOfTwo((std::max)(count, MIN_TOP_CAPACITY));
	//A top level the GPU has finished with, big enough; else one it has finished with that is made bigger; else one more.
	TopLevel* free_small = nullptr;
	for (TopLevel& top : set.tops) {
		if (!gfx->IsFrameComplete(top.serial)) {
			continue;
		}
		if (top.capacity >= capacity) {
			return &top;
		}
		free_small = &top;
	}
	if (free_small == nullptr) {
		set.tops.emplace_back();
		free_small = &set.tops.back();
	}
	TopLevel& top = *free_small;
	ReleaseTop(top);

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.NumDescs = capacity;
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
	gfx->Device5()->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
	if (info.ResultDataMaxSizeInBytes == 0 ||
		FAILED(gfx->CreateAccelerationBuffer(AlignUp(info.ResultDataMaxSizeInBytes, AS_ALIGNMENT), false, &top.buffer))) {
		top.buffer = nullptr;
		return nullptr;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
	view.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	view.RaytracingAccelerationStructure.Location = top.buffer->GpuAddress();
	if (FAILED(gfx->CreateSRV(top.buffer, &view, &top.srv))) {
		top.srv = nullptr;
		ReleaseTop(top);
		return nullptr;
	}
	top.capacity = capacity;
	top.scratch_size = AlignUp(info.ScratchDataSizeInBytes, SCRATCH_ALIGNMENT);
	top.serial = 0;
	return &top;
}

bool AccelStructure::EnsureScratch(UINT64 bytes) {
	if (scratch != nullptr && scratch->size >= bytes) {
		return true;
	}
	GfxDevice* gfx = DXCore::Get()->device;
	//The memory of builds already recorded may still be in use: the old buffer is released (destroyed once the GPU is done).
	if (scratch != nullptr) {
		scratch->Release();
		scratch = nullptr;
	}
	UINT64 size = 4ull * 1024 * 1024;
	while (size < bytes) {
		size <<= 1;
	}
	if (FAILED(gfx->CreateAccelerationBuffer(size, true, &scratch))) {
		scratch = nullptr;
		return false;
	}
	return true;
}

bool AccelStructure::Build(GfxBuffer* vertex, GfxBuffer* index, const Instance* instances, uint32_t count, Top which) {
	const auto started = std::chrono::steady_clock::now();
	TopSet& set = sets[which];
	Stats& stats = set.stats;
	set.ready = false;
	stats.instances = 0;
	stats.skipped = 0;
	stats.built = 0;
	stats.top_bytes = 0;
	if (!supported || vertex == nullptr || index == nullptr || instances == nullptr || count == 0) {
		return false;
	}
	if (count > MAX_INSTANCES) {
		stats.skipped += count - MAX_INSTANCES;
		count = MAX_INSTANCES;
	}
	DXCore* dxcore = DXCore::Get();
	GfxDevice* gfx = dxcore->device;
	GfxContext* context = dxcore->context;
	const UINT64 vertex_capacity = vertex->size / VERTEX_STRIDE;
	const UINT64 index_capacity = index->size / sizeof(uint32_t);

	//The bottom level of every geometry the instances stand on; the ones that do not exist yet are made and queued for a build.
	pending.clear();
	resolved.clear();
	resolved_instance.clear();
	UINT64 scratch_needed = 0;
	for (uint32_t i = 0; i < count; ++i) {
		const Instance& in = instances[i];
		if (in.index_count < 3 || (in.index_count % 3) != 0 || in.vertex_count == 0 ||
			(UINT64)in.vertex_offset + in.vertex_count > vertex_capacity || (UINT64)in.index_offset + in.index_count > index_capacity) {
			++stats.skipped;
			continue;
		}
		const Key key = { in.vertex_offset, in.vertex_count, in.index_offset, in.index_count };
		auto it = bottoms.find(key);
		if (it == bottoms.end()) {
			it = bottoms.emplace(key, Bottom{}).first;
			it->second.failed = !CreateBottom(key, vertex, index, it->second);
		}
		Bottom& bottom = it->second;
		if (bottom.failed) {
			++stats.skipped;
			continue;
		}
		if (!bottom.built && !bottom.queued) {
			if (scratch_needed + bottom.scratch_size > SCRATCH_BUDGET) {
				//Next frame's: this one has spent what it may.
				++stats.skipped;
				continue;
			}
			bottom.queued = true;
			scratch_needed += bottom.scratch_size;
			pending.push_back({ &it->first, &bottom });
		}
		//(A bottom level queued by an earlier instance of this call is built before the top level is.)
		resolved.push_back(&bottom);
		resolved_instance.push_back(i);
	}
	geometries = (uint32_t)bottoms.size();
	stats.geometries = geometries;
	if (resolved.empty()) {
		return false;
	}

	TopLevel* top = AcquireTop(set, (uint32_t)resolved.size());
	if (top == nullptr || !EnsureScratch(scratch_needed + top->scratch_size)) {
		return false;
	}
	stats.scratch_bytes = scratch->size;
	stats.geometry_bytes = geometry_bytes;
	stats.top_bytes = top->buffer->size;

	//The instances go into the frame's upload ring: the build reads them from there.
	const UINT64 instance_bytes = (UINT64)resolved.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
	const GfxContext::RingAllocation instance_memory = context->AllocateUpload(instance_bytes, 256);
	if (instance_memory.cpu == nullptr) {
		return false;
	}
	D3D12_RAYTRACING_INSTANCE_DESC* descs = (D3D12_RAYTRACING_INSTANCE_DESC*)instance_memory.cpu;
	for (size_t k = 0; k < resolved.size(); ++k) {
		D3D12_RAYTRACING_INSTANCE_DESC& d = descs[k];
		memset(&d, 0, sizeof(d));
		//world_matrix has the translation in its last column: its first three rows are the 3x4 the structure takes.
		static_assert(sizeof(float4x4) == 16 * sizeof(float), "a row of world_matrix is four floats");
		memcpy(d.Transform, &instances[resolved_instance[k]].world, 12 * sizeof(float));
		//The place in the list that was handed in: the shader finds what the instance is by it.
		d.InstanceID = (UINT)resolved_instance[k];
		d.InstanceMask = 0xFF;
		d.InstanceContributionToHitGroupIndex = 0;
		//The shadows only ask whether something is in the way. The world's triangles are not opaque, which hands each one the ray meets to
		//the shader, and a shader that wants the nearest hit anyway says so with a ray flag (RAY_FLAG_FORCE_OPAQUE).
		d.Flags = (which == SHADOWS) ? D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE : D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
		d.AccelerationStructure = resolved[k]->address;
	}

	//The shared buffers are read by the builds (they rest in a state that includes the one that needs).
	context->Transition(vertex, GFX_STATE_BUFFER_READ);
	context->Transition(index, GFX_STATE_BUFFER_READ);
	ID3D12GraphicsCommandList* list = context->BeginExternal();
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> list4;
	if (FAILED(list->QueryInterface(IID_PPV_ARGS(&list4)))) {
		context->EndExternal();
		return false;
	}

	//The scratch memory may be in use by the previous frame's builds: they finish before these start.
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.UAV.pResource = scratch->Get();
	list->ResourceBarrier(1, &barrier);

	UINT64 scratch_offset = 0;
	for (const Pending& p : pending) {
		D3D12_RAYTRACING_GEOMETRY_DESC geometry;
		MakeGeometry(*p.key, vertex, index, geometry);
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
		build.DestAccelerationStructureData = p.bottom->address;
		build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		build.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		build.Inputs.NumDescs = 1;
		build.Inputs.pGeometryDescs = &geometry;
		build.ScratchAccelerationStructureData = scratch->GpuAddress() + scratch_offset;
		list4->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
		scratch_offset += p.bottom->scratch_size;
		p.bottom->built = true;
		p.bottom->queued = false;
		++stats.built;
	}
	if (!pending.empty()) {
		//The bottom levels are complete before the top level reads them.
		D3D12_RESOURCE_BARRIER all = {};
		all.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		all.UAV.pResource = nullptr;
		list->ResourceBarrier(1, &all);
	}

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
	build.DestAccelerationStructureData = top->buffer->GpuAddress();
	build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	build.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	build.Inputs.NumDescs = (UINT)resolved.size();
	build.Inputs.InstanceDescs = instance_memory.gpu;
	build.ScratchAccelerationStructureData = scratch->GpuAddress() + scratch_offset;
	list4->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
	//What the shaders that follow trace is complete.
	D3D12_RESOURCE_BARRIER done = {};
	done.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	done.UAV.pResource = top->buffer->Get();
	list->ResourceBarrier(1, &done);
	context->EndExternal();

	top->serial = gfx->FrameSerial();
	set.current_srv = top->srv;
	stats.instances = (uint32_t)resolved.size();
	set.ready = true;
	stats.cpu_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	return true;
}
