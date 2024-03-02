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
#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>
#include <Core/DXCore.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>

using namespace DirectX;

namespace HotBite {
	namespace Engine {
		namespace Core {

			float3 parseColorStringF3(const std::string& colorString) {
				float4 color = parseColorStringF4(colorString);
				return { color.x, color.y, color.z };
			}

			float4 parseColorStringF4(const std::string& colorString) {
				// Check if the string has the correct format
				if (colorString.size() != 9 || colorString[0] != '#') {
					printf("Invalid color string format %s\n", colorString.c_str());
					return {};
				}

				float r, g, b, a;

				// Extract RR, GG, BB, AA substrings
				std::string rr = colorString.substr(1, 2);
				std::string gg = colorString.substr(3, 2);
				std::string bb = colorString.substr(5, 2);
				std::string aa = colorString.substr(7, 2);

				// Convert substrings to integers
				r = (float)std::stoi(rr, nullptr, 16) / 255.0f;
				g = (float)std::stoi(gg, nullptr, 16) / 255.0f;
				b = (float)std::stoi(bb, nullptr, 16) / 255.0f;
				a = (float)std::stoi(aa, nullptr, 16) / 255.0f;
				return { r, g, b, a };
			}

			void SetFlag(uint32_t& bitset, uint32_t flag) {
				UpdateFlag(bitset, flag, true);
			}

			void ClearFlag(uint32_t& bitset, uint32_t flag) {
				UpdateFlag(bitset, flag, false);
			}

			void UpdateFlag(uint32_t& bitset, uint32_t flag, bool active) {
				if (active) {
					bitset |= flag;
				}
				else {
					bitset &= ~flag;
				}
			}

			bool Match(const std::string& str, const std::string& pattern) {
				size_t wildcard_pos = pattern.find("*");
				if (wildcard_pos != std::string::npos)
				{
					bool f1 = true;
					bool f2 = true;
					if (wildcard_pos > 1) {
						f1 = (str.find(pattern.substr(0, wildcard_pos)) != std::string::npos);
					}
					if (wildcard_pos < pattern.size() - 1) {
						f2 = (str.find(pattern.substr(wildcard_pos + 1)) != std::string::npos);
					}
					return (f1 && f2);
				}
				else {
					return (str == pattern);
				}
			}

			std::mutex texture_mutex;
			std::unordered_map<std::string, ID3D11ShaderResourceView*> textures;
			std::unordered_map<ID3D11ShaderResourceView*, std::string> textures_names;

			void ReleaseTexture(ID3D11ShaderResourceView* srv) {
				std::lock_guard<std::mutex> l(texture_mutex);
				if (srv != nullptr && srv->Release() == 0) {
					auto it0 = textures_names.find(srv);
					auto it1 = textures.find(it0->second);
					textures.erase(it1);
					textures_names.erase(it0);
				}
			}

			ID3D11ShaderResourceView* LoadTexture(const std::string& filename)
			{
				std::lock_guard<std::mutex> l(texture_mutex);
				ID3D11ShaderResourceView* srv = nullptr;
				HRESULT ret = S_OK;
				if (!filename.empty()) {
					auto it = textures.find(filename);
					if (it == textures.end()) {					
						try {
							std::wstring ws(filename.begin(), filename.end());
							if (filename.find(".dds") != std::string::npos || filename.find(".DDS") != std::string::npos) {
								ret = DirectX::CreateDDSTextureFromFile(DXCore::Get()->device, DXCore::Get()->context, ws.c_str(), nullptr, &srv);
								if (!SUCCEEDED(ret)) {
									throw std::exception("CreateDDSTextureFromFile failed");
								}
								printf("Loaded DDS texture %s\n", filename.c_str());
							}
							else {
								ret = DirectX::CreateWICTextureFromFile(DXCore::Get()->device, DXCore::Get()->context, ws.c_str(), nullptr, &srv);
								if (!SUCCEEDED(ret)) {
									throw std::exception("CreateWICTextureFromMemory failed");
								}
							}

						}
						catch (std::exception e) {
							printf("Error: %s, error %d loading texture %s\n", e.what(), ret, filename.c_str());
						}
						if (srv != nullptr) {
							textures[filename] = srv;
							textures_names[srv] = filename;
						}
					}
					else {
						srv = it->second;
						srv->AddRef();
					}
				}
				return srv;
			}

			float4 ColorFromStr(const std::string& color) {
				float4 ret{};
				ret.x = (float)strtol(color.substr(0, 3).c_str(), NULL, 10) / 100.0f;
				ret.y = (float)strtol(color.substr(3, 3).c_str(), NULL, 10) / 100.0f;
				ret.z = (float)strtol(color.substr(6, 3).c_str(), NULL, 10) / 100.0f;
				ret.w = (float)strtol(color.substr(9, 3).c_str(), NULL, 10) / 100.0f;
				return ret;
			}

			float3 ColorRGBFromStr(const std::string& color) {
				float3 ret{};
				ret.x = (float)strtol(color.substr(0, 3).c_str(), NULL, 10) / 100.0f;
				ret.y = (float)strtol(color.substr(3, 3).c_str(), NULL, 10) / 100.0f;
				ret.z = (float)strtol(color.substr(6, 3).c_str(), NULL, 10) / 100.0f;
				return ret;
			}

