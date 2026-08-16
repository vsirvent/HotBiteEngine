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

#include "SplatCloud.h"
#include "Log.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

using namespace HotBite::Engine::Core;

namespace {

	//The SH degree-0 basis function. A .ply's f_dc_* are coefficients against it, so
	//the colour they encode is 0.5 + C0 * f_dc - the 0.5 being the DC offset the
	//reference implementation trains around.
	constexpr float SH_C0 = 0.28209479177387814f;

	//How many standard deviations of a Gaussian the bounds should contain. Past 3
	//sigma a Gaussian contributes under 1% and the rasterizer's own alpha cutoff
	//removes it, so this is where the cloud visually ends.
	constexpr float BOUNDS_SIGMA = 3.0f;

	struct PlyProperty {
		std::string name;
		//Size in bytes of the scalar. Only float/double/int-family appear in the splat
		//layout, but the header is parsed generically so an unexpected property can be
		//skipped by width rather than aborting the load.
		uint32_t size = 0;
		bool is_float = false;
		bool is_double = false;
	};

	uint32_t PlyTypeSize(const std::string& type, bool& is_float, bool& is_double) {
		is_float = false;
		is_double = false;
		if (type == "float" || type == "float32") { is_float = true; return 4; }
		if (type == "double" || type == "float64") { is_double = true; return 8; }
		if (type == "char" || type == "int8" || type == "uchar" || type == "uint8") { return 1; }
		if (type == "short" || type == "int16" || type == "ushort" || type == "uint16") { return 2; }
		if (type == "int" || type == "int32" || type == "uint" || type == "uint32") { return 4; }
		return 0;
	}

	float Sigmoid(float x) {
		return 1.0f / (1.0f + expf(-x));
	}
}

SplatCloudData::~SplatCloudData() {
	Unprepare();
}

