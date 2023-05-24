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

#include <winsock2.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <vector>
#include <unordered_map>
#include <algorithm>
namespace HotBite {
	namespace Engine {

#define MAX_TEXTURE_SIZE 16384
        typedef DirectX::XMFLOAT4X4 float4x4;
        typedef DirectX::XMFLOAT3X3 float3x3;
        typedef DirectX::XMFLOAT3X4 float3x4;
        typedef DirectX::XMFLOAT4   float4;
        typedef DirectX::XMFLOAT3   float3;
        typedef DirectX::XMFLOAT2   float2;
        typedef DirectX::XMVECTOR   vector4d;
        typedef DirectX::XMVECTOR   vector3d;
        typedef DirectX::XMMATRIX   matrix;
        typedef DirectX::BoundingOrientedBox box;


        struct Int2 {
            int x = 0;
            int y = 0;

            bool operator==(const Int2& p) const {
                return x == p.x && y == p.y;
            }

            Int2() {}

            Int2(int x0, int y0): x(x0), y(y0) {}

            Int2(const float2& other) {
                this->x = (int)other.x;
                this->y = (int)other.y;
            }

            operator float2() const {
                return float2{(float)x, (float)y};
            }

            static float Dist2(const Int2& p0, const Int2& p1) {
                float d1 = (float)(p1.x - p0.x);
                float d2 = (float)(p1.y - p0.y);
                return (d1 * d1 + d2 * d2);
            }

            static float Dist(const Int2& p0, const Int2& p1) {
                return sqrtf(Dist2(p0, p1));
            }
        };

        bool operator==(const float3& a, const float3& b);
        bool operator==(const float2& a, const float2& b);
        bool operator!=(const float3& a, const float3& b);
        bool operator!=(const float2& a, const float2& b);
     
        float3 operator-(const float3& a, const float3& b);
        float2 operator-(const float2& a, const float2& b);
        
        inline  bool EQ_INT2(const float2& a, const float2& b) {
            return (int)a.x == (int)b.x && (int)a.y == (int)b.y;
        }

        inline float3 ADD_F3_F3(const float3& a, const float3& b) {
            return { a.x + b.x , a.y + b.y , a.z + b.z };
        }

        inline float3 SUB_F3_F3(const float3& a, const float3& b) {
            return { a.x - b.x , a.y - b.y , a.z - b.z };
        }

        inline float2 ADD_F2_F2(const float2& a, const float2& b) {
            return { a.x + b.x , a.y + b.y };
        }

        inline float3 MULT_F3_F3(const float3& a, const float3& b) {
            return { a.x * b.x , a.y * b.y , a.z * b.z };
        }

        inline float3 MULT_F3_F(const float3& a, const float& b) {
            return { a.x * b , a.y * b , a.z * b };
        }

        inline float2 MULT_F2_F(const float2& a, const float& b) {
            return { a.x * b , a.y * b };
        }

        inline bool EQ_INT2(const float2& a, const float2& b);

        inline float LENGHT_SQUARE_F2(const float2& a) {
            return (a.x * a.x + a.y * a.y);
        }

        inline float LENGHT_F2(const float2& a) {
            return sqrt(a.x*a.x + a.y*a.y);
        }

        inline float LENGHT_SQUARE_F3(const float3& a) {
            return (a.x * a.x + a.y * a.y + a.z * a.z);
        }

        inline float LENGHT_F3(const float3& a) {
            return sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
        }

        inline float3 UNIT_F3(const float3& a) {
            float length = LENGHT_F3(a);
            return { a.x / length, a.y / length, a.z / length };
        }

        inline float2 UNIT_F2(const float2& a) {
            float length = LENGHT_F2(a);
            return { a.x / length, a.y / length };
        }

        inline float2 DIV_F2(const float2& a, const float d) {
            return { a.x / d, a.y / d };
        }
	}
}