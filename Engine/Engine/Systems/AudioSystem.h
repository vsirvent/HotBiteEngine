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

#include <Core\Audio.h>
#include <Core\Scheduler.h>
#include <Core\Json.h>
#include <Core\SpinLock.h>
#include <Core\LockingQueue.h>

#include <Components\Base.h>
#include <Components\Camera.h>
#include <ECS\System.h>
#include <ECS\EntityVector.h>
#include <ECS\Coordinator.h>

#include <string>
#include <optional>
#include <unordered_map>
#include <mutex>

namespace HotBite {
    namespace Engine {
        namespace Systems {

            class AudioSystem: public ECS::System
            {
            public:
                using SoundId = int32_t;
                using PlayId = int32_t;
                static constexpr SoundId INVALID_SOUND_ID = -1;
                static constexpr PlayId INVALID_PLAY_ID = -1;

                static constexpr double A = 0.1;
                static constexpr double B = 1.0 - A;

            public:
                ECS::Signature transform_signature;
                ECS::Signature bound_signature;
                ECS::Coordinator* coordinator = nullptr;
                PlayId play_count = 0;

                struct TransformEntity {
                    Components::Transform* transform = nullptr;
                    Components::Base* base = nullptr;
                    TransformEntity() = default;
                    TransformEntity(ECS::Coordinator* c, ECS::Entity entity) {
                        transform = &(c->GetComponent<Components::Transform>(entity));
                        base = &(c->GetComponent<Components::Base>(entity));
                    }
                };

                struct CameraEntity: public TransformEntity {
                    Components::Camera* camera = nullptr;
                    CameraEntity() = default;
                    CameraEntity(ECS::Coordinator* c, ECS::Entity entity): TransformEntity(c, entity) {
                        camera = &(c->GetComponent<Components::Camera>(entity));
                    }
                };

                struct BoundsEntity {
                    Components::Transform* transform = nullptr;
                    Components::Bounds* bounds = nullptr;
                    Components::Base* base = nullptr;
                    BoundsEntity() = default;
                    BoundsEntity(ECS::Coordinator* c, ECS::Entity entity) {
                        transform = &(c->GetComponent<Components::Transform>(entity));
                        bounds = &(c->GetComponent<Components::Bounds>(entity));
                        base = &(c->GetComponent<Components::Base>(entity));
                    }
                };

                enum EMic {
                    LEFT,
                    RIGHT,
                    NUM_MICS
                };

                static constexpr double MIC_DISTANCE = 0.5;
                static constexpr double HALF_MIC_DISTANCE = MIC_DISTANCE/2.0; //30 cm
                //relative mic positions
                float3 relative_mic_position[NUM_MICS] = { { HALF_MIC_DISTANCE, 0.0f, 0.0f }, { -HALF_MIC_DISTANCE, 0.0f, 0.0f } };
                vector3d vrelative_mic_position[NUM_MICS];
                //absolute mic positions
                Core::spin_lock mic_lock;
                float3 mic_positions[NUM_MICS]{};
                vector3d vmic_positions[NUM_MICS]{};

                //Transform entities emit sounds from a point in space
                std::unordered_map<ECS::Entity, TransformEntity> transform_entities;
                //Bound entities emit sound from a cube in space
                std::unordered_map<ECS::Entity, BoundsEntity> bound_entities;
                //Camera
                TransformEntity local_entity;
                vector3d local_offset{};
                CameraEntity camera_entity;
                ECS::EventListenerId local_entity_event_id;
                ECS::EventListenerId camera_entity_event_id;

                //Audio clip must be a RAW Little Endian file with 16 bits per sample, 44100 Hz, 2 channels
                //use Audacity to create the audio clips
                struct AudioClip
                {
                    std::vector<int16_t> left_data;
                    std::vector<int16_t> right_data;
                    std::vector<int16_t> mono_data;
                    SoundId id = INVALID_SOUND_ID;
                    //TODO: Add here any audio option (fx, speed, etc...)
                };

                enum EOffsetType {
                    OFFSET_NONE,
                    OFFSET_INC,
                    OFFSET_DEC
                };

