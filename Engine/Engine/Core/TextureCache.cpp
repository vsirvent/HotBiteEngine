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
#include "Utils.h"
#include "TextureCache.h"
#include "DXCore.h"
#include "Log.h"

#include <DirectXTex.h>
#include <DDSTextureLoader.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;

namespace {
	constexpr char MAGIC[4] = { 'H', 'B', 'T', 'X' };
	// Bumped when the cooker produces something different (format choice, mip filter).
	constexpr uint32_t VERSION = 1;
	constexpr const char* EXTENSION = ".cooked";

	// magic, version, source size, source time, size of the .dds that follows.
	struct Header {
		char magic[4];
		uint32_t version;
		uint64_t source_size;
		int64_t source_time;
		uint64_t dds_size;
	};

	std::atomic<uint32_t> cooked_reads{ 0 }, decodes{ 0 }, cooked_writes{ 0 }, plain_loads{ 0 }, failures{ 0 };
	std::atomic<uint64_t> cooked_read_us{ 0 }, decode_us{ 0 };

	// Sources that cannot be cooked, so a texture that is never going to be (an odd
	// format) is not decoded again by every load that asks for it.
	std::mutex uncookable_mutex;
	std::set<std::string>& uncookable = *new std::set<std::string>();

	using Clock = std::chrono::steady_clock;
	uint64_t MicrosSince(Clock::time_point start) {
		return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
	}

	// WIC needs COM on the calling thread. The main thread has it already; a worker does not.
	class ComScope {
		bool owned = false;
	public:
		ComScope() {
			const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			owned = SUCCEEDED(hr);
		}
		~ComScope() {
			if (owned) { CoUninitialize(); }
		}
	};

	bool StampOf(const std::string& source, uint64_t& size, int64_t& time) {
		std::error_code ec;
		const std::filesystem::path p(source);
		const uintmax_t file_size = std::filesystem::file_size(p, ec);
		if (ec) { return false; }
		const auto write_time = std::filesystem::last_write_time(p, ec);
		if (ec) { return false; }
		size = (uint64_t)file_size;
		time = (int64_t)write_time.time_since_epoch().count();
		return true;
	}

	std::wstring Wide(const std::string& s) {
		return std::wstring(s.begin(), s.end());
	}

	// The cooked file's bytes when it is whole and current; empty otherwise. `dds` and `size`
	// point into `content`.
	bool ReadCooked(const std::string& source, std::string& content, const uint8_t*& dds, size_t& size) {
		const std::string file = source + EXTENSION;
		std::ifstream in(file, std::ios::binary | std::ios::ate);
		if (!in) { return false; }
		const std::streamsize length = in.tellg();
		if (length < (std::streamsize)sizeof(Header)) { return false; }
		in.seekg(0);
		content.resize((size_t)length);
		if (!in.read(content.data(), length)) { return false; }
		Header h;
		std::memcpy(&h, content.data(), sizeof(h));
		if (std::memcmp(h.magic, MAGIC, sizeof(MAGIC)) != 0 || h.version != VERSION ||
			h.dds_size != (uint64_t)length - sizeof(Header)) {
			return false;
		}
		uint64_t src_size = 0;
		int64_t src_time = 0;
		// With no source on disk the cooked file is trusted (a build that ships cooked files only).
		if (StampOf(source, src_size, src_time) && (src_size != h.source_size || src_time != h.source_time)) {
			return false;
		}
		dds = reinterpret_cast<const uint8_t*>(content.data()) + sizeof(Header);
		size = (size_t)h.dds_size;
		return true;
	}

	// A device of the cooker's own, for compressing BC7 on the GPU (DirectXTex's compute
	// compressor): about a hundred times faster than the CPU encoder, and the reason cooking a
	// texture takes tenths of a second instead of tens. It is not the renderer's device - the
	// compressor drives that device's immediate context, which the render thread owns - so it
	// has its own, used by one thread at a time. Null when no hardware device can be made
	// (a machine with no D3D11 GPU), and the CPU encoder does the work.
	std::mutex gpu_mutex;
	ID3D11Device* gpu_device = nullptr;
	bool gpu_tried = false;

