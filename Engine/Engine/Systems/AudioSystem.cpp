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

#include <Components\Camera.h>
#include <Systems\CameraSystem.h>
#include "DXCore.h"
#include "AudioSystem.h"

#include <assert.h>
#include <iostream>
#include <fstream>
#include <vector>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::Components;
using namespace DirectX;

AudioSystem::PlayInfo::PlayInfo(const AudioClip* _clip, int64_t _delay, float _speed, float _volume, bool _loop, ECS::Entity _entity):
	clip(_clip), delay(_delay), speed(_speed), volume(_volume), loop(_loop), entity(_entity) {
	start = Scheduler::GetNanoSeconds();
}

bool AudioSystem::Config(const std::string& root_folder, const nlohmann::json& config) {
	Reset();
	auto& clips = config["clips"];

	//Load audio clips
	for (const auto& c : clips) {
		if (!LoadSound(root_folder + std::string(c["file"]), c["id"])) {
			return false;
		}
	}
	return true;
}

void AudioSystem::OnRegister(ECS::Coordinator* c) {
	this->coordinator = c;
	transform_signature.set(coordinator->GetComponentType<Transform>(), true);
	transform_signature.set(coordinator->GetComponentType<Base>(), true);
	bound_signature.set(coordinator->GetComponentType<Transform>(), true);
	bound_signature.set(coordinator->GetComponentType<Bounds>(), true);
	bound_signature.set(coordinator->GetComponentType<Base>(), true);	
}

void AudioSystem::OnEntitySignatureChanged(ECS::Entity entity, const Signature& entity_signature) {
	AutoLock l(lock);
	if ((entity_signature & transform_signature) == transform_signature)
	{
		transform_entities[entity] = { coordinator, entity };
	}
	else
	{
		transform_entities.erase(entity);
	}
	/*
	if ((entity_signature & bound_signature) == bound_signature)
	{
		bound_entities[entity] = { coordinator, entity };
	}
	else
	{
		bound_entities.erase(entity);
	}
	*/
}

void AudioSystem::OnEntityDestroyed(ECS::Entity entity) {
	AutoLock l(lock);
	transform_entities.erase(entity);
	bound_entities.erase(entity);
	for (auto it = playlist.begin(); it != playlist.end(); ++it) {
		if (it->second->entity == entity) {
			playlist.erase(it);
			break;
		}
	}
}

AudioSystem::AudioSystem() {
	sound = new SoundDeviceGrabber();
	buffer = new int16_t[BUFFER_SAMPLES];
	audio_scheduler = Core::Scheduler::Get(Core::DXCore::AUDIO_THREAD);
	audio_timer = audio_scheduler->RegisterTimer(MSEC_TO_NSEC(AUDIO_PERIOD_MS), std::bind(&AudioSystem::OnTick, this, std::placeholders::_1));
	vrelative_mic_position[EMic::LEFT] = XMLoadFloat3(&relative_mic_position[EMic::LEFT]);
	vrelative_mic_position[EMic::RIGHT] = XMLoadFloat3(&relative_mic_position[EMic::RIGHT]);

	physics_worker = std::thread([&]() {
			PlayInfoPtr play_info;
			while (!physics_worker_end) {
				if (playinfo_queue.TimedWaitAndPop(play_info, 200) == true) {
					CalculatePhysics(play_info);
				}
			}
		});
}

AudioSystem::~AudioSystem() {
	physics_worker_end = true;
	physics_worker.join();
	audio_scheduler->RemoveTimer(audio_timer);
	sound->Stop();
	delete sound;
	delete[] buffer;
}

void AudioSystem::OnLocalTransformChanged(ECS::Event& ev) {
	AutoLock l(lock);
	CalculateMicPositions();
}

