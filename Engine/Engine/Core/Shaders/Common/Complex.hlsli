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

complex InitComplex(float real, float img) {
    complex c;
    c.real = real;
    c.img = img;
    return c;
}

complex InitComplex() {
    complex c;
    c.real = 0.0f;
    c.img = 0.0f;
    return c;
}

complex AddComplex(const complex n0, const complex n1) {
    complex ret;
    ret.real = n0.real + n1.real;
    ret.img = n0.img + n1.img;
    return ret;
}

complex SubsComplex(const complex n0, const complex n1) {
    complex ret;
    ret.real = n0.real - n1.real;
    ret.img = n0.img - n1.img;
    return ret;
}

complex MultComplex(const complex n0, const complex n1) {
    complex ret;
    ret.real = n0.real * n1.real - n0.img * n1.img;
    ret.img = n0.real * n1.img + n0.img * n1.real;
    return ret;
}

complex ScaleComplex(const complex n0, const float n1) {
    complex ret;
    ret.real = n0.real * n1;
    ret.img = n0.img * n1;
    return ret;
}

complex PowComplex(const complex n0) {

    return MultComplex(n0, n0);
}

void AccMultComplex(const complex n0, const complex n1, in out complex val) {
    val.real += n0.real * n1.real - n0.img * n1.img;
    val.img += n0.real * n1.img + n0.img * n1.real;
}

complex_color MultComplexColor(const complex_color in_color, const complex value) {
    complex_color ret;
    ret.r = MultComplex(in_color.r, value);
    ret.g = MultComplex(in_color.g, value);
    ret.b = MultComplex(in_color.b, value);
    return ret;
}

complex_color ScaleComplexColor(const complex_color in_color, const float value) {
    complex_color ret;
    ret.r = ScaleComplex(in_color.r, value);
    ret.g = ScaleComplex(in_color.g, value);
    ret.b = ScaleComplex(in_color.b, value);
    return ret;
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

void PackedComplex2ColorToComplexColor(float4 packed_color, out complex_color ccolor1, out complex_color ccolor2) {
    float4 c1, c2, c3, c4;
    Unpack4Colors(packed_color, c1, c2, c3, c4);
    ccolor1.r.real = c1.r;
    ccolor1.g.real = c1.g;
    ccolor1.b.real = c1.b;
    ccolor1.r.img = c2.r;
    ccolor1.g.img = c2.g;
    ccolor1.b.img = c2.b;
    ccolor2.r.real = c3.r;
    ccolor2.g.real = c3.g;
    ccolor2.b.real = c3.b;
    ccolor2.r.img = c4.r;
    ccolor2.g.img = c4.g;
    ccolor2.b.img = c4.b;
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

float4 ComplexColorToColor(const complex_color ccolor) {
    return float4(ComplexToReal(ccolor.r), ComplexToReal(ccolor.g), ComplexToReal(ccolor.b), 1.0f);
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
