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

#include "FBXUtil.h"
#include <DirectXMath.h>

using namespace HotBite::Engine;

namespace HotBite {
	namespace Engine {
		namespace FBX {

			FbxAnimCurve* getCurve(FbxPropertyT<FbxDouble3>& prop, FbxAnimLayer* animLayer, const char* pChannel)
			{
				return prop.GetCurve(animLayer, pChannel);
			}

			FbxAMatrix getMatrix(const float4x4& m) {
				FbxAMatrix ret;
				ret[0][0] = m.m[0][0]; ret[0][1] = m.m[0][1]; ret[0][2] = m.m[0][2]; ret[0][3] = m.m[0][3];
				ret[1][0] = m.m[1][0]; ret[1][1] = m.m[1][1]; ret[1][2] = m.m[1][2]; ret[1][3] = m.m[1][3];
				ret[2][0] = m.m[2][0]; ret[2][1] = m.m[2][1]; ret[2][2] = m.m[2][2]; ret[2][3] = m.m[2][3];
				ret[3][0] = m.m[3][0]; ret[3][1] = m.m[3][1]; ret[3][2] = m.m[3][2]; ret[3][3] = m.m[3][3];
				return ret;
			}

			float4x4 getMatrix(const FbxAMatrix& m) {
				return float4x4(
					(float)m.GetRow(0)[0], (float)m.GetRow(0)[1], (float)m.GetRow(0)[2], (float)m.GetRow(0)[3],
					(float)m.GetRow(1)[0], (float)m.GetRow(1)[1], (float)m.GetRow(1)[2], (float)m.GetRow(1)[3],
					(float)m.GetRow(2)[0], (float)m.GetRow(2)[1], (float)m.GetRow(2)[2], (float)m.GetRow(2)[3],
					(float)m.GetRow(3)[0], (float)m.GetRow(3)[1], (float)m.GetRow(3)[2], (float)m.GetRow(3)[3]);
			}

			FbxAMatrix getGeometryTransformation(FbxNode* inNode)
			{
				if (!inNode)
				{
					throw std::exception("Null for mesh geometry");
				}

				const FbxVector4 lT = inNode->GetGeometricTranslation(FbxNode::eSourcePivot);
				const FbxVector4 lR = inNode->GetGeometricRotation(FbxNode::eSourcePivot);
				const FbxVector4 lS = inNode->GetGeometricScaling(FbxNode::eSourcePivot);

				return FbxAMatrix(lT, lR, lS);
			}
		}
	}
}