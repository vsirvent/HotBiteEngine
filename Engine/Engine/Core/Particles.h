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

#include <Defines.h>
#include <ECS/Types.h>
#include <Components/Base.h>
#include <Core/Material.h>
#include <Core/Vertex.h>
#include <Core/Utils.h>
#include <DirectXMath.h>

using namespace HotBite::Engine::Core;

namespace HotBite {
	namespace Engine {
		namespace Core {

			struct Particle {
				int64_t birth_time = 0;
				int64_t dead_time = 0;
			};

			class ParticlesData {
			public:
				enum eParticleOrigin {
					PARTICLE_ORIGIN_RNG_VERTEX,
					PARTICLE_ORIGIN_FIXED_VERTEX,
					PARTICLE_ORIGIN_CENTER_VERTEX
				};
			
				using ParticlesUpdateCallback = std::function<void(ParticlesData& particles, int64_t elapsed_nsec, int64_t total_nsec)>;
			
			public:
				ParticlesUpdateCallback cb;
				Core::MaterialData* material = nullptr;
				//This allows to share the same vertex buffer between copied obects
				std::shared_ptr<Core::VertexBuffer<ParticleVertex>> vertex_buffer = std::make_shared<VertexBuffer<ParticleVertex>>();
				//The entity that owns this particle
				ECS::Entity parent = ECS::INVALID_ENTITY_ID;
				Components::Mesh* mesh = nullptr;
				Components::Transform* transform = nullptr;
				ECS::Coordinator* coordinator = nullptr;
				Core::UnorderedVector<Particle> particles_info;
				Core::UnorderedVector<ParticleVertex> particles_vertex;
				Core::UnorderedVector<uint32_t> particles_indices;
				float density = 0.0f;
				int max_particles = 0;
				float size = 0.0f;
				float size_variance = 0.0f;
				uint64_t life = 0;
				float life_variance = 0.0f;
				float position_variance = 0.0f;
				float3 offset{};
				float size_increment_ratio = 0.0f;
				bool infinite = false;
				bool visible = true;
				uint64_t last_update = 0;
#define PARTICLE_FLAGS_LOCAL 1 << 0
				uint32_t flags = 0;
				eParticleOrigin origin = PARTICLE_ORIGIN_RNG_VERTEX;
				uint32_t id_count = 0;
			public:

				ParticlesData() {
				}

				ParticlesData(ParticlesUpdateCallback callback) : cb(callback) {
				}
				
				virtual ~ParticlesData() {
				}
				
				virtual void SetCB(ParticlesUpdateCallback& callback) {
					cb = callback;
				}
				
				virtual void Init(ECS::Coordinator* c, ECS::Entity parent, Core::MaterialData* material, float density, int max,
					              float size, uint64_t life, eParticleOrigin origin = PARTICLE_ORIGIN_RNG_VERTEX,
								  float size_variance = 0.0f, float life_variance = 0.0f, float position_variance = 0.0f, float size_increment_ratio = 0.0f,
					float3 offset = {}) {
					this->max_particles = max;
					this->coordinator = c;
					this->parent = parent;
					this->material = material;
					this->density = density;
					this->size = size;
					this->size_variance = size*size_variance;
					this->life = life;
					this->life_variance = life*life_variance;
					this->position_variance = position_variance;
					this->origin = origin;
					this->size_increment_ratio = size_increment_ratio;
					this->offset = offset;
					if (life == INFINITE) {
						infinite = true;
					}
					
					vertex_buffer->Reserve(max_particles);
					transform = c->GetComponentPtr<Components::Transform>(parent);
					assert(transform != nullptr);
					if (c->ContainsComponent<Components::Mesh>(parent)) {
						mesh = c->GetComponentPtr<Components::Mesh>(parent);						
						particles_info.GetData().reserve(max_particles);
						particles_vertex.GetData().reserve(max_particles);
						particles_indices.GetData().reserve(max_particles);
					}
				}

