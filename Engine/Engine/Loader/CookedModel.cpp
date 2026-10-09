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
#include "CookedModel.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <type_traits>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Loader;
using namespace HotBite::Engine::Core;

namespace {
	// The file starts and ends with these. The tail is what tells a file that was cut
	// short (a crash mid-write, a copy that did not finish) from a complete one.
	constexpr char HEAD_MAGIC[4] = { 'H', 'B', 'C', 'M' };
	constexpr char TAIL_MAGIC[4] = { 'E', 'N', 'D', '!' };

	// Blocks are written as raw bytes. That is only sound for types with no pointers,
	// and only readable by a build that lays them out the same way - which is what the
	// recorded sizes in the header are for.
	static_assert(std::is_trivially_copyable_v<Vertex>, "cooked vertices are written as raw bytes");
	static_assert(std::is_trivially_copyable_v<Keyframe>, "cooked keyframes are written as raw bytes");
	static_assert(std::is_trivially_copyable_v<float3> && std::is_trivially_copyable_v<float4>,
		"cooked vectors are written as raw bytes");

	class Writer {
	public:
		std::string buffer;

		void Bytes(const void* data, size_t size) {
			buffer.append(static_cast<const char*>(data), size);
		}
		template <class T>
		void Pod(const T& value) {
			static_assert(std::is_trivially_copyable_v<T>);
			Bytes(&value, sizeof(T));
		}
		void Str(const std::string& s) {
			Pod<uint32_t>((uint32_t)s.size());
			Bytes(s.data(), s.size());
		}
		template <class T>
		void Block(const std::vector<T>& v) {
			static_assert(std::is_trivially_copyable_v<T>);
			Pod<uint32_t>((uint32_t)v.size());
			Bytes(v.data(), v.size() * sizeof(T));
		}
	};

	// Every read is bounds-checked against the buffer, and a failed one sticks: after
	// the first, everything returns zero and `ok` stays false, so the caller checks
	// once at the end instead of after each field.
	class Reader {
	public:
		const char* data;
		size_t size;
		size_t pos = 0;
		bool ok = true;

		Reader(const char* d, size_t s) : data(d), size(s) {}

		bool Take(void* out, size_t n) {
			if (!ok || n > size - pos) {
				ok = false;
				return false;
			}
			std::memcpy(out, data + pos, n);
			pos += n;
			return true;
		}
		template <class T>
		T Pod() {
			T value{};
			Take(&value, sizeof(T));
			return value;
		}
		std::string Str() {
			const uint32_t n = Pod<uint32_t>();
			if (!ok || n > size - pos) {
				ok = false;
				return std::string();
			}
			std::string s(data + pos, n);
			pos += n;
			return s;
		}
		template <class T>
		void Block(std::vector<T>& v) {
			const uint32_t n = Pod<uint32_t>();
			// A count that cannot fit in what is left is a corrupt file, and must not turn
			// into a huge allocation.
			if (!ok || (uint64_t)n * sizeof(T) > size - pos) {
				ok = false;
				v.clear();
				return;
			}
			v.resize(n);
			Take(v.data(), (size_t)n * sizeof(T));
		}
		// A count of elements that each take at least `min_bytes` in the file.
		uint32_t Count(size_t min_bytes) {
			const uint32_t n = Pod<uint32_t>();
			if (!ok || (uint64_t)n * min_bytes > size - pos) {
				ok = false;
				return 0;
			}
			return n;
		}
	};

	void WriteJoints(Writer& w, const std::vector<JointCpuData>& joints) {
		w.Pod<uint32_t>((uint32_t)joints.size());
		for (const JointCpuData& j : joints) {
			w.Pod<int32_t>(j.joint_id);
			w.Pod<int32_t>(j.id);
			w.Pod<int32_t>(j.parent_id);
			w.Str(j.name);
			w.Pod(j.model_to_bindpose);
			w.Pod<uint32_t>((uint32_t)j.animations.size());
			for (const JointAnim& a : j.animations) {
				w.Str(a.name);
				w.Pod<float>(a.start);
				w.Pod<float>(a.end);
				w.Pod<float>(a.duration);
				w.Pod<uint8_t>(a.loop ? 1 : 0);
				w.Pod<float>(a.fps);
				w.Block(a.key_frames);
			}
		}
	}