void AudioSystem::SetCameraEntity(ECS::Entity entity) {
	AutoLock l(lock);
	if (camera_entity_event_id.e != INVALID_ENTITY_ID) {
		coordinator->RemoveEventListener(camera_entity_event_id);
	}
	if (entity != INVALID_ENTITY_ID) {
		camera_entity = { coordinator, entity };
		CalculateMicPositions();
		camera_entity_event_id = coordinator->AddEventListener(CameraSystem::EVENT_ID_CAMERA_MOVED, std::bind(&AudioSystem::OnLocalTransformChanged, this, std::placeholders::_1));
	}
	else {
		camera_entity_event_id = EventListenerId();
		camera_entity = {};
	}
}

void AudioSystem::SetLocalEntity(ECS::Entity entity, const float3& offset) {
	AutoLock l(lock);
	if (local_entity_event_id.e != INVALID_ENTITY_ID) {
		coordinator->RemoveEventListener(local_entity_event_id);
	}
	if (entity != INVALID_ENTITY_ID) {
		local_entity = { coordinator, entity };		
		local_offset = XMLoadFloat3(&offset);
		CalculateMicPositions();
		local_entity_event_id = coordinator->AddEventListenerByEntity(Transform::EVENT_ID_TRANSFORM_CHANGED, entity, std::bind(&AudioSystem::OnLocalTransformChanged, this, std::placeholders::_1));
	}
	else {
		local_entity_event_id = EventListenerId();
		local_entity = {};
	}
}

void AudioSystem::CalculateMicPositions() {
	if (local_entity.base == nullptr) {
		return;
	}	
	vector3d tv, rv, sv;
	XMMatrixDecompose(&sv, &rv, &tv, local_entity.transform->world_xmmatrix);
	tv += local_offset;
	matrix t = XMMatrixTranslationFromVector(tv);
	matrix r;

	if (camera_entity.camera != nullptr) {
		r = XMMatrixRotationQuaternion(camera_entity.camera->xm_rotation);
	}
	else {
		r = XMMatrixRotationQuaternion(rv);
	}

	matrix local_matrix = r * t;
	mic_lock.lock();
	vmic_positions[EMic::LEFT] = XMVector3Transform(vrelative_mic_position[EMic::LEFT], local_matrix);
	vmic_positions[EMic::RIGHT] = XMVector3Transform(vrelative_mic_position[EMic::RIGHT], local_matrix);	
	XMStoreFloat3(&mic_positions[EMic::LEFT], vmic_positions[EMic::LEFT]);
	XMStoreFloat3(&mic_positions[EMic::RIGHT], vmic_positions[EMic::RIGHT]);
	mic_lock.unlock();
}

void AudioSystem::CalculatePointPhysics(PlayInfoPtr info, const float3& point, EMic channel) {
	//Distance square from local camera to point
	auto& physics = info->physics[channel];
	mic_lock.lock();
	float3 v = SUB_F3_F3(point, mic_positions[channel]);
	mic_lock.unlock();
	double d2 = (double)LENGHT_SQUARE_F3(v);
	physics.lock.lock();
	d2 = sqrt(d2);
	physics.distance = d2;
	if (d2 < 1.0) {
		d2 = 1.0;
	}
	physics.dist_attenuation = (1.0 / d2)*A + physics.dist_attenuation*B;
	
	//Calculate delay
	static const double sound_speed = 343.0;
	double time_delay = physics.distance / sound_speed;
	physics.offset = (int32_t)(time_delay * (float)Core::SoundDevice::FREQ)* Core::SoundDevice::CHANNELS;
	physics.lock.unlock();
}

void AudioSystem::CalculateAngleAttenuation(PlayInfoPtr info) {
	static constexpr double MIN_ATT = 0.2;
	static constexpr double MAX_ATT = 1.0;

	info->physics[EMic::LEFT].lock.lock();
	info->physics[EMic::RIGHT].lock.lock();

	double dleft = info->physics[EMic::LEFT].distance;
	double dright = info->physics[EMic::RIGHT].distance;
	double diffLeft = dleft - dright;
	double attLeft = MIN_ATT + (MAX_ATT - MIN_ATT) * ((diffLeft + MIC_DISTANCE) / (2.0 * MIC_DISTANCE));
	double attRight = MIN_ATT + (MAX_ATT - MIN_ATT) * ((-diffLeft + MIC_DISTANCE) / (2.0 * MIC_DISTANCE));
	info->physics[EMic::LEFT].angle_attenuation = attLeft * A + info->physics[EMic::LEFT].angle_attenuation*B;
	info->physics[EMic::RIGHT].angle_attenuation = attRight * A + info->physics[EMic::RIGHT].angle_attenuation * B;

	info->physics[EMic::LEFT].lock.unlock();
	info->physics[EMic::RIGHT].lock.unlock();
}