				void PrepareParticle(Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps) {
					if (vs != nullptr) {
						if (mesh != nullptr) {
							mesh->Prepare(vs);
						}
						vs->SetInt("flags", flags);
						vs->SetFloat3("offset", offset);
						vs->SetMatrix4x4("world", transform->world_matrix);
						vs->CopyAllBufferData();
					}
					if (gs != nullptr) {
						gs->SetFloat("sizeIncrementRatio", size_increment_ratio);
						gs->CopyAllBufferData();
					}
					if (ps != nullptr) {
						ps->SetFloat("sizeIncrementRatio", size_increment_ratio);
						ps->CopyAllBufferData();
					}
				}

				void UnprepareParticle(Core::SimpleVertexShader* vs, Core::SimpleHullShader* hs, Core::SimpleDomainShader* ds, Core::SimpleGeometryShader* gs, Core::SimplePixelShader* ps) {
					if (vs != nullptr) {
						if (mesh != nullptr) {
							mesh->Unprepare(vs);
						}
					}
				}

				std::shared_ptr<Core::VertexBuffer<ParticleVertex>> GetVertexBuffer() const { return vertex_buffer; }

				virtual void Flush() {
					vertex_buffer->SetBuffers();
				}
				/**
				 * This method updates the particles status depending on the specific behaviour
				 * of the particles.
				 */
				virtual void Update(int64_t elapsed_nsec, int64_t total_nsec) {
					if (visible) {
						if ((total_nsec - last_update) > MSEC_TO_NSEC(100)) {
							last_update = total_nsec;
							int count = (int)((float)max_particles * density);
							for (int i = 0; i < count && particles_info.GetData().size() < max_particles; ++i) {
								float rng_life = 0.0f;
								float rng_position = 0.0f;
								float rng_size = 0.0f;
								if (life_variance != 0.0f) { rng_life = getRandomNumber(-life_variance, life_variance); }
								if (position_variance != 0.0f) { rng_position = getRandomNumber(-position_variance, position_variance); }
								if (size_variance != 0.0f) { rng_size = getRandomNumber(-size_variance, size_variance); }

								//Get the vertex position
								float3 pos = { 0.0f, 0.0f, 0.0f };
								HotBite::Engine::Core::Vertex v{};
								if (origin != PARTICLE_ORIGIN_CENTER_VERTEX && mesh != nullptr && mesh->GetData()->vertexCount > 0) {
									uint64_t vertex = 0;
									uint64_t max = (mesh->GetData()->vertexCount - 1);
									switch (origin) {
									case PARTICLE_ORIGIN_RNG_VERTEX:
										vertex = (rand() * max) / RAND_MAX;
										break;
									case PARTICLE_ORIGIN_FIXED_VERTEX:
										vertex = (i * max) / count;
										break;
									}
									assert(vertex <= max);
									v = mesh->GetData()->vertices[vertex];
									pos = v.Position;
								}
								if (!(flags & PARTICLE_FLAGS_LOCAL)) {
									//Transform location to current object position, if it's an animated mesh, use the animation transform
									vector3d xm_pos = DirectX::XMLoadFloat3(&pos);
									bool apply_matrix = false;
									if (mesh != nullptr && mesh->skeleton_mutex != nullptr) {
										mesh->skeleton_mutex->lock();
										if (mesh->GetCurrentAnimationId() >= 0) {
											const std::vector<matrix>& joints = mesh->GetJoints();
											float* w = (float*)(&v.Weights);
											size_t njoints = joints.size();
											if (njoints > 0) {
												matrix m = DirectX::XMMatrixIdentity();
												for (int i = 0; i < 4; ++i) {
													if (v.Boneids[i] >= 0 && w[i] > 0 && (int)njoints > v.Boneids[i]) {
														m += joints[v.Boneids[i]] * w[i];
														apply_matrix = true;
													}
												}
												if (apply_matrix) {
													xm_pos = DirectX::XMVector3Transform(xm_pos, m);
												}
											}
										}
										mesh->skeleton_mutex->unlock();
									}
									xm_pos = DirectX::XMVector3Transform(xm_pos, transform->world_xmmatrix);
									DirectX::XMStoreFloat3(&pos, xm_pos);
								}
								pos = float3{ pos.x + offset.x + rng_position, pos.y + offset.y + rng_position, pos.z + offset.z + rng_position };

								//Create a new particle
								uint32_t id = id_count++;
								int64_t end_time = 0;
								if (!infinite) {
									end_time = total_nsec + MSEC_TO_NSEC(life + rng_life);
								}
								particles_info.Insert(Particle{ total_nsec,  end_time });
								particles_vertex.Insert(ParticleVertex(pos, float3{}, 1.0f, size + size_variance, id, v.Boneids, v.Weights));
								particles_indices.Insert(id);
							}
						}
					
						//Update particles position
						auto& data = particles_info.GetData();
						auto& vertices = particles_vertex.GetData();
						for (int i = 0; i < data.size(); ++i) {
							Particle& p = data[i];
							//Remove if dead particle
							if (p.dead_time > 0 && total_nsec > p.dead_time) {
								particles_info.Remove(i);
								particles_vertex.Remove(i);
								particles_indices.Remove(i);
								continue;
							}
							if (p.dead_time != 0) {
								vertices[i].Life = 1.0f - (float)(total_nsec -p.birth_time) / (float)(p.dead_time - p.birth_time);
							}
							else {
								//Ininite particles are in the middle of their lifes forever
								vertices[i].Life = 0.5f;
							}
						}
						if (cb != nullptr) {
							cb(*this, elapsed_nsec, total_nsec);
						}
					}					
				};

