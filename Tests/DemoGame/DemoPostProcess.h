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

#include <ECS/Coordinator.h>
#include <Core/PostProcess.h>

class PostGame : public HotBite::Engine::Core::PostProcess, public HotBite::Engine::ECS::IEventSender
{
private:
	HotBite::Engine::ECS::Coordinator* coordinator = nullptr;
	HotBite::Engine::Core::RenderTexture2D text;

	//This is pure abstract from HotBite::Engine::Core::PostProcess to clean internal process texture
	virtual void ClearData(const float color[4]);

public:
	PostGame(ID3D11DeviceContext* dxcontext,
		int width, int height, HotBite::Engine::ECS::Coordinator* c);
	virtual ~PostGame();

	virtual ID3D11ShaderResourceView* RenderResource() override;
	virtual ID3D11RenderTargetView* RenderTarget() const override;
};