	void ReadJoints(Reader& r, std::vector<JointCpuData>& joints) {
		const uint32_t count = r.Count(40);
		joints.clear();
		joints.reserve(count);
		for (uint32_t i = 0; i < count && r.ok; ++i) {
			JointCpuData j;
			j.joint_id = r.Pod<int32_t>();
			j.id = r.Pod<int32_t>();
			j.parent_id = r.Pod<int32_t>();
			j.name = r.Str();
			j.model_to_bindpose = r.Pod<float4x4>();
			const uint32_t animations = r.Count(24);
			j.animations.resize(animations);
			for (uint32_t a = 0; a < animations && r.ok; ++a) {
				JointAnim& anim = j.animations[a];
				anim.name = r.Str();
				anim.start = r.Pod<float>();
				anim.end = r.Pod<float>();
				anim.duration = r.Pod<float>();
				anim.loop = r.Pod<uint8_t>() != 0;
				anim.fps = r.Pod<float>();
				r.Block(anim.key_frames);
			}
			joints.push_back(std::move(j));
		}
	}

	void SetError(std::string* error, const std::string& text) {
		if (error != nullptr) {
			*error = text;
		}
	}
}

std::string CookedModel::PathFor(const std::string& source) {
	return source + EXTENSION;
}

bool CookedModel::StampOf(const std::string& source, uint64_t& size, int64_t& time) {
	std::error_code ec;
	const std::filesystem::path p(source);
	const uintmax_t file_size = std::filesystem::file_size(p, ec);
	if (ec) {
		return false;
	}
	const auto write_time = std::filesystem::last_write_time(p, ec);
	if (ec) {
		return false;
	}
	size = (uint64_t)file_size;
	time = (int64_t)write_time.time_since_epoch().count();
	return true;
}

bool CookedModel::IsCurrentFor(const std::string& source, bool tri, bool anim_names) const {
	if (triangulate != tri || use_animation_names != anim_names) {
		return false;
	}
	uint64_t size = 0;
	int64_t time = 0;
	if (!StampOf(source, size, time)) {
		// No source to compare with: this is a build that ships cooked files only.
		return true;
	}
	return size == source_size && time == source_time;
}

bool CookedModel::Write(const std::string& file, std::string* error) const {
	Writer w;
	w.Bytes(HEAD_MAGIC, sizeof(HEAD_MAGIC));
	w.Pod<uint32_t>(VERSION);
	w.Pod<uint32_t>((uint32_t)sizeof(Vertex));
	w.Pod<uint32_t>((uint32_t)sizeof(Keyframe));
	w.Pod<uint8_t>(triangulate ? 1 : 0);
	w.Pod<uint8_t>(use_animation_names ? 1 : 0);
	w.Pod<uint64_t>(source_size);
	w.Pod<int64_t>(source_time);

	w.Pod<uint32_t>((uint32_t)materials.size());
	for (const CookedMaterial& m : materials) {
		w.Str(m.name);
		w.Pod(m.diffuse_color);
		w.Str(m.diffuse_texture);
	}

	w.Pod<uint32_t>((uint32_t)meshes.size());
	for (const CookedMesh& m : meshes) {
		w.Str(m.name);
		w.Pod<uint8_t>(m.smooth ? 1 : 0);
		w.Block(m.vertices);
		w.Block(m.indices);
		w.Block(m.smooth_groups);
		w.Pod<uint8_t>(m.skinned ? 1 : 0);
		WriteJoints(w, m.joints);
	}

	w.Pod<uint32_t>((uint32_t)shapes.size());
	for (const CookedShape& s : shapes) {
		w.Str(s.name);
		w.Pod(s.authored_scale);
		w.Block(s.vertices);
		w.Block(s.normals);
		w.Block(s.indices);
	}

	w.Pod<uint32_t>((uint32_t)skeletons.size());
	for (const CookedSkeleton& s : skeletons) {
		w.Str(s.name);
		WriteJoints(w, s.joints);
	}

	w.Pod<uint32_t>((uint32_t)nodes.size());
	for (const CookedNode& n : nodes) {
		w.Str(n.name);
		w.Pod<int32_t>(n.parent);
		w.Pod<uint8_t>((uint8_t)n.kind);
		w.Pod(n.position);
		w.Pod(n.scale);
		w.Pod(n.rotation);
		w.Str(n.mesh);
		w.Str(n.material);
		w.Pod(n.light_color);
		w.Pod<float>(n.light_intensity);
	}
	w.Bytes(TAIL_MAGIC, sizeof(TAIL_MAGIC));

	// Written beside the target and renamed over it, so a reader (another process
	// starting at the same time) sees either the old file or the whole new one.
	const std::string temp = file + ".tmp" + std::to_string(GetCurrentProcessId());
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if (!out) {
			SetError(error, "cannot write " + temp);
			return false;
		}
		out.write(w.buffer.data(), (std::streamsize)w.buffer.size());
		out.flush();
		if (!out) {
			SetError(error, "short write to " + temp);
			out.close();
			std::error_code ignore;
			std::filesystem::remove(temp, ignore);
			return false;
		}
	}
	std::error_code ec;
	std::filesystem::rename(temp, file, ec);
	if (ec) {
		std::error_code ignore;
		std::filesystem::remove(temp, ignore);
		SetError(error, "cannot replace " + file + ": " + ec.message());
		return false;
	}
	return true;
}

