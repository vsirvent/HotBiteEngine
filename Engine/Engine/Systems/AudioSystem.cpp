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

AudioSystem::PlayInfo::PlayInfo(const AudioClip* _clip, float _speed, float _volume, bool _loop, bool _offset, ECS::Entity _entity, const float3& _pos_offset):
	clip(_clip), speed(_speed), volume(_volume), loop(_loop), offset(_offset), entity(_entity), pos_offset(_pos_offset) {
}

bool AudioSystem::Config(const std::string& root_folder, const nlohmann::json& config) {
	bool ret = true;
	Reset();
	auto& clips = config["clips"];

	//Load audio clips
	for (const auto& c : clips) {
		if (!LoadSound(root_folder + std::string(c["file"]), c["id"])) {
			ret = false;
		}
	}
	return ret;
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
	if ((entity_signature & bound_signature) == bound_signature)
	{
		bound_entities[entity] = { coordinator, entity };
	}
	else
	{
		bound_entities.erase(entity);
	}
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
	buffer = new int16_t[Core::SoundDevice::BUFFER_SAMPLES];
	audio_scheduler = Core::Scheduler::Get(Core::DXCore::AUDIO_THREAD);
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
	Stop();
	physics_worker_end = true;
	physics_worker.join();	
	delete sound;
	delete[] buffer;
}

void AudioSystem::OnLocalTransformChanged(ECS::Event& ev) {
	AutoLock l(lock);
	CalculateMicPositions();
}

void AudioSystem::Start() {
	if (audio_timer == Scheduler::INVALID_TIMER_ID) {
		audio_timer = audio_scheduler->RegisterTimer(MSEC_TO_NSEC(Core::SoundDevice::AUDIO_PERIOD_MS), std::bind(&AudioSystem::OnTick, this, std::placeholders::_1));
		sound->Run();
	}	
}

void AudioSystem::Stop() {
	if (audio_timer != Scheduler::INVALID_TIMER_ID) {
		audio_scheduler->RemoveTimer(audio_timer);
		audio_timer = Scheduler::INVALID_TIMER_ID;
		sound->Stop();
	}
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
	physics.distance = sqrt(d2);
	if (d2 < 1.0) {
		d2 = 1.0;
	}
	physics.dist_attenuation = (1.0 / d2)*A + physics.dist_attenuation*B;
	
	//Calculate delay	
	double time_delay = physics.distance / sound_speed;
	physics.offset = (float)time_delay * (float)Core::SoundDevice::FREQ* (float)Core::SoundDevice::CHANNELS;
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
	double attLeft = MIN_ATT + (MAX_ATT - MIN_ATT) * ((diffLeft + mic_distance) / (2.0 * mic_distance));
	double attRight = MIN_ATT + (MAX_ATT - MIN_ATT) * ((-diffLeft + mic_distance) / (2.0 * mic_distance));
	info->physics[EMic::LEFT].angle_attenuation = attLeft * A + info->physics[EMic::LEFT].angle_attenuation * B;	
	info->physics[EMic::RIGHT].angle_attenuation = attRight * A + info->physics[EMic::RIGHT].angle_attenuation * B;

	info->physics[EMic::LEFT].lock.unlock();
	info->physics[EMic::RIGHT].lock.unlock();
}

void AudioSystem::CalculatePointPhysics(PlayInfoPtr info, const TransformEntity& e, EMic channel) {
	if (LENGHT_SQUARE_F3(info->pos_offset) < 0.01f) {
		CalculatePointPhysics(info, e.transform->position, channel);
	}
	else {
		float3 fpos;
		vector3d pos = XMLoadFloat3(&info->pos_offset);

		vector3d tv, rv, sv;
		XMMatrixDecompose(&sv, &rv, &tv, e.transform->world_xmmatrix);
		matrix t = XMMatrixTranslationFromVector(tv);
		matrix r = XMMatrixRotationQuaternion(rv);
		matrix local_matrix = r * t;
		pos = XMVector3Transform(pos, local_matrix);
		XMStoreFloat3(&fpos, pos);
		CalculatePointPhysics(info, fpos, channel);
	}
}