			float2 WorldToScreen(const float3& world_position, const matrix& view_projection, const float w, const float h) {
				vector4d v = XMVector3Transform(XMLoadFloat3(&world_position), view_projection);
				// Divide the x, y, and z components of the clip-space point by the w component
				v.m128_f32[0] /= v.m128_f32[3];
				v.m128_f32[1] /= v.m128_f32[3];
				// Multiply the x and y NDC coordinates by the width and height of the viewport
				v.m128_f32[0] = (1.0f + v.m128_f32[0]) * 0.5f * w;
				v.m128_f32[1] = (1.0f - v.m128_f32[1]) * 0.5f * h;
				return { v.m128_f32[0], v.m128_f32[1] };
			}
#pragma pack(1)
			struct BmpHeader {
				char magic[2];          // 0-1
				uint32_t fileSize;      // 2-5
				uint32_t reserved;      // 6-9
				uint32_t offset;        // 10-13
				uint32_t headerSize;    // 14-17
				uint32_t width;         // 18-21
				uint32_t height;        // 22-25
				uint16_t bitsPerPixel;  // 26-27
				uint16_t bitDepth;      // 28-29
			};
#pragma pack()

			float getRandomNumber(float min, float max)
			{
				// Generate a random number within the specified range
				return ((float)rand() / (float)RAND_MAX) * (max - min) + min;
			}

			std::vector<std::vector<uint8_t>> getBlackAndWhiteBmp(std::string filename) {
				BmpHeader head;
				std::ifstream f(filename, std::ios::binary);

				if (!f) {
					throw "Invalid file given";
				}

				int headSize = sizeof(BmpHeader);
				f.read((char*)&head, headSize);

				if (head.bitsPerPixel != 1) {
					f.close();
					throw "Invalid bitmap loaded";
				}

				int height = head.height;
				int width = head.width;

				// Lines are aligned on a 4-byte boundary
				int lineSize = ((width + 31) / 32) * 4;
				int fileSize = lineSize * height;

				std::vector<unsigned char> rawFile(fileSize);
				std::vector<std::vector<uint8_t>> img;
				img.resize(height);
				for (size_t i = 0; i < height; ++i) {
					img[i].resize(width);
				}

				// Skip to where the actual image data is
				f.seekg(head.offset);

				// Read in all of the file
				f.read((char*)&rawFile[0], fileSize);

				// Decode the actual boolean values of the pixesl
				int row;
				int reverseRow; // Because bitmaps are stored bottom to top for some reason
				int columnByte;
				int columnBit;

				for (row = 0, reverseRow = height - 1; row < height; ++row, --reverseRow) {
					columnBit = 0;
					for (columnByte = 0; columnByte < ceil((width / 8.0)); ++columnByte) {
						int rawPos = (row * lineSize) + columnByte;

						for (int k = 7; k >= 0 && columnBit < width; --k, ++columnBit) {
							img[columnBit][reverseRow] = (rawFile[rawPos] >> k) & 1;
						}
					}
				}

				f.close();
				return img;
			}

			DirectX::BoundingOrientedBox TransformBoundingBox(const DirectX::BoundingOrientedBox& originalBox, const DirectX::XMMATRIX& worldMatrix)
			{

				BoundingOrientedBox transformedBox;

				// Transform the center of the original bounding box by the world matrix
				XMVECTOR center = XMVector3TransformCoord(XMLoadFloat3(&originalBox.Center), worldMatrix);

				// Define the eight vertices of the original bounding box
				XMVECTOR corners[8];
				corners[0] = XMLoadFloat3(&originalBox.Center) + XMLoadFloat3(&originalBox.Extents) * XMVectorSet(-1.0f, -1.0f, -1.0f, 0.0f);
				corners[1] = XMLoadFloat3(&originalBox.Center) + XMLoadFloat3(&originalBox.Extents) * XMVectorSet(-1.0f, -1.0f, 1.0f, 0.0f);
				corners[2] = XMLoadFloat3(&originalBox.Center) + XMLoadFloat3(&originalBox.Extents) * XMVectorSet(-1.0f, 1.0f, -1.0f, 0.0f);
				corners[3] = XMLoadFloat3(&originalBox.Center) + XMLoadFloat3(&originalBox.Extents) * XMVectorSet(-1.0f, 1.0f, 1.0f, 0.0f);
				corners[4] = XMLoadFloat3(&originalBox.Center) + XMLoadFloat3(&originalBox.Extents) * XMVectorSet(1.0f, -1.0f, -1.0f, 0.0f);
				corners[5] = XMLoadFloat3(&originalBox.Center) + XMLoadFloat3(&originalBox.Extents) * XMVectorSet(1.0f, -1.0f, 1.0f, 0.0f);
				corners[6] = XMLoadFloat3(&originalBox.Center) + XMLoadFloat3(&originalBox.Extents) * XMVectorSet(1.0f, 1.0f, -1.0f, 0.0f);
				corners[7] = XMLoadFloat3(&originalBox.Center) + XMLoadFloat3(&originalBox.Extents) * XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);

				// Transform all eight vertices of the original bounding box by the world matrix
				for (int i = 0; i < 8; ++i) {
					corners[i] = XMVector3TransformCoord(corners[i], worldMatrix);
				}

				// Calculate the minimum and maximum coordinates among the transformed vertices to determine the new extent
				XMVECTOR minVec = corners[0];
				XMVECTOR maxVec = corners[0];
				for (int i = 1; i < 8; ++i) {
					minVec = XMVectorMin(minVec, corners[i]);
					maxVec = XMVectorMax(maxVec, corners[i]);
				}

				// Calculate the new center and extent of the transformed bounding box
				XMStoreFloat3(&transformedBox.Center, center);
				XMStoreFloat3(&transformedBox.Extents, XMVectorSubtract(maxVec, minVec) * 0.5f);

				return transformedBox;
			}
		}
	}
}