                struct AudioPhysics {
                    AudioPhysics(){}
                    AudioPhysics(const AudioPhysics& other) {
                        offset = other.offset;
                        dist_attenuation = other.dist_attenuation;
                        angle_attenuation = other.angle_attenuation;
                        distance = other.distance;
                    }
                    EOffsetType offset_type = OFFSET_NONE;
                    float offset = 0.0f;
                    float current_offset = 0.0f;
                    double dist_attenuation = 0.0;
                    double angle_attenuation = 0.0;
                    double distance = 0.0;
                    double last_sample = 0.0;
                    bool init = false;
                    mutable Core::spin_lock lock;
                };

                struct PlayInfo
                {
                    PlayInfo(const AudioClip* _clip, float _speed, float _volume, bool _loop, bool _offset, ECS::Entity _entity, const float3& pos_offset);
                    const AudioClip* clip = nullptr;
                    AudioPhysics physics[EMic::NUM_MICS];
                    double last_sample[EMic::NUM_MICS] = {};
                    float fpos = 0.0f;
                    float speed = 1.0f;
                    float volume = 1.0f;
                    bool offset = false;
                    bool loop = false;                    
                    ECS::Entity entity = ECS::INVALID_ENTITY_ID;
                    float3 pos_offset = {};
                    std::atomic<bool> updating = false;
                };

                using PlayInfoPtr = std::shared_ptr<PlayInfo>;
                std::unordered_map<std::string, SoundId> id_by_name;
                std::unordered_map<SoundId, AudioClip> audio_by_id;
                std::map<PlayId, PlayInfoPtr> playlist;
                std::thread physics_worker;
                bool physics_worker_end = false;
                Core::LockingQueue<PlayInfoPtr> playinfo_queue;

                int16_t* buffer = nullptr;

                Core::SoundDeviceGrabber* sound = nullptr;
                Core::spin_lock lock;
                Core::Scheduler* audio_scheduler;
                int audio_timer;

                void OnLocalTransformChanged(ECS::Event& ev);
                void CalculateMicPositions();
                void CalculateAngleAttenuation(PlayInfoPtr info);
                void CalculatePointPhysics(PlayInfoPtr info, const float3& point, EMic channel);
                void CalculatePointPhysics(PlayInfoPtr info, const TransformEntity& e, EMic channel);
                void CalculateCubePhysics(PlayInfoPtr info, const BoundsEntity& e, EMic channel);
                bool CalculatePhysics(PlayInfoPtr info);
                bool OnTick(const Core::Scheduler::TimerData& td);

            public:

                AudioSystem();
                ~AudioSystem();

                void OnRegister(ECS::Coordinator* c) override;
                void OnEntitySignatureChanged(ECS::Entity entity, const ECS::Signature& entity_signature) override;
                void OnEntityDestroyed(ECS::Entity entity) override;
                void SetLocalEntity(ECS::Entity entity, const float3& offset = {});
                void SetCameraEntity(ECS::Entity entity);
                bool Config(const std::string& root_folder, const nlohmann::json& config);
                void Reset();
                std::optional<SoundId> LoadSound(const std::string& file, SoundId id);
                std::optional<SoundId> GetSound(const std::string& file);
                
                PlayId Play(SoundId id,
                            int32_t delay_ms = 0,
                            bool loop = false,
                            float speed = 1.0f,
                            float volume = 1.0f,
                            bool simulate_sound_speed = false,
                            ECS::Entity entity = ECS::INVALID_ENTITY_ID,
                            const float3& pos_offset = {});

                void Stop(PlayId id);

                std::optional<float> GetSpeed(PlayId id);
                bool SetSpeed(PlayId id, float speed);

                std::optional<float> GetVol(PlayId id);
                bool SetVol(PlayId id, float speed);

                std::optional<bool> GetLoop(PlayId id);
                bool SetLoop(PlayId id, bool loop);

                std::optional<bool> GetSimSoundSpeed(PlayId id);
                bool SimSoundSpeed(PlayId id, bool loop);
            };
        }
    }
}