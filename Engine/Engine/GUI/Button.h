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
#include "InteractiveWidget.h"
#include <Systems/AudioSystem.h>
#include <Core/Utils.h>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace HotBite {
	namespace Engine {
		namespace UI {

			class Button : public Label, public ECS::EventListener {
			private:
				std::shared_ptr<InteractiveWidget> interactive;

			public:
				static inline uint64_t BUTTON_WIDGET_ID = GetTypeId<Button>();
				ID3D11ShaderResourceView* click_image = nullptr;
				ID3D11ShaderResourceView* idle_image = nullptr;
				ID3D11ShaderResourceView* hover_image = nullptr;
				
				Systems::AudioSystem::SoundId click_sound_id = Systems::AudioSystem::INVALID_SOUND_ID;
				Systems::AudioSystem::SoundId hover_sound_id = Systems::AudioSystem::INVALID_SOUND_ID;;

				std::string click_sound;
				std::string hover_sound;

				void SetupListeners(ECS::Coordinator* c) {
					Init(c);
					interactive = std::make_shared<UI::InteractiveWidget>(this);
					AddEventListenerByEntity(UI::InteractiveWidget::EVENT_ID_MOUSE_LDOWN,
						GetId(),
						std::bind(&Button::OnLDown, this, std::placeholders::_1));
					AddEventListenerByEntity(UI::InteractiveWidget::EVENT_ID_MOUSE_LUP,
						GetId(),
						std::bind(&Button::OnLUp, this, std::placeholders::_1));
					AddEventListenerByEntity(UI::InteractiveWidget::EVENT_ID_HOVER_START,
						GetId(),
						std::bind(&Button::OnHoverStart, this, std::placeholders::_1));
					AddEventListenerByEntity(UI::InteractiveWidget::EVENT_ID_HOVER_END,
						GetId(),
						std::bind(&Button::OnLUp, this, std::placeholders::_1));
				}

			public:

				virtual uint64_t GetType() override {
					return BUTTON_WIDGET_ID;
				}

				Button(ECS::Coordinator* c, const std::string& name, const std::string& txt = "", float size = 0.0f,
					const std::string& fname = "", DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE s = DWRITE_FONT_STYLE_NORMAL,
					DWRITE_TEXT_ALIGNMENT ha = DWRITE_TEXT_ALIGNMENT_LEADING,
					DWRITE_PARAGRAPH_ALIGNMENT va = DWRITE_PARAGRAPH_ALIGNMENT_CENTER) : Label(c, name, txt, size, fname, w, s, ha, va) {
					SetupListeners(c);
				}

				Button(ECS::Coordinator* c, const json& config, const std::string& root) : Label(c, config, root) {
					SetupListeners(c);
					SetPressedImage(config["click_image"]);
					SetIdleImage(config["idle_image"]);
					SetHoverImage(config["hover_image"]);
					SetClickSound(config["click_sound"]);
					SetHoverSound(config["hover_sound"]);
				}

				virtual ~Button() {
					coordinator->GetSystem<Systems::AudioSystem>()->RemoveSound(click_sound_id);
				}

				virtual void OnLDown(ECS::Event& ev) {
					if (click_image != nullptr) {
						background_image = click_image;
					}
					coordinator->GetSystem<Systems::AudioSystem>()->Play(click_sound_id);
				}

				virtual void OnLUp(ECS::Event& ev) {
					if (interactive->IsHover() && hover_image != nullptr) {
						background_image = hover_image;
					}
					else {
						background_image = idle_image;
					}
				}

				virtual void OnHoverStart(ECS::Event& ev) {
					background_image = hover_image;
					coordinator->GetSystem<Systems::AudioSystem>()->Play(hover_sound_id);
				}

				virtual void SetClickSound(const std::string& sound) {
					auto audio_system = coordinator->GetSystem<Systems::AudioSystem>();					
					audio_system->RemoveSound(click_sound_id);
					click_sound = root + "\\" + sound;
					if (!click_sound.empty()) {
						click_sound_id = audio_system->LoadSound(click_sound, Core::GetId<Button, Systems::AudioSystem::SoundId>(this, 0x01)).value_or(Systems::AudioSystem::INVALID_SOUND_ID);
					}
					else {
						click_sound_id = Systems::AudioSystem::INVALID_SOUND_ID;
					}					
				}

				virtual void SetHoverSound(const std::string& sound) {
					auto audio_system = coordinator->GetSystem<Systems::AudioSystem>();
					audio_system->RemoveSound(hover_sound_id);
					hover_sound = root + "\\" + sound;
					if (!hover_sound.empty()) {
						hover_sound_id = audio_system->LoadSound(hover_sound, Core::GetId<Button, Systems::AudioSystem::SoundId>(this, 0x02)).value_or(Systems::AudioSystem::INVALID_SOUND_ID);
					}
					else {
						hover_sound_id = Systems::AudioSystem::INVALID_SOUND_ID;
					}
				}

				virtual void SetIdleImage(const std::string& filename) {
					std::string file = root + "\\" + filename;
					Widget::SetBackGroundImage(file);
					idle_image = background_image;
				}

				virtual void SetHoverImage(const std::string& filename) {
					if (hover_image != nullptr) {
						hover_image->Release();
					}
					std::string file = root + "\\" + filename;
					hover_image = LoadTexture(file);
				}

				virtual void SetPressedImage(const std::string& filename) {
					if (click_image != nullptr) {
						click_image->Release();
					}
					std::string file = root + "\\" + filename;
					click_image = LoadTexture(file);
				}
			};
		}
	}
}