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

#define FBXSDK_NEW_API

#include <Defines.h>
#include <iostream>
#include <list>
#include <vector>
#include <ctime>
#include <fbxsdk.h>

namespace HotBite {
	namespace Engine {
		namespace FBX {
			FbxAnimCurve* getCurve(FbxPropertyT<FbxDouble3>& prop, FbxAnimLayer* animLayer, const char* pChannel);
			float4x4 getMatrix(const FbxAMatrix& m);
			FbxAMatrix getMatrix(const float4x4& m);
			FbxAMatrix getGeometryTransformation(FbxNode* inNode);
		}
	}
}