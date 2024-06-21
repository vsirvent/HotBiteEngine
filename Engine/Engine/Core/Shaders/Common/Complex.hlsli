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

#include "Defines.hlsli"
#include "Utils.hlsli"
//#define TEST
struct complex {
    float real;
    float img;
};

struct complex_color {
    complex r;
    complex g;
    complex b;
};

complex MultComplex(const complex n0, const complex n1) {
    complex ret;
    ret.real = n0.real * n1.real - n0.img * n1.img;
    ret.img = n0.real * n1.img + n0.img * n1.real;
    return ret;
}

void AccMultComplex(const complex n0, const complex n1, in out complex val) {
    val.real += n0.real * n1.real - n0.img * n1.img;
    val.img += n0.real * n1.img + n0.img * n1.real;
}

void AccMultComplexColor(const complex_color in_color, const complex value, in out complex_color ccolor) {
    AccMultComplex(in_color.r, value, ccolor.r);
    AccMultComplex(in_color.g, value, ccolor.g);
    AccMultComplex(in_color.b, value, ccolor.b);
}

void PackedComplexColorToComplexColor(float4 packed_color, in out complex_color ccolor) {
    float4 c1;
    float4 c2;
    UnpackColors(packed_color, c1, c2);
    ccolor.r.real = c1.r;
    ccolor.g.real = c1.g;
    ccolor.b.real = c1.b;
    ccolor.r.img = c2.r;
    ccolor.g.img = c2.g;
    ccolor.b.img = c2.b;
}

void ColorToComplexColor(float4 color, out complex_color ccolor) {
    ccolor.r.real = color.r;
    ccolor.g.real = color.g;
    ccolor.b.real = color.b;
    ccolor.r.img = 0.0f;
    ccolor.g.img = 0.0f;
    ccolor.b.img = 0.0f;
}

float Energy(const complex value) {
    return (value.real * value.real + value.img * value.img);
}
float Length(const complex value) {
    return sqrt(Energy(value));
}

float ComplexToReal(const complex in_value) {
    return in_value.real + in_value.img * 2.0f;
}

float ComplexToReal(const complex in_value, uint type) {
#ifdef TEST
    if (type == 0)
        return in_value.real;
    if (type == 1)
        return in_value.img;
    if (type == 2)
        return in_value.real + in_value.img;
    return 0.0f;
#else
    return ComplexToReal(in_value);
#endif
}

float4 ComplexColorToColor(const complex_color ccolor, uint type) {
    return float4(ComplexToReal(ccolor.r, type), ComplexToReal(ccolor.g, type), ComplexToReal(ccolor.b, type), 1.0f);
}

void InitComplexColor(out complex_color ccolor) {
    ccolor.r.real = 0.0f;
    ccolor.g.real = 0.0f;
    ccolor.b.real = 0.0f;
    ccolor.r.img = 0.0f;
    ccolor.g.img = 0.0f;
    ccolor.b.img = 0.0f;
}

float4 GetReal(const complex_color ccolor) {
    return float4(ccolor.r.real, ccolor.g.real, ccolor.b.real, 1.0f);
}

float4 GetImg(const complex_color ccolor) {
    return float4(ccolor.r.img, ccolor.g.img, ccolor.b.img, 1.0f);
}
