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

#include "Defines.h"
#include <sstream>

namespace HotBite {
    namespace Engine {

        bool operator==(const float4& a, const float4& b) {
            return (a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w);
        }

        bool operator==(const float3& a, const float3& b) {
            return (a.x == b.x && a.y == b.y && a.z == b.z);
        }

        bool operator==(const float2& a, const float2& b) {
            return (a.x == b.x && a.y == b.y);
        }

        bool operator!=(const float4& a, const float4& b) {
            return !(a == b);
        }

        bool operator!=(const float3& a, const float3& b) {
            return !(a == b);
        }
        
        bool operator!=(const float2& a, const float2& b) {
            return !(a == b);
        }

        float3 operator-(const float3& a, const float3& b) {
            return { a.x - b.x, a.y - b.y, a.z - b.z };
        }

        float3 operator-(const float3& a, float b) {
            return { a.x - b, a.y - b, a.z - b };

        }
        float2 operator-(const float2& a, const float2& b) {
            return { a.x - b.x, a.y - b.y };
        }

        float3 parseFloat3orDefault(const std::string& input, const float3& default_value) {
            std::string cleanedInput = input;
            cleanedInput.erase(std::remove(cleanedInput.begin(), cleanedInput.end(), '{'), cleanedInput.end());
            cleanedInput.erase(std::remove(cleanedInput.begin(), cleanedInput.end(), '}'), cleanedInput.end());

            // Split the cleaned string by commas and parse the float values
            std::stringstream ss(cleanedInput);
            std::string item;
            float values[3];
            int index = 0;

            while (std::getline(ss, item, ',')) {
                if (index < 3) {
                    values[index++] = std::stof(item);
                }
                else {
                    return default_value;
                }
            }

            if (index != 3) {
                return default_value;
            }
            return { values[0], values[1], values[2] };
        }
    }
}