void AudioSystem::CalculatePointPhysics(PlayInfoPtr info, const TransformEntity& e, EMic channel) {
	CalculatePointPhysics(info, e.transform->position, channel);	
}

void AudioSystem::CalculateCubePhysics(PlayInfoPtr info, const BoundsEntity& e, EMic channel) {
	mic_lock.lock();
	// Transform the point into the local space of the box
	vector3d pos = XMVectorSubtract(vmic_positions[channel], XMLoadFloat3(&e.bounds->final_box.Center));
	mic_lock.unlock();
	matrix inverseOrientation = XMMatrixInverse(nullptr, e.transform->world_xmmatrix);
	vector3d localPoint = XMVector3Transform(pos, inverseOrientation);

	// Clamp the point to the box extents
	vector3d clampedPoint = XMVectorClamp(localPoint, XMVectorNegate(XMLoadFloat3(&e.bounds->final_box.Extents)), XMLoadFloat3(&e.bounds->final_box.Extents));

	// Transform the clamped point back to the world space
	vector3d transformedPoint = XMVector3Transform(clampedPoint, e.transform->world_xmmatrix);
	
	float3 entity_point;
	XMStoreFloat3(&entity_point, transformedPoint);

	CalculatePointPhysics(info, entity_point, channel);
}

bool AudioSystem::CalculatePhysics(PlayInfoPtr info) {
	bool ret = true;
	if (local_entity.transform == nullptr || info->entity == INVALID_ENTITY_ID) {
		//No audio physics if we don't have a position in space
		for (int i = 0; i < EMic::NUM_MICS; ++i) {
			auto& p = info->physics[(EMic)i];
			p.lock.lock();
			p.dist_attenuation = 1.0;
			p.angle_attenuation = 1.0;
			p.offset = 0;
			p.lock.unlock();
		}
	} else if (const auto it = bound_entities.find(info->entity); it != bound_entities.cend()) {
		CalculateCubePhysics(info, it->second, EMic::LEFT);
		CalculateCubePhysics(info, it->second, EMic::RIGHT);
		CalculateAngleAttenuation(info);
	} else if (const auto it = transform_entities.find(info->entity); it != transform_entities.end()) {
		CalculatePointPhysics(info, it->second, EMic::LEFT);
		CalculatePointPhysics(info, it->second, EMic::RIGHT);
		CalculateAngleAttenuation(info);
	} else {
		//error, no entity found
		ret = false;
	}
	info->updating = false;
	return ret;
}

