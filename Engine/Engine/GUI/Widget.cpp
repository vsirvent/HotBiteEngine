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

#include "Widget.h"

namespace HotBite {
	namespace Engine {
		namespace UI {

			//Calculate required font size depending the screen 
			//resolution and the desired font size in screen percetnage
			float CalculateFontSize(float font_size_percentage) {
				float font_size = 0.0f;
				float screen_width = (float)DXCore::Get()->GetWidth();
				float screen_height = (float)DXCore::Get()->GetHeight();
				font_size_percentage *= 0.001f;

				if (screen_width > screen_height) {
					font_size = screen_height * font_size_percentage;
				}
				else {
					font_size = screen_width * font_size_percentage;
				}

				return font_size;
			}
		}
	}
}