bool SplatCloudData::Load(const std::string& file, const std::string& asset_name) {
	std::ifstream in(file, std::ios::binary);
	if (!in.is_open()) {
		LOG_ERROR("SplatCloudData::Load: cannot open %s", file.c_str());
		return false;
	}

	std::string line;
	if (!std::getline(in, line) || line.rfind("ply", 0) != 0) {
		LOG_ERROR("SplatCloudData::Load: %s is not a .ply", file.c_str());
		return false;
	}

	bool binary_le = false;
	uint64_t vertex_count = 0;
	std::vector<PlyProperty> props;
	//Properties are only counted for the *vertex* element. A .ply may declare a face
	//element after it, whose properties are lists and must not be added to the
	//per-vertex stride.
	bool in_vertex_element = false;

	while (std::getline(in, line)) {
		//Headers are ASCII but the file may have been written on a platform that used
		//CRLF, and the trailing \r would otherwise end up inside a property name.
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
			line.pop_back();
		}
		std::istringstream ss(line);
		std::string tok;
		ss >> tok;

		if (tok == "format") {
			std::string fmt;
			ss >> fmt;
			if (fmt == "binary_little_endian") {
				binary_le = true;
			}
			else {
				LOG_ERROR("SplatCloudData::Load: %s is '%s'; only binary_little_endian is supported", file.c_str(), fmt.c_str());
				return false;
			}
		}
		else if (tok == "element") {
			std::string ename;
			ss >> ename;
			in_vertex_element = (ename == "vertex");
			if (in_vertex_element) {
				ss >> vertex_count;
			}
		}
		else if (tok == "property" && in_vertex_element) {
			std::string type;
			ss >> type;
			if (type == "list") {
				LOG_ERROR("SplatCloudData::Load: %s has a list property on the vertex element", file.c_str());
				return false;
			}
			PlyProperty p;
			p.size = PlyTypeSize(type, p.is_float, p.is_double);
			ss >> p.name;
			if (p.size == 0) {
				LOG_ERROR("SplatCloudData::Load: unknown property type '%s' in %s", type.c_str(), file.c_str());
				return false;
			}
			props.push_back(p);
		}
		else if (tok == "end_header") {
			break;
		}
	}

	if (!binary_le || vertex_count == 0 || props.empty()) {
		LOG_ERROR("SplatCloudData::Load: %s has no usable vertex element", file.c_str());
		return false;
	}

	//Index the properties by name once, so the per-splat loop is offset arithmetic
	//rather than a string compare per field per splat.
	std::unordered_map<std::string, uint32_t> offset_of;
	std::unordered_map<std::string, const PlyProperty*> prop_of;
	uint32_t stride = 0;
	for (const auto& p : props) {
		offset_of[p.name] = stride;
		prop_of[p.name] = &p;
		stride += p.size;
	}

	auto has = [&](const char* n) { return offset_of.find(n) != offset_of.end(); };
	//The minimum a splat needs. Normals are deliberately not required - see the header.
	const char* required[] = { "x", "y", "z", "opacity",
							   "scale_0", "scale_1", "scale_2",
							   "rot_0", "rot_1", "rot_2", "rot_3",
							   "f_dc_0", "f_dc_1", "f_dc_2" };
	for (const char* r : required) {
		if (!has(r)) {
			LOG_ERROR("SplatCloudData::Load: %s is missing required property '%s'; is it a 3DGS .ply?", file.c_str(), r);
			return false;
		}
	}

	std::vector<uint8_t> raw(stride * (size_t)vertex_count);
	in.read((char*)raw.data(), (std::streamsize)raw.size());
	if ((uint64_t)in.gcount() != (uint64_t)raw.size()) {
		LOG_ERROR("SplatCloudData::Load: %s is truncated (wanted %llu bytes, got %lld)",
			file.c_str(), (unsigned long long)raw.size(), (long long)in.gcount());
		return false;
	}
	in.close();

	//Reads one scalar property as a float, widening whatever the file stored it as.
	auto read_f = [&](const uint8_t* base, const char* name) -> float {
		auto it = offset_of.find(name);
		if (it == offset_of.end()) { return 0.0f; }
		const PlyProperty* p = prop_of[name];
		const uint8_t* at = base + it->second;
		if (p->is_float) { float v; memcpy(&v, at, 4); return v; }
		if (p->is_double) { double v; memcpy(&v, at, 8); return (float)v; }
		//Integer-typed. Only ever seen on nx/ny/nz in odd exports, and those are
		//discarded anyway, so a plain widening is enough.
		switch (p->size) {
		case 1: return (float)*at;
		case 2: { uint16_t v; memcpy(&v, at, 2); return (float)v; }
		case 4: { uint32_t v; memcpy(&v, at, 4); return (float)v; }
		default: return 0.0f;
		}
	};

	splats.clear();
	splats.reserve((size_t)vertex_count);

	//First pass builds the splats and accumulates the centroid, which the normal's
	//sign resolution needs and which is therefore not known until every centre is read.
	double cx = 0.0, cy = 0.0, cz = 0.0;
	//The un-oriented normal per splat, filled alongside so the second pass only has to
	//decide a sign.
	std::vector<float3> minor_axis;
	minor_axis.reserve((size_t)vertex_count);

	for (uint64_t i = 0; i < vertex_count; ++i) {
		const uint8_t* base = raw.data() + (size_t)i * stride;

		SplatVertex s;
		s.position = { read_f(base, "x"), read_f(base, "y"), read_f(base, "z") };
		s.opacity = Sigmoid(read_f(base, "opacity"));

		//The file stores log-scales; exponentiate to get world units.
		float sx = expf(read_f(base, "scale_0"));
		float sy = expf(read_f(base, "scale_1"));
		float sz = expf(read_f(base, "scale_2"));

		//3DGS stores the rotation as (w, x, y, z) and does not guarantee it normalized.
		float qw = read_f(base, "rot_0");
		float qx = read_f(base, "rot_1");
		float qy = read_f(base, "rot_2");
		float qz = read_f(base, "rot_3");
		float qlen = sqrtf(qw * qw + qx * qx + qy * qy + qz * qz);
		if (qlen > 1e-8f) { qw /= qlen; qx /= qlen; qy /= qlen; qz /= qlen; }
		else { qw = 1.0f; qx = qy = qz = 0.0f; }

		//Rotation matrix, column-major as three basis vectors: R = [r0 r1 r2]. Each
		//column is the world direction of one of the ellipsoid's local axes, which is
		//exactly what the normal derivation below needs.
		const float r00 = 1.0f - 2.0f * (qy * qy + qz * qz);
		const float r01 = 2.0f * (qx * qy - qw * qz);
		const float r02 = 2.0f * (qx * qz + qw * qy);
		const float r10 = 2.0f * (qx * qy + qw * qz);
		const float r11 = 1.0f - 2.0f * (qx * qx + qz * qz);
		const float r12 = 2.0f * (qy * qz - qw * qx);
		const float r20 = 2.0f * (qx * qz - qw * qy);
		const float r21 = 2.0f * (qy * qz + qw * qx);
		const float r22 = 1.0f - 2.0f * (qx * qx + qy * qy);

		//Sigma = R * S * S^T * R^T with S diagonal, which reduces to summing the outer
		//product of each scaled basis column. Only the upper triangle is kept - the
		//matrix is symmetric by construction.
		const float ax = sx * sx, ay = sy * sy, az = sz * sz;
		s.cov_diag.x = r00 * r00 * ax + r01 * r01 * ay + r02 * r02 * az;
		s.cov_diag.y = r10 * r10 * ax + r11 * r11 * ay + r12 * r12 * az;
		s.cov_diag.z = r20 * r20 * ax + r21 * r21 * ay + r22 * r22 * az;
		s.cov_offdiag.x = r00 * r10 * ax + r01 * r11 * ay + r02 * r12 * az;  //Sxy
		s.cov_offdiag.y = r00 * r20 * ax + r01 * r21 * ay + r02 * r22 * az;  //Sxz
		s.cov_offdiag.z = r10 * r20 * ax + r11 * r21 * ay + r12 * r22 * az;  //Syz

		//SH degree 0 -> colour, then kept as albedo. Clamped at zero because a
		//negative coefficient is legal in the fit but a negative albedo is not, and it
		//would drive the lighting negative rather than merely looking wrong.
		s.albedo.x = fmaxf(0.0f, 0.5f + SH_C0 * read_f(base, "f_dc_0"));
		s.albedo.y = fmaxf(0.0f, 0.5f + SH_C0 * read_f(base, "f_dc_1"));
		s.albedo.z = fmaxf(0.0f, 0.5f + SH_C0 * read_f(base, "f_dc_2"));

		//A capture carries no specular information at all - a radiance field folds
		//every highlight into the colour it fitted. So this is a constant the material
		//supplies, not something recovered from the file, and it starts low because a
		//scanned surface that is already carrying its own baked highlights should not
		//get a second one on top.
		s.spec_intensity = 0.1f;

		//The minor axis: the basis column belonging to the smallest scale. A trained
		//Gaussian is flat against the surface it represents, so its shortest axis is
		//the surface normal.
		float3 axis;
		if (sx <= sy && sx <= sz) { axis = { r00, r10, r20 }; }
		else if (sy <= sx && sy <= sz) { axis = { r01, r11, r21 }; }
		else { axis = { r02, r12, r22 }; }
		minor_axis.push_back(axis);

		cx += s.position.x;
		cy += s.position.y;
		cz += s.position.z;

		splats.push_back(s);
	}

	const float3 centroid = {
		(float)(cx / (double)vertex_count),
		(float)(cy / (double)vertex_count),
		(float)(cz / (double)vertex_count)
	};

	//Second pass: orient the normals and measure the bounds.
	//
	//An ellipsoid's minor axis has no sign - it is an axis, not a direction - and the
	//file gives nothing to disambiguate it. Pointing it away from the cloud's centroid
	//is right for a scan of an object viewed from outside, which is the common case,
	//and wrong for a room scanned from within, where every normal comes out facing the
	//wall. There is no way to tell the two apart from the point set alone, which is why
	//the component carries invert_normals.
	float3 bmin = { FLT_MAX, FLT_MAX, FLT_MAX };
	float3 bmax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

	for (size_t i = 0; i < splats.size(); ++i) {
		SplatVertex& s = splats[i];
		float3 n = minor_axis[i];
		const float nlen = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
		if (nlen > 1e-8f) { n.x /= nlen; n.y /= nlen; n.z /= nlen; }
		else { n = { 0.0f, 1.0f, 0.0f }; }

		const float3 out = { s.position.x - centroid.x,
							 s.position.y - centroid.y,
							 s.position.z - centroid.z };
		if (n.x * out.x + n.y * out.y + n.z * out.z < 0.0f) {
			n.x = -n.x; n.y = -n.y; n.z = -n.z;
		}
		s.normal = n;

		//3 sigma along each world axis. The covariance diagonal is variance, so the
		//standard deviation is its square root - taking the diagonal directly here
		//would under-measure the box by squaring an already-small number.
		const float ex = BOUNDS_SIGMA * sqrtf(fmaxf(0.0f, s.cov_diag.x));
		const float ey = BOUNDS_SIGMA * sqrtf(fmaxf(0.0f, s.cov_diag.y));
		const float ez = BOUNDS_SIGMA * sqrtf(fmaxf(0.0f, s.cov_diag.z));

		bmin.x = fminf(bmin.x, s.position.x - ex);
		bmin.y = fminf(bmin.y, s.position.y - ey);
		bmin.z = fminf(bmin.z, s.position.z - ez);
		bmax.x = fmaxf(bmax.x, s.position.x + ex);
		bmax.y = fmaxf(bmax.y, s.position.y + ey);
		bmax.z = fmaxf(bmax.z, s.position.z + ez);
	}

	min_dimensions = bmin;
	max_dimensions = bmax;
	splat_count = (uint32_t)splats.size();
	name = asset_name;
	source_file = file;

	LOG_INFO("SplatCloudData::Load: %s -> '%s', %llu splats, box (%.2f %.2f %.2f)-(%.2f %.2f %.2f)",
		file.c_str(), asset_name.c_str(), (unsigned long long)splats.size(),
		bmin.x, bmin.y, bmin.z, bmax.x, bmax.y, bmax.z);

	return true;
}