bool AudioSystem::OnTick(const Scheduler::TimerData& td)
{
	lock.lock();
	EMic channel;
	int lcount = 0;
	int rcount = 0;
	int64_t sample = 0;
	int32_t pos = 0;
	int32_t size = 0;

	for (int i = 0; i < BUFFER_SAMPLES; ++i) {

		if (i % 2 == 0) {
			lcount++;
			channel = EMic::LEFT;
		}
		else {
			rcount++;
			channel = EMic::RIGHT;
		}
		
		if (local_entity.transform != nullptr) {
			if (i % 40 == 0) {
				for (auto it = playlist.begin(); it != playlist.end(); ++it) {
					if (it->second->updating == false && it->second->entity != INVALID_ENTITY_ID) {
						it->second->updating = true;
						playinfo_queue.Push(it->second);
					}
				}
			}
		}

		sample = 0;
		int mic = i % EMic::NUM_MICS;
		for (auto it = playlist.begin(); it != playlist.end();) {	
			auto& physics = it->second->physics[mic];
			physics.lock.lock();
			pos = it->second->pos++;
			bool update_offset = (channel == EMic::LEFT && (lcount % 5 == 0)) || (channel == EMic::RIGHT && (rcount % 5 == 0));
			if (physics.offset > physics.current_offset && update_offset) {
				physics.current_offset += SoundDevice::CHANNELS;
			}
			else if (physics.offset < physics.current_offset && update_offset) {
				physics.current_offset -= SoundDevice::CHANNELS;
			}
			pos -= physics.current_offset;
			size = (int32_t)it->second->clip->data.size();
			
			//save last position before loop
			if (pos >= size) {
				if (it->second->loop) {
					pos = pos % size;
				}
			}
			else if (pos < 0) {
				if (it->second->loop) {
					pos += size;
				}
			}
			
			it->second->last_pos = pos;

			if (pos >= 0 && pos < size) {
				double att = physics.dist_attenuation * physics.angle_attenuation * it->second->volume;
				double s = (static_cast<double>(it->second->clip->data[pos])) * att;
				sample += static_cast<int64_t>(s);				
			}
			physics.lock.unlock();
			if (it->second->pos >= size) {
				if (it->second->loop) {
					it->second->pos = 0;					
					++it;
				}
				else {
					it = playlist.erase(it);
				}
			}
			else {
				++it;
			}
		}
		if (sample > 0x7fff) {
			sample = 0x7fff;
		}
		else if (sample < -0x7fff) {
			sample = -0x7fff;
		}

		buffer[i] = static_cast<int16_t>(sample);
	}
	lock.unlock();
	sound->write((BYTE*)buffer, BUFFER_BYTES);
	return true;
}

void  AudioSystem::Reset() {
	AutoLock l(lock);
	id_by_name.clear();
	audio_by_id.clear();
	playlist.clear();
}

std::optional<AudioSystem::SoundId> AudioSystem::LoadSound(const std::string& file, SoundId id) {
	AutoLock l(lock);
	if (const auto it = id_by_name.find(file); it != id_by_name.cend()) {
		printf("Audio %s clip already loaded\n", file.c_str());
		return it->second;
	}
	//Load the sound clip
	std::ifstream f(file, std::ios::binary);

	if (!f.is_open()) {
		printf("Error opening file: %s\n", file.c_str());
		return std::nullopt;
	}

	// Determine the size of the file
	f.seekg(0, std::ios::end);
	std::streampos file_size = f.tellg();
	f.seekg(0, std::ios::beg);

	std::vector<int16_t> data(file_size / 2);
	if (file_size % 2 != 0) {
		assert(false && "Audio clip bytes must be even");
		return std::nullopt;
	}

	// Read the raw audio data into the vector
	f.read((char*)data.data(), file_size);

	// Close the file
	f.close();	
	audio_by_id[id] = { std::move(data), id };
	id_by_name[file] = id;
	return id;
}

std::optional<AudioSystem::SoundId> AudioSystem::GetSound(const std::string& file) {
	AutoLock l(lock);
	if (const auto it = id_by_name.find(file); it != id_by_name.cend()) {
		return it->second;
	}
	//Sound not loaded
	printf("Audio %s clip not found\n", file.c_str());
	return std::nullopt;
}

AudioSystem::PlayId AudioSystem::Play(SoundId id, int32_t delay_ms, bool loop, float speed, float volume, ECS::Entity entity) {
	AutoLock l(lock);
	const auto it = audio_by_id.find(id);
	if (it == audio_by_id.cend()) {
		return false;
	}
	playlist[play_count++] = (std::make_shared<PlayInfo>(&(it->second), MSEC_TO_NSEC(delay_ms), speed, volume, loop, entity));
	return play_count;
}

void AudioSystem::Stop(PlayId id) {
	AutoLock l(lock);
	if (const auto it = playlist.find(id); it != playlist.cend()) {
		playlist.erase(it);
	}
}