	// gpu_mutex held.
	ID3D11Device* CookDevice() {
		if (!gpu_tried) {
			gpu_tried = true;
			const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
			D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;
			if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
				D3D11_SDK_VERSION, &gpu_device, &got, nullptr))) {
				gpu_device = nullptr;
			}
		}
		return gpu_device;
	}

	// Decodes `source`, builds its mips, compresses it and returns the .dds bytes.
	bool CookToMemory(const std::string& source, DirectX::Blob& dds, std::string& error, bool wait_for_gpu) {
		using namespace DirectX;
		ComScope com;
		TexMetadata meta;
		ScratchImage image;
		HRESULT hr = LoadFromWICFile(Wide(source).c_str(), WIC_FLAGS_NONE, &meta, image);
		if (FAILED(hr)) {
			error = "cannot decode " + source;
			return false;
		}
		const bool srgb = IsSRGB(meta.format);
		DXGI_FORMAT target = DXGI_FORMAT_UNKNOWN;
		bool single_channel = false;
		switch (meta.format) {
		case DXGI_FORMAT_R8_UNORM:
			single_channel = true;
			target = DXGI_FORMAT_BC4_UNORM;
			break;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			target = srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
			break;
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: {
			ScratchImage converted;
			const DXGI_FORMAT rgba = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
			hr = Convert(image.GetImages(), image.GetImageCount(), meta, rgba, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted);
			if (FAILED(hr)) {
				error = "cannot convert " + source;
				return false;
			}
			image = std::move(converted);
			meta = image.GetMetadata();
			target = srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
		}break;
		default:
			error = "format not kept by the cooker: " + source;
			return false;
		}

		ScratchImage chain;
		hr = GenerateMipMaps(image.GetImages(), image.GetImageCount(), meta, TEX_FILTER_DEFAULT, 0, chain);
		if (FAILED(hr)) {
			error = "cannot build mips of " + source;
			return false;
		}
		// Blocks are 4x4: a size that is not a multiple of four stays as it is, uncompressed,
		// which still saves the decode and the GPU's mip build.
		const bool blocks = (meta.width % 4 == 0) && (meta.height % 4 == 0);
		ScratchImage* result = &chain;
		ScratchImage compressed;
		if (blocks) {
			const TEX_COMPRESS_FLAGS flags = single_channel ? TEX_COMPRESS_DEFAULT
				: (TEX_COMPRESS_FLAGS)(TEX_COMPRESS_BC7_QUICK | TEX_COMPRESS_PARALLEL);
			hr = E_FAIL;
			if (!single_channel) {
				//The GPU when it is free, the CPU encoder when another texture is on it: with
				//many cores cooking at once the two together finish sooner than either alone.
				//`wait_for_gpu` (one background cooker, which has no one to share with) queues.
				std::unique_lock<std::mutex> gpu(gpu_mutex, std::defer_lock);
				if (wait_for_gpu) { gpu.lock(); } else { (void)gpu.try_lock(); }
				if (gpu.owns_lock()) {
					ID3D11Device* device = CookDevice();
					if (device != nullptr) {
						hr = Compress(device, chain.GetImages(), chain.GetImageCount(), chain.GetMetadata(), target,
							TEX_COMPRESS_DEFAULT, 1.0f, compressed);
					}
				}
			}
			if (FAILED(hr)) {
				hr = Compress(chain.GetImages(), chain.GetImageCount(), chain.GetMetadata(), target, flags, 1.0f, compressed);
			}
			if (FAILED(hr)) {
				error = "cannot compress " + source;
				return false;
			}
			result = &compressed;
		}
		hr = SaveToDDSMemory(result->GetImages(), result->GetImageCount(), result->GetMetadata(), DDS_FLAGS_NONE, dds);
		if (FAILED(hr)) {
			error = "cannot write the .dds of " + source;
			return false;
		}
		return true;
	}

	bool WriteCooked(const std::string& source, const DirectX::Blob& dds, std::string& error) {
		Header h = {};
		std::memcpy(h.magic, MAGIC, sizeof(MAGIC));
		h.version = VERSION;
		StampOf(source, h.source_size, h.source_time);
		h.dds_size = dds.GetBufferSize();
		const std::string file = source + EXTENSION;
		const std::string temp = file + ".tmp" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetCurrentThreadId());
		{
			std::ofstream out(temp, std::ios::binary | std::ios::trunc);
			if (!out) { error = "cannot write " + temp; return false; }
			out.write(reinterpret_cast<const char*>(&h), sizeof(h));
			out.write(reinterpret_cast<const char*>(dds.GetBufferPointer()), (std::streamsize)dds.GetBufferSize());
			out.flush();
			if (!out) {
				error = "short write to " + temp;
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
			error = "cannot replace " + file + ": " + ec.message();
			return false;
		}
		return true;
	}

	bool IsUncookable(const std::string& source) {
		std::lock_guard<std::mutex> l(uncookable_mutex);
		return uncookable.count(source) != 0;
	}
	void MarkUncookable(const std::string& source) {
		std::lock_guard<std::mutex> l(uncookable_mutex);
		uncookable.insert(source);
	}

	// Textures with no current cooked file are cooked on a thread of their own, a low
	// priority one, while the caller loads them the old way: the first start is no slower
	// than it was (a BC7 encode is seconds a texture), and the next one finds them done.
	// The thread ends when its queue is empty and is started again by the next miss.
	std::mutex queue_mutex;
	// Leaked on purpose: the cooker thread is detached and may still be running when the
	// process's statics are destroyed.
	std::vector<std::string>& queue = *new std::vector<std::string>();
	std::set<std::string>& queued = *new std::set<std::string>();
	bool worker_running = false;

	void BackgroundCook() {
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
		for (;;) {
			std::string source;
			{
				std::lock_guard<std::mutex> l(queue_mutex);
				if (queue.empty()) {
					worker_running = false;
					return;
				}
				source = queue.back();
				queue.pop_back();
			}
			DirectX::Blob blob;
			std::string error;
			const Clock::time_point began = Clock::now();
			if (CookToMemory(source, blob, error, true)) {
				decode_us += MicrosSince(began);
				++decodes;
				if (WriteCooked(source, blob, error)) { ++cooked_writes; }
				else { LOG_WARN("Texture cache: %s", error.c_str()); }
			}
			else {
				MarkUncookable(source);
			}
		}
	}

	void CookInBackground(const std::string& source) {
		std::lock_guard<std::mutex> l(queue_mutex);
		if (!queued.insert(source).second) {
			return;
		}
		queue.push_back(source);
		if (!worker_running) {
			worker_running = true;
			std::thread(BackgroundCook).detach();
		}
	}
}