void SplatCloudData::BuildDefault(const std::string& asset_name) {
	//A Fibonacci sphere: the cheapest way to get points that are near-uniform over a
	//sphere without the pole clustering a lat/long grid produces, which would show up
	//as two bright spots on the stand-in.
	constexpr uint32_t COUNT = 2048;
	constexpr float RADIUS = 0.5f;
	//Tangential extent of one splat. Sized so the sphere reads as a surface rather
	//than as separate dots: roughly the spacing between neighbours at this count.
	constexpr float TANGENT_SIGMA = 0.030f;
	//Radial extent, deliberately much smaller - this is what makes each splat a disc
	//lying on the sphere, and therefore what makes its minor axis the surface normal.
	constexpr float RADIAL_SIGMA = 0.006f;

	//The golden angle, pi * (3 - sqrt(5)). Spelled out rather than reached through
	//M_PI, which needs _USE_MATH_DEFINES before <cmath> and is not defined here.
	constexpr float golden = 2.39996322972865332f;

	splats.clear();
	splats.reserve(COUNT);

	float3 bmin = { FLT_MAX, FLT_MAX, FLT_MAX };
	float3 bmax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

	for (uint32_t i = 0; i < COUNT; ++i) {
		const float y = 1.0f - 2.0f * ((float)i + 0.5f) / (float)COUNT;
		const float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
		const float theta = golden * (float)i;
		const float3 n = { cosf(theta) * r, y, sinf(theta) * r };

		SplatVertex s;
		s.position = { n.x * RADIUS, n.y * RADIUS, n.z * RADIUS };
		s.normal = n;
		s.opacity = 1.0f;
		s.spec_intensity = 0.1f;
		//Normal-as-colour, remapped from [-1,1]. Makes the orientation of the stand-in
		//readable at a glance, which is the whole job of a stand-in.
		s.albedo = { n.x * 0.5f + 0.5f, n.y * 0.5f + 0.5f, n.z * 0.5f + 0.5f };

		//Sigma = R S S^T R^T with the local frame's third axis along the normal. Built
		//directly rather than through a quaternion: only the outer products are needed,
		//and an anisotropic diagonal in a frame whose third axis is n is exactly
		//(t1 t1^T + t2 t2^T) * tangent^2 + n n^T * radial^2. Because the tangential
		//variances are equal, the two tangent vectors cancel out of the sum and the
		//whole thing reduces to an isotropic term minus the radial deficit along n -
		//so no tangent basis has to be constructed at all.
		const float ta = TANGENT_SIGMA * TANGENT_SIGMA;
		const float ra = RADIAL_SIGMA * RADIAL_SIGMA;
		const float d = ra - ta;
		s.cov_diag = { ta + d * n.x * n.x, ta + d * n.y * n.y, ta + d * n.z * n.z };
		s.cov_offdiag = { d * n.x * n.y, d * n.x * n.z, d * n.y * n.z };

		const float ex = BOUNDS_SIGMA * sqrtf(fmaxf(0.0f, s.cov_diag.x));
		const float ey = BOUNDS_SIGMA * sqrtf(fmaxf(0.0f, s.cov_diag.y));
		const float ez = BOUNDS_SIGMA * sqrtf(fmaxf(0.0f, s.cov_diag.z));
		bmin.x = fminf(bmin.x, s.position.x - ex);
		bmin.y = fminf(bmin.y, s.position.y - ey);
		bmin.z = fminf(bmin.z, s.position.z - ez);
		bmax.x = fmaxf(bmax.x, s.position.x + ex);
		bmax.y = fmaxf(bmax.y, s.position.y + ey);
		bmax.z = fmaxf(bmax.z, s.position.z + ez);

		splats.push_back(s);
	}

	min_dimensions = bmin;
	max_dimensions = bmax;
	splat_count = (uint32_t)splats.size();
	name = asset_name;
	source_file.clear();
}

