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

#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>

namespace HotBite {
	namespace Engine {
		namespace Core {
			// == Cooked textures ========================================================
			//
			// A .png is a source format: loading one decodes it on the CPU (a 2048x2048
			// takes ~50 ms), uploads it as 16 MB of RGBA and has the GPU build the mips,
			// all on the thread that asked, one texture at a time.
			//
			// A cooked texture is that work done once: the same image with its whole mip
			// chain, block-compressed (BC7 for colour, BC4 for a single channel - a quarter
			// of the memory and of the file), stored beside the source as
			// `<source>.cooked` (a small header with the source's size and time, then a
			// .dds). Loading it is reading a file and handing the bytes to the device,
			// which needs no context and so can be done on any thread: that is what
			// Core::PreloadTextures does with a material file's textures all at once.
			//
			// The cache is lossy where BC7 is, and that is the trade: it applies only to
			// formats the cooker knows how to keep (8-bit grey, 8-bit RGB/RGBA, with their
			// sRGB flag as the source had it); anything else loads as it always did.
			// A cooked file is rebuilt when the source's size or time changes, or the
			// cook version moves; with no source on disk it is used as it is.

			// On unless HOTBITE_TEXTURE_CACHE=0 is in the environment.
			bool TextureCacheEnabled();

			// The texture of `filename` (anything but a .dds) from its cooked file, cooking
			// it first when there is none or it is stale. One reference, which the caller
			// owns. Null when the file cannot be cooked (unreadable, a format the cooker
			// does not keep, the cache off): load it the plain way. Thread-safe, and uses
			// no device context.
			ID3D11ShaderResourceView* LoadCookedTexture(const std::string& filename);

			// Writes `filename`'s cooked file, with no device: what a build step runs.
			// `force` rebuilds a current one. False (and `error`) when it cannot be cooked.
			bool CookTexture(const std::string& filename, bool force, std::string* error = nullptr);

			// CookTexture for each, on every core. Returns how many could not be cooked.
			int CookTextures(const std::vector<std::string>& filenames, bool force = false);

			struct TextureCacheStats {
				uint32_t cooked_reads = 0;   // loaded from a current cooked file
				uint32_t decodes = 0;        // the source image had to be decoded (and cooked)
				uint32_t cooked_writes = 0;
				uint32_t plain_loads = 0;    // loaded the old way: not cookable, or the cache off
				uint32_t failures = 0;
				double cooked_read_ms = 0.0; // summed over all threads
				double decode_ms = 0.0;
			};
			TextureCacheStats GetTextureCacheStats();
			void ResetTextureCacheStats();
			// A texture loaded the old way, by name (the names are kept, newest 64, to say which).
			void CountPlainTextureLoad(const std::string& filename);
			std::vector<std::string> PlainTextureLoads();
		}
	}
}
