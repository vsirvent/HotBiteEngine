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

#include "Label.h"
#include <list>
#include <Core\DXCore.h>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace HotBite {
	namespace Engine {
		namespace UI {

			class InteractiveWidget : public ECS::IEventSender, public ECS::EventListener {
			
			public:
				static inline ECS::EventId EVENT_ID_MOUSE_MOVE = ECS::GetEventId<InteractiveWidget>(0x00);
				static inline ECS::EventId EVENT_ID_MOUSE_WHEEL = ECS::GetEventId<InteractiveWidget>(0x01);
				static inline ECS::EventId EVENT_ID_MOUSE_LDOWN = ECS::GetEventId<InteractiveWidget>(0x02);
				static inline ECS::EventId EVENT_ID_MOUSE_LUP = ECS::GetEventId<InteractiveWidget>(0x03);
				static inline ECS::EventId EVENT_ID_MOUSE_MDOWN = ECS::GetEventId<InteractiveWidget>(0x04);
				static inline ECS::EventId EVENT_ID_MOUSE_MUP = ECS::GetEventId<InteractiveWidget>(0x05);
				static inline ECS::EventId EVENT_ID_MOUSE_RDOWN = ECS::GetEventId<InteractiveWidget>(0x06);
				static inline ECS::EventId EVENT_ID_MOUSE_RUP = ECS::GetEventId<InteractiveWidget>(0x07);
				static inline ECS::EventId EVENT_ID_DETROY = ECS::GetEventId<InteractiveWidget>(0x08);
				static inline ECS::EventId EVENT_ID_FOCUS = ECS::GetEventId<InteractiveWidget>(0x09);

			protected:

				Widget* widget = nullptr;

				bool Inside(const float2& coords) {
					float w = (float)DXCore::Get()->GetWidth();
					float h = (float)DXCore::Get()->GetHeight();
					float2 p = { coords.x / w * 2.0f - 1.0f,  (1.0f - coords.y / h) * 2.0f - 1.0f };
					return ((p.x >= widget->position.x && p.x <= widget->position.x + widget->width) &&
						    (p.y >= widget->position.y && p.y <= widget->position.y + widget->height));
				}

				virtual void OnMouseEvent(ECS::Event& ev) {
					if (widget->visible) {
						float2 coords;
						coords.x = (float)ev.GetParam<int>(DXCore::PARAM_ID_X);
						coords.y = (float)ev.GetParam<int>(DXCore::PARAM_ID_Y);
						if (Inside(coords)) {
							ECS::Event e = ev;
							float w = (float)DXCore::Get()->GetWidth();
							float h = (float)DXCore::Get()->GetHeight();
							float2 p = { coords.x / w * 2.0f - 1.0f,  (1.0f - coords.y / h) * 2.0f - 1.0f };
							float2 relative_coords = { (p.x - widget->position.x) / widget->width, (p.y - widget->position.y) / widget->height };
							e.SetSender(this);
							e.SetEntity(widget->id);
							e.SetParam<float>(DXCore::PARAM_RELATIVE_ID_X, 1.0f - relative_coords.x);
							e.SetParam<float>(DXCore::PARAM_RELATIVE_ID_Y, 1.0f - relative_coords.y);
							e.SetType(ECS::GetEventId<InteractiveWidget>(ev.GetType()));
							widget->coordinator->SendEvent(e);
						}
					}
				}
								
			public:
	
				InteractiveWidget(Widget* parent_widget):
					widget(parent_widget)
				{					
					EventListener::Init(widget->coordinator);
					AddEventListener(DXCore::EVENT_ID_MOUSE_LDOWN, std::bind(&InteractiveWidget::OnMouseEvent, this, std::placeholders::_1));
					AddEventListener(DXCore::EVENT_ID_MOUSE_LUP, std::bind(&InteractiveWidget::OnMouseEvent, this, std::placeholders::_1));
					AddEventListener(DXCore::EVENT_ID_MOUSE_MDOWN, std::bind(&InteractiveWidget::OnMouseEvent, this, std::placeholders::_1));
					AddEventListener(DXCore::EVENT_ID_MOUSE_MUP, std::bind(&InteractiveWidget::OnMouseEvent, this, std::placeholders::_1));
					AddEventListener(DXCore::EVENT_ID_MOUSE_RDOWN, std::bind(&InteractiveWidget::OnMouseEvent, this, std::placeholders::_1));
					AddEventListener(DXCore::EVENT_ID_MOUSE_RUP, std::bind(&InteractiveWidget::OnMouseEvent, this, std::placeholders::_1));
					AddEventListener(DXCore::EVENT_ID_MOUSE_MOVE, std::bind(&InteractiveWidget::OnMouseEvent, this, std::placeholders::_1));
				}
			};			
		}
	}
}