				Core::MaterialData* GetMaterial() const { return material; }
				void SetMaterial(Core::MaterialData* m) { material = m; }

				virtual ECS::Entity GetParent() const { return parent; }
			};

			class SmokeParticles : public ParticlesData
			{
			public:
				virtual void Update(int64_t elapsed_nsec, int64_t total_nsec) {
					if (visible) {
						ParticlesData::Update(elapsed_nsec, total_nsec);
						float t = (float)(total_nsec) / 1000000000.0f;
						auto& data = particles_info.GetData();
						auto& vertices = particles_vertex.GetData();
						auto& indices = particles_indices.GetData();
						for (int i = 0; i < data.size(); ++i) {
							Particle& p = data[i];
							//Update the particle position						
							vertices[i].Position.y += 0.015f;
						}

						//Add to buffer
						vertex_buffer->FlushMesh(vertices, indices);
					}
				}
			};

			class FireParticles : public ParticlesData
			{
			public:
				FireParticles() {
					flags |= PARTICLE_FLAGS_LOCAL;
				}

				virtual void Update(int64_t elapsed_nsec, int64_t total_nsec) {
					if (visible) {
						ParticlesData::Update(elapsed_nsec, total_nsec);
						float t = (float)(total_nsec) / 1000000000.0f;
						auto& vertices = particles_vertex.GetData();
						auto& indices = particles_indices.GetData();
						//Add to buffer
						vertex_buffer->FlushMesh(vertices, indices);
					}
				};
			};

			class SparksParticles : public ParticlesData
			{
			public:
				
				virtual void Update(int64_t elapsed_nsec, int64_t total_nsec) {
					if (visible) {
						ParticlesData::Update(elapsed_nsec, total_nsec);
						float t = (float)(total_nsec) / 1000000000.0f;
						auto& data = particles_info.GetData();
						auto& vertices = particles_vertex.GetData();
						auto& indices = particles_indices.GetData();
						for (int i = 0; i < data.size(); ++i) {
							Particle& p = data[i];
							ParticleVertex& v = vertices[i];
							float n = (float)vertices[i].Id;
							//Update the particle position	
							v.Position.x += 0.01f * sin(t * 2.0f + n);
							v.Position.z += 0.01f * cos(t * 2.5f + n);
							v.Position.y += 0.05f * v.Life;
						}
						//Add to buffer
						vertex_buffer->FlushMesh(vertices, indices);
					}
				};
			};
		}
	}
}