bool CookedModel::Read(const std::string& file, CookedModel& out, std::string* error) {
	std::ifstream in(file, std::ios::binary | std::ios::ate);
	if (!in) {
		SetError(error, "cannot open " + file);
		return false;
	}
	const std::streamsize length = in.tellg();
	if (length < (std::streamsize)(sizeof(HEAD_MAGIC) + sizeof(TAIL_MAGIC))) {
		SetError(error, "too short");
		return false;
	}
	in.seekg(0);
	std::string content((size_t)length, '\0');
	if (!in.read(content.data(), length)) {
		SetError(error, "cannot read " + file);
		return false;
	}

	Reader r(content.data(), content.size());
	char head[4] = {};
	r.Take(head, sizeof(head));
	if (std::memcmp(head, HEAD_MAGIC, sizeof(head)) != 0) {
		SetError(error, "not a cooked model");
		return false;
	}
	if (r.Pod<uint32_t>() != VERSION) {
		SetError(error, "cook version differs");
		return false;
	}
	if (r.Pod<uint32_t>() != (uint32_t)sizeof(Vertex) || r.Pod<uint32_t>() != (uint32_t)sizeof(Keyframe)) {
		SetError(error, "vertex layout differs");
		return false;
	}

	CookedModel m;
	m.triangulate = r.Pod<uint8_t>() != 0;
	m.use_animation_names = r.Pod<uint8_t>() != 0;
	m.source_size = r.Pod<uint64_t>();
	m.source_time = r.Pod<int64_t>();

	const uint32_t material_count = r.Count(24);
	m.materials.resize(material_count);
	for (uint32_t i = 0; i < material_count && r.ok; ++i) {
		CookedMaterial& c = m.materials[i];
		c.name = r.Str();
		c.diffuse_color = r.Pod<float4>();
		c.diffuse_texture = r.Str();
	}

	const uint32_t mesh_count = r.Count(20);
	m.meshes.resize(mesh_count);
	for (uint32_t i = 0; i < mesh_count && r.ok; ++i) {
		CookedMesh& c = m.meshes[i];
		c.name = r.Str();
		c.smooth = r.Pod<uint8_t>() != 0;
		r.Block(c.vertices);
		r.Block(c.indices);
		r.Block(c.smooth_groups);
		c.skinned = r.Pod<uint8_t>() != 0;
		ReadJoints(r, c.joints);
	}

	const uint32_t shape_count = r.Count(24);
	m.shapes.resize(shape_count);
	for (uint32_t i = 0; i < shape_count && r.ok; ++i) {
		CookedShape& c = m.shapes[i];
		c.name = r.Str();
		c.authored_scale = r.Pod<float3>();
		r.Block(c.vertices);
		r.Block(c.normals);
		r.Block(c.indices);
	}

	const uint32_t skeleton_count = r.Count(8);
	m.skeletons.resize(skeleton_count);
	for (uint32_t i = 0; i < skeleton_count && r.ok; ++i) {
		CookedSkeleton& c = m.skeletons[i];
		c.name = r.Str();
		ReadJoints(r, c.joints);
	}

	const uint32_t node_count = r.Count(60);
	m.nodes.resize(node_count);
	for (uint32_t i = 0; i < node_count && r.ok; ++i) {
		CookedNode& c = m.nodes[i];
		c.name = r.Str();
		c.parent = r.Pod<int32_t>();
		c.kind = (CookedNode::Kind)r.Pod<uint8_t>();
		c.position = r.Pod<float3>();
		c.scale = r.Pod<float3>();
		c.rotation = r.Pod<float4>();
		c.mesh = r.Str();
		c.material = r.Str();
		c.light_color = r.Pod<float3>();
		c.light_intensity = r.Pod<float>();
		if (c.parent >= (int32_t)i) {
			r.ok = false;
		}
	}

	char tail[4] = {};
	r.Take(tail, sizeof(tail));
	if (!r.ok || std::memcmp(tail, TAIL_MAGIC, sizeof(tail)) != 0 || r.pos != r.size) {
		SetError(error, "truncated or corrupt");
		return false;
	}
	// A mesh that names vertices it does not have would be read past its end later.
	for (const CookedMesh& c : m.meshes) {
		if (!c.smooth_groups.empty() && c.smooth_groups.size() != c.vertices.size()) {
			SetError(error, "corrupt mesh " + c.name);
			return false;
		}
		for (uint32_t index : c.indices) {
			if (index >= c.vertices.size()) {
				SetError(error, "corrupt mesh " + c.name);
				return false;
			}
		}
	}
	out = std::move(m);
	return true;
}