HRESULT SplatCloudData::Prepare() {
	if (prepared || splats.empty()) {
		return S_OK;
	}
	ID3D11Device* device = Core::DXCore::Get()->device;

	D3D11_BUFFER_DESC bd{};
	bd.Usage = D3D11_USAGE_IMMUTABLE;
	bd.ByteWidth = (UINT)(sizeof(SplatVertex) * splats.size());
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.CPUAccessFlags = 0;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.StructureByteStride = sizeof(SplatVertex);

	D3D11_SUBRESOURCE_DATA init{};
	init.pSysMem = splats.data();

	HRESULT hr = device->CreateBuffer(&bd, &init, &buffer);
	if (FAILED(hr)) {
		LOG_ERROR("SplatCloudData::Prepare: CreateBuffer failed for '%s' (%llu splats, %.1f MB, 0x%08x)",
			name.c_str(), (unsigned long long)splats.size(),
			(double)bd.ByteWidth / (1024.0 * 1024.0), hr);
		return hr;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
	srv_desc.Format = DXGI_FORMAT_UNKNOWN;
	srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srv_desc.Buffer.FirstElement = 0;
	srv_desc.Buffer.NumElements = (uint32_t)splats.size();
	hr = device->CreateShaderResourceView(buffer, &srv_desc, &srv);
	if (FAILED(hr)) {
		LOG_ERROR("SplatCloudData::Prepare: CreateShaderResourceView failed for '%s' (0x%08x)",
			name.c_str(), hr);
		buffer->Release();
		buffer = nullptr;
		return hr;
	}

	prepared = true;
	LOG_INFO("SplatCloudData::Prepare: '%s' uploaded, %llu splats, %.1f MB",
		name.c_str(), (unsigned long long)splats.size(),
		(double)bd.ByteWidth / (1024.0 * 1024.0));
	return S_OK;
}

void SplatCloudData::ReleaseCpuCopy() {
	//swap-with-empty, because clear() on a vector keeps the capacity - which for a
	//capture is the entire allocation this is trying to give back.
	std::vector<SplatVertex>().swap(splats);
}

void SplatCloudData::Unprepare() {
	if (srv != nullptr) {
		srv->Release();
		srv = nullptr;
	}
	if (buffer != nullptr) {
		buffer->Release();
		buffer = nullptr;
	}
	prepared = false;
}
