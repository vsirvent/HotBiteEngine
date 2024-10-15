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

#define HOTBITE_VERSION "1.0.0"

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
        typedef DirectX::BoundingBox box;
        typedef DirectX::BoundingOrientedBox orientedBox;


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
        
        // Function to multiply two quaternions (float4)
        inline float4 quaternion_multiply(const float4& q1, const float4& q2) {
            __m128 q1_vec = _mm_set_ps(q1.w, q1.z, q1.y, q1.x);
            __m128 q2_vec = _mm_set_ps(q2.w, q2.z, q2.y, q2.x);

            // Compute intermediate results
            __m128 q2_shuf1 = _mm_shuffle_ps(q2_vec, q2_vec, _MM_SHUFFLE(3, 0, 2, 1));
            __m128 q1_mul1 = _mm_mul_ps(q1_vec, q2_shuf1);

            __m128 q2_shuf2 = _mm_shuffle_ps(q2_vec, q2_vec, _MM_SHUFFLE(2, 1, 3, 0));
            __m128 q1_mul2 = _mm_mul_ps(q1_vec, q2_shuf2);

            __m128 q2_shuf3 = _mm_shuffle_ps(q2_vec, q2_vec, _MM_SHUFFLE(1, 3, 0, 2));
            __m128 q1_mul3 = _mm_mul_ps(q1_vec, q2_shuf3);

            __m128 result_x = _mm_sub_ps(_mm_add_ps(q1_mul1, q1_mul2), q1_mul3);

            // Compute result.w
            __m128 q2_shuf4 = _mm_shuffle_ps(q2_vec, q2_vec, _MM_SHUFFLE(0, 1, 2, 3));
            __m128 result_w = _mm_mul_ps(q1_vec, q2_shuf4);

            // Sum the elements for result.x
            __m128 sum_x = _mm_hadd_ps(result_x, result_x);
            sum_x = _mm_hadd_ps(sum_x, sum_x);

            // Prepare to compute result.y, result.z, result.w
            __m128 temp_yzw = _mm_shuffle_ps(result_x, result_w, _MM_SHUFFLE(2, 1, 0, 3));
            __m128 yzw = _mm_addsub_ps(temp_yzw, _mm_shuffle_ps(result_w, result_w, _MM_SHUFFLE(3, 3, 3, 3)));

            // Extract results
            float4 result;
            result.x = _mm_cvtss_f32(sum_x);
            result.y = _mm_cvtss_f32(_mm_shuffle_ps(yzw, yzw, _MM_SHUFFLE(0, 0, 0, 0)));
            result.z = _mm_cvtss_f32(_mm_shuffle_ps(yzw, yzw, _MM_SHUFFLE(1, 1, 1, 1)));
            result.w = _mm_cvtss_f32(_mm_shuffle_ps(yzw, yzw, _MM_SHUFFLE(2, 2, 2, 2)));

            return result;
        }

        // Function to compute the conjugate of a quaternion (float4)
        inline float4 quaternion_conjugate(const float4& q) {
            float4 conjugate;
            conjugate.x = -q.x;
            conjugate.y = -q.y;
            conjugate.z = -q.z;
            conjugate.w = q.w;
            return conjugate;
        }

        // Function to express rotation quaternion q1 with respect to q2
        inline float4 express_rotation_with_respect_to(const float4& q1, const float4& q2) {
            // Step 1: Compute intermediate quaternion product
            float4 q_intermediate = quaternion_multiply(q1, quaternion_conjugate(q2));
             return q_intermediate;

        }



        inline  bool EQ_INT2(const float2& a, const float2& b) {
            return (int)a.x == (int)b.x && (int)a.y == (int)b.y;
        }

        inline float3 ADD_F3_F3(const float3& a, const float3& b) {
            return { a.x + b.x , a.y + b.y , a.z + b.z };
        }

        inline float3 SUB_F3_F3(const float3& a, const float3& b) {
            return { a.x - b.x , a.y - b.y , a.z - b.z };
        }

        inline float4 SUB_F4_F4(const float4& a, const float4& b) {
            return { a.x - b.x , a.y - b.y , a.z - b.z, a.w - b.w };
        }

        inline float2 ADD_F2_F2(const float2& a, const float2& b) {
            return { a.x + b.x , a.y + b.y };
        }

        inline float3 MULT_F3_F3(const float3& a, const float3& b) {
            return { a.x * b.x , a.y * b.y , a.z * b.z };
        }

        inline float DOT_F3_F3(const float3& a, const float3& b) {
            return { a.x * b.x + a.y * b.y + a.z * b.z };
        }

        inline float3 DIV_F3_F3(const float3& a, const float3& b) {
            return { a.x / b.x , a.y / b.y , a.z / b.z };
        }

        inline float4 MULT_F4_F(const float4& a, const float& b) {
            __m128 a_vec = _mm_loadu_ps(&a.x);
            __m128 b_vec = _mm_set1_ps(b);
            __m128 result_vec = _mm_mul_ps(a_vec, b_vec);
            float4 result;
            _mm_storeu_ps(&result.x, result_vec);
            return result;
        }

        inline float3 MULT_F3_F(const float3& a, const float& b) {
            float4 r = MULT_F4_F({ a.x, a.y, a.z, 0.0f }, b);
            return { r.x, r.y, r.z };
        }

        inline float2 MULT_F2_F(const float2& a, const float& b) {
            float4 r = MULT_F4_F({ a.x, a.y, 0.0f, 0.0f }, b);
            return { r.x, r.y };
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

        inline float distance_point_line(float3 p0, float3 line_endpoint1, float3 line_endpoint0) {
            float3 line_direction = line_endpoint1 - line_endpoint0;
            float3 point_direction = p0 - line_endpoint0;

            float t = DOT_F3_F3(point_direction, line_direction) / DOT_F3_F3(line_direction, line_direction);

            float3 distance_vector;

            if (t < 0.0f)
                distance_vector = point_direction;
            else if (t > 1.0f)
                distance_vector = p0 - line_endpoint1;
            else
                distance_vector = SUB_F3_F3(point_direction, MULT_F3_F(line_direction, t));

            return std::sqrt(distance_vector.x * distance_vector.x +
                distance_vector.y * distance_vector.y +
                distance_vector.z * distance_vector.z);
        }
	}
}