bool Core::TextureCacheEnabled() {
	char value[8] = {};
	const DWORD length = GetEnvironmentVariableA("HOTBITE_TEXTURE_CACHE", value, (DWORD)sizeof(value));
	return !(length > 0 && length < sizeof(value) && value[0] == '0');
}

TextureCacheStats Core::GetTextureCacheStats() {
	TextureCacheStats s;
	s.cooked_reads = cooked_reads;
	s.decodes = decodes;
	s.cooked_writes = cooked_writes;
	s.plain_loads = plain_loads;
	s.failures = failures;
	s.cooked_read_ms = (double)cooked_read_us / 1000.0;
	s.decode_ms = (double)decode_us / 1000.0;
	return s;
}

void Core::ResetTextureCacheStats() {
	cooked_reads = 0; decodes = 0; cooked_writes = 0; plain_loads = 0; failures = 0;
	cooked_read_us = 0; decode_us = 0;
}

namespace {
	std::mutex plain_mutex;
	std::vector<std::string>& plain_names = *new std::vector<std::string>();
}

void Core::CountPlainTextureLoad(const std::string& filename) {
	++plain_loads;
	std::lock_guard<std::mutex> l(plain_mutex);
	if (plain_names.size() < 64) {
		plain_names.push_back(filename);
	}
}

std::vector<std::string> Core::PlainTextureLoads() {
	std::lock_guard<std::mutex> l(plain_mutex);
	return plain_names;
}

ID3D11ShaderResourceView* Core::LoadCookedTexture(const std::string& filename) {
	if (filename.empty() || !TextureCacheEnabled() || IsUncookable(filename)) {
		return nullptr;
	}
	ID3D11Device* device = DXCore::Get()->device;
	std::error_code ec;
	const std::string cooked_file = filename + EXTENSION;
	const bool has_source = std::filesystem::exists(filename, ec);
	if (!has_source && !std::filesystem::exists(cooked_file, ec)) {
		return nullptr;
	}

	std::string content;
	const uint8_t* dds = nullptr;
	size_t dds_size = 0;
	const Clock::time_point start = Clock::now();
	if (!ReadCooked(filename, content, dds, dds_size)) {
		if (!has_source) {
			++failures;
			return nullptr;
		}
		// Not cooked yet: cook it in the background and let the caller load the source now.
		CookInBackground(filename);
		return nullptr;
	}
	ID3D11ShaderResourceView* srv = nullptr;
	if (FAILED(DirectX::CreateDDSTextureFromMemory(device, dds, dds_size, nullptr, &srv))) {
		++failures;
		return nullptr;
	}
	cooked_read_us += MicrosSince(start);
	++cooked_reads;
	return srv;
}

bool Core::CookTexture(const std::string& filename, bool force, std::string* error) {
	std::string why;
	std::error_code ec;
	if (!std::filesystem::exists(filename, ec)) {
		if (error != nullptr) { *error = "no such texture: " + filename; }
		return false;
	}
	if (!force) {
		std::string content;
		const uint8_t* dds = nullptr;
		size_t size = 0;
		if (ReadCooked(filename, content, dds, size)) {
			return true;
		}
	}
	DirectX::Blob blob;
	const Clock::time_point began = Clock::now();
	if (!CookToMemory(filename, blob, why, false)) {
		// A format the cooker does not keep is not a failure: it loads the plain way.
		if (why.compare(0, 8, "format n") == 0) {
			return true;
		}
		if (error != nullptr) { *error = why; }
		return false;
	}
	decode_us += MicrosSince(began);
	++decodes;
	if (!WriteCooked(filename, blob, why)) {
		if (error != nullptr) { *error = why; }
		return false;
	}
	++cooked_writes;
	return true;
}

int Core::CookTextures(const std::vector<std::string>& all, bool force) {
	// Once each: two workers cooking the same file would only fight over it.
	std::vector<std::string> filenames = all;
	std::sort(filenames.begin(), filenames.end());
	filenames.erase(std::unique(filenames.begin(), filenames.end()), filenames.end());
	std::atomic<size_t> next{ 0 };
	std::atomic<int> failed{ 0 };
	const size_t workers = (std::min)((size_t)(std::max)(2u, std::thread::hardware_concurrency()), filenames.size());
	std::vector<std::thread> threads;
	for (size_t t = 0; t < workers; ++t) {
		threads.emplace_back([&]() {
			for (size_t i = next++; i < filenames.size(); i = next++) {
				if (!CookTexture(filenames[i], force)) { ++failed; }
			}
		});
	}
	for (std::thread& t : threads) { t.join(); }
	return failed;
}