void AudioSystem::CalculateCubePhysics(PlayInfoPtr info, const BoundsEntity& e, EMic channel) {
	//Sanity check of the cube, it can happen to only transform is updated by game
	if (LENGHT_SQUARE_F3(SUB_F3_F3(e.transform->position, { e.bounds->final_box.Center.x, e.bounds->final_box.Center.y, e.bounds->final_box.Center.z })) > 0.1f) {
		return CalculatePointPhysics(info, e.transform->position, channel);
	}

	//Check if the mic is inside the box	
	mic_lock.lock();
	float3 mic_pos = mic_positions[channel];
	mic_lock.unlock();

	bool in_box = (mic_pos.x >= (e.bounds->final_box.Center.x - e.bounds->final_box.Extents.x)) && (mic_pos.x <= (e.bounds->final_box.Center.x + e.bounds->final_box.Extents.x)) &&
		(mic_pos.y >= (e.bounds->final_box.Center.y - e.bounds->final_box.Extents.y)) && (mic_pos.y <= (e.bounds->final_box.Center.y + e.bounds->final_box.Extents.y)) &&
		(mic_pos.z >= (e.bounds->final_box.Center.z - e.bounds->final_box.Extents.z)) && (mic_pos.z <= (e.bounds->final_box.Center.z + e.bounds->final_box.Extents.z));
	
	if (in_box) {
		CalculatePointPhysics(info, mic_pos, channel);
	}
	else {
		// Transform the point into the local space of the box
		// Calculate the distance along each axis
		float3 v{
			max(max(e.bounds->final_box.Center.x - e.bounds->final_box.Extents.x - mic_pos.x, 0.0f), mic_pos.x - (e.bounds->final_box.Center.x + e.bounds->final_box.Extents.x)),
			max(max(e.bounds->final_box.Center.y - e.bounds->final_box.Extents.y - mic_pos.y, 0.0f), mic_pos.y - (e.bounds->final_box.Center.y + e.bounds->final_box.Extents.y)),
			max(max(e.bounds->final_box.Center.z - e.bounds->final_box.Extents.z - mic_pos.z, 0.0f), mic_pos.z - (e.bounds->final_box.Center.z + e.bounds->final_box.Extents.z)) 
		};

		//Distance square from local camera to point
		auto& physics = info->physics[channel];
		double d2 = (double)LENGHT_SQUARE_F3(v);
		physics.lock.lock();
		physics.distance = sqrt(d2);
		if (d2 < 1.0) {
			d2 = 1.0;
		}
		physics.dist_attenuation = (1.0 / d2) * A + physics.dist_attenuation * B;

		//Calculate delay
		static const double sound_speed = 343.0;
		double time_delay = physics.distance / sound_speed;
		physics.offset = (float)time_delay * (float)Core::SoundDevice::FREQ * (float)Core::SoundDevice::CHANNELS;
		physics.lock.unlock();
	}
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
			p.offset = 0.0f;
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
	
	float size = 0;
	static constexpr float OFFSET_DELTA = 0.1f;
	static constexpr float OFFSET_MAX = 20000.0f;
	static constexpr float OFFSET_HIST = OFFSET_DELTA * 50.0f;
	bool physic_sound = false;
	for (int i = 0; i < Core::SoundDevice::BUFFER_SAMPLES; ++i) {

		if (i % 2 == 0) {
			lcount++;
			channel = EMic::LEFT;
		}
		else {
			rcount++;
			channel = EMic::RIGHT;
		}
		
		if (local_entity.transform != nullptr) {
			if (i % 50 == 0) {
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
			size = (float)it->second->clip->mono_data.size();
			physic_sound = local_entity.transform != nullptr && it->second->entity != INVALID_ENTITY_ID;
			float pos = it->second->fpos;
			double att = it->second->volume;
			if (physic_sound) {
				//Calculate audio physics 
				auto& physics = it->second->physics[mic];

				float offset_diff = fabs(physics.offset - physics.current_offset);
				physics.lock.lock();
				if (it->second->offset) {
					if (physics.init == false || offset_diff > OFFSET_MAX) {
						physics.init = true;
						physics.current_offset = physics.offset;
					}
					else {
						//Check offset histeresys
						if (physics.offset_type == EOffsetType::OFFSET_INC) {
							if ((physics.current_offset - physics.offset) > OFFSET_HIST) {
								physics.offset_type = OFFSET_DEC;
							}
						}
						else if (physics.offset_type == EOffsetType::OFFSET_DEC) {
							if ((physics.offset - physics.current_offset) > OFFSET_HIST) {
								physics.offset_type = OFFSET_INC;
							}
						}
						else {
							if (physics.offset > physics.current_offset) {
								physics.offset_type = OFFSET_INC;
							}
							else if (physics.offset < physics.current_offset) {
								physics.offset_type = OFFSET_DEC;
							}
						}
						if (physics.offset_type == EOffsetType::OFFSET_INC && physics.offset > physics.current_offset) {
							physics.current_offset += OFFSET_DELTA;
						}
						else if (physics.offset_type == EOffsetType::OFFSET_DEC && physics.offset < physics.current_offset) {
							physics.current_offset -= OFFSET_DELTA;
						}
					}
					pos -= physics.current_offset;
				}
				att *= physics.dist_attenuation * physics.angle_attenuation;
				physics.lock.unlock();
			}

			if (pos >= size) {
				if (it->second->loop) {
					pos = (float)fmod(pos, size);
				}
			}
			else if (pos < 0.0f) {
				if (it->second->loop) {
					pos += size;
				}
			}

			if (pos >= 0 && pos < size) {	
				double s;
				if (physic_sound && mic == EMic::RIGHT) {
					s = it->second->last_sample[EMic::LEFT];
				} else {
					float p0 = std::floor(pos);
					float p1 = std::ceil(pos);

					float a = pos - p0;
					float b = 1.0f - a;

					int32_t ip0 = (int32_t)p0;
					int32_t ip1 = (int32_t)p1;
					int32_t isize = (int32_t)size;
					if (it->second->loop) {
						if (ip0 < 0) { ip0 += isize; }
						if (ip1 < 0) { ip1 += isize; }
						if (ip0 >= size) { ip0 = ip0 % isize; }
						if (ip1 >= size) { ip1 = ip1 % isize; }
					}
					else {
						if (ip0 < 0) { ip0 = 0; }
						if (ip1 < 0) { ip1 = 0; }
						if (ip0 >= size) { ip0 = isize - 1; }
						if (ip1 >= size) { ip1 = isize - 1; }
					}

					const std::vector<int16_t>* data;
					if (physic_sound) {
						data = &it->second->clip->mono_data;
					}
					else {
						data = (mic == EMic::LEFT) ? &it->second->clip->left_data : &it->second->clip->right_data;
					}

					double s0 = (static_cast<double>((*data)[ip0]));
					double s1 = (static_cast<double>((*data)[ip1]));
					s = (s0 * b + s1 * a);
					//If we simulate sound speed or are playing at a different speed, apply low pass filter to avoid armonics due to freq changes
					if (it->second->offset || it->second->speed != 1.0f) {
						s = (s + it->second->last_sample[mic]) * 0.5f;
					}
					it->second->last_sample[mic] = s;
				}
				s *= att;
				sample += static_cast<int64_t>(s);
			}
			if (mic == EMic::RIGHT) {
				it->second->fpos += it->second->speed;
			}
			if (it->second->fpos >= size) {
				if (it->second->loop) {
					it->second->fpos = 0.0f;
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
	sound->write((BYTE*)buffer, Core::SoundDevice::BUFFER_BYTES);
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
	if (id == INVALID_SOUND_ID) {
		return std::nullopt;
	}
	if (const auto it = audio_by_id.find(id); it != audio_by_id.cend()) {
		printf("Audio %s clip already loaded\n", file.c_str());
		it->second.ref_count++;
		return id;
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
	auto& play_info = audio_by_id[id];
	play_info.id = id;
	play_info.ref_count = 1;
	play_info.left_data.clear();
	play_info.left_data.resize(data.size() / 2);
	play_info.right_data.clear();
	play_info.right_data.resize(data.size() / 2);
	play_info.mono_data.clear();
	play_info.mono_data.resize(data.size() / 2);

	for (int i = 0; i < data.size(); i += 2) {
		play_info.left_data[i / 2] = data[i];
		play_info.right_data[i / 2] = data[i + 1];
		play_info.mono_data[i / 2] = (data[i] + data[i + 1]) / 2;
	}
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

bool AudioSystem::RemoveSound(SoundId id) {
	AutoLock l(lock);
	if (id == INVALID_SOUND_ID) {
		return false;
	}
	
	if (const auto it = audio_by_id.find(id); it != audio_by_id.cend()) {
		if (it->second.ref_count-- == 0) {
			for (auto it2 = playlist.begin(); it2 != playlist.end();) {
				if (it2->second->clip->id == id) {
					it2 = playlist.erase(it2);
				}
				else {
					++it2;
				}
			}
			audio_by_id.erase(it);
			return true;
		}
	}

	return false;
}

AudioSystem::PlayId AudioSystem::Play(SoundId id, int32_t delay_ms, bool loop, float speed, float volume, bool simulate_sound_speed, ECS::Entity entity, const float3& pos_offset) {
	AutoLock l(lock);

	if (id == INVALID_SOUND_ID) {
		return INVALID_PLAY_ID;
	}
	
	const auto it = audio_by_id.find(id);

	//Check if audio loaded
	if (it == audio_by_id.cend()) {
		return false;
	}

	PlayId pid = play_count++;
	PlayInfoPtr play_info = std::make_shared<PlayInfo>(&(it->second), speed, volume, loop, simulate_sound_speed, entity, pos_offset);
	if (delay_ms > 0) {
		//Add the play sound with delay
		audio_scheduler->RegisterTimer(MSEC_TO_NSEC(delay_ms), [this, play_info, pid](const Scheduler::TimerData& td) {
			AutoLock l(lock);
			playlist[pid] = play_info;
			//No repeat
			return false;
		});
	}
	else {
		playlist[pid] = play_info;
	}
	return pid;
}

void AudioSystem::Stop(PlayId id) {
	AutoLock l(lock);
	if (const auto it = playlist.find(id); it != playlist.cend()) {
		playlist.erase(it);
	}
}

std::optional<float> AudioSystem::GetSpeed(PlayId id) const {
	AutoLock l(lock);
	const auto it = playlist.find(id);
	if (it == playlist.cend()) {
		return std::nullopt;
	}
	return it->second->speed;
}

bool AudioSystem::SetSpeed(PlayId id, float speed) {
	AutoLock l(lock);
	auto it = playlist.find(id);
	if (it == playlist.cend()) {
		return false;
	}
	it->second->speed = speed;
	return true;
}

std::optional<float> AudioSystem::GetVol(PlayId id) const {
	AutoLock l(lock);
	const auto it = playlist.find(id);
	if (it == playlist.cend()) {
		return std::nullopt;
	}
	return it->second->volume;
}

bool AudioSystem::SetVol(PlayId id, float vol) {
	AutoLock l(lock);
	auto it = playlist.find(id);
	if (vol < 0.0f || it == playlist.cend()) {
		return false;
	}
	it->second->volume = vol;
	return true;
}

std::optional<bool> AudioSystem::GetLoop(PlayId id) const {
	AutoLock l(lock);
	auto it = playlist.find(id);
	if (it == playlist.cend()) {
		return std::nullopt;
	}
	return it->second->loop;
}

bool AudioSystem::SetLoop(PlayId id, bool loop) {
	AutoLock l(lock);
	auto it = playlist.find(id);
	if (it == playlist.cend()) {
		return false;
	}
	it->second->loop = loop;
	return true;
}

std::optional<bool> AudioSystem::GetSimSoundSpeed(PlayId id) const {
	AutoLock l(lock);
	auto it = playlist.find(id);
	if (it == playlist.cend()) {
		return std::nullopt;
	}
	return it->second->offset;
}

bool AudioSystem::SetSimSoundSpeed(PlayId id, bool simulate_sound_speed) {
	AutoLock l(lock);
	auto it = playlist.find(id);
	if (it == playlist.cend()) {
		return false;
	}
	it->second->offset = simulate_sound_speed;
	return true;
}

void AudioSystem::SetMicDistance(float dist_meters) {
	AutoLock l(lock);
	mic_distance = dist_meters;
	relative_mic_position[0] = { mic_distance / 2.0f, 0.0f, 0.0f };
	relative_mic_position[1] = { -mic_distance / 2.0f, 0.0f, 0.0f };
}

double AudioSystem::GetMicDistance() const {
	return mic_distance;
}

void AudioSystem::SetSoundSpeed(double speed_meters_per_sec) {
	sound_speed = speed_meters_per_sec;
}

double AudioSystem::GetSoundSpeed() const {
	return sound_speed;
}
