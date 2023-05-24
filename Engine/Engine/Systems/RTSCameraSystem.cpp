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

#include <reactphysics3d\reactphysics3d.h>
#include <Components\camera.h>
#include <Core\DXCore.h>
#include "RTSCameraSystem.h"
#include "RenderSystem.h"

using namespace HotBite::Engine;
using namespace HotBite::Engine::Core;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Systems;
using namespace DirectX;

RTSCameraSystem::~RTSCameraSystem() {
    Scheduler::Get(DXCore::BACKGROUND2_THREAD)->RemoveTimerAsync(zoom_check_timer);
}

void RTSCameraSystem::OnRegister(ECS::Coordinator* c) {
	this->coordinator = c;
    //Initialize EventListener, this is a parent class used for easy event register/unregister management
    //and needs access to the coordinator
    EventListener::Init(c);

    //Set the signature of the entity to include the components we want
    signature.set(coordinator->GetComponentType<Base>(), true);
	signature.set(coordinator->GetComponentType<Transform>(), true);
	signature.set(coordinator->GetComponentType<Camera>(), true);	

    //Register to the events we need for managing the camera
    AddEventListener(DXCore::EVENT_ID_MOUSE_WHEEL, std::bind(&RTSCameraSystem::OnMouseWheel, this, std::placeholders::_1));
    AddEventListener(DXCore::EVENT_ID_MOUSE_MOVE, std::bind(&RTSCameraSystem::OnMouseMove, this, std::placeholders::_1));
    AddEventListener(Systems::CameraSystem::EVENT_ID_CAMERA_MOVED, std::bind(&RTSCameraSystem::OnCameraMoved, this, std::placeholders::_1));
    zoom_check_timer = Scheduler::Get(DXCore::BACKGROUND2_THREAD)->RegisterTimer(1000000000 / 60,
        std::bind(&RTSCameraSystem::OnZoomCheck, this, std::placeholders::_1));
}

void RTSCameraSystem::RotateX(CameraSystem::CameraData& entity, float x)
{
    entity.camera->rotation.x += x;
    entity.transform->dirty = true;
    check_zoom = true;
}

void RTSCameraSystem::RotateY(CameraSystem::CameraData& entity, float y)
{
    entity.camera->rotation.y += y;
    entity.transform->dirty = true;
    check_zoom = true;
}

void RTSCameraSystem::RotateZ(CameraSystem::CameraData& entity, float z)
{
    entity.camera->rotation.z += z;
    entity.transform->dirty = true;
    check_zoom = true;
}

void RTSCameraSystem::Move(CameraSystem::CameraData& entity, float2 dpos) {
    lock.lock();
    CameraSystem::CameraData& cdata = cameras.GetData()[0];
    bool out_limits = false;
    float dx = cdata.transform->position.x - cdata.camera->direction.x;
    float dz = cdata.transform->position.z - cdata.camera->direction.z;
    dpos.x = dpos.x * sgn<float>(dx);
    dpos.y = dpos.y * sgn<float>(dz);
    float2 new_pos = { cdata.camera->final_position.x + dpos.x, cdata.camera->final_position.z + dpos.y };

    if (terrain.base != nullptr) {
        new_pos.x = std::clamp(new_pos.x, -terrain.bounds->final_box.Extents.x, terrain.bounds->final_box.Extents.x);
        new_pos.y = std::clamp(new_pos.y, -terrain.bounds->final_box.Extents.y, terrain.bounds->final_box.Extents.y);
        dpos.x = new_pos.x - cdata.camera->final_position.x;
        dpos.y = new_pos.y - cdata.camera->final_position.z;
    }
    if (!out_limits) {
        entity.camera->direction.x += dpos.x;
        entity.camera->direction.z += dpos.y;
        entity.transform->position.x += dpos.x;
        entity.transform->position.z += dpos.y;
        entity.transform->dirty = true;
        check_zoom = true;
    }
    lock.unlock();
}

void RTSCameraSystem::Zoom(CameraSystem::CameraData& entity, float delta) {
    lock.lock();
    float3 dir = entity.camera->direction - entity.transform->position;
    vector3d xm_dir = XMVector3Normalize(XMLoadFloat3(&dir)) * (-delta);
    entity.transform->position.x += xm_dir.m128_f32[0];
    entity.transform->position.y += xm_dir.m128_f32[1];
    entity.transform->position.z += xm_dir.m128_f32[2];
    entity.transform->dirty = true;
    check_zoom = true;
    Event ev(this, EVENT_ID_CAMERA_ZOOMED);
    ev.SetParam<float>(EVENT_PARAM_CAMERA_ZOOM, current_zoom);
    coordinator->SendEvent(ev);
    lock.unlock();
}

void RTSCameraSystem::OnEntityDestroyed(Entity entity) {
    //Remove method is re-entrant, so we don't need to check whether it exists or not
	cameras.Remove(entity);
}

void RTSCameraSystem::SetCameraDirection(const float2& dir_offset) {
    lock.lock();
    CameraSystem::CameraData& cdata = cameras.GetData()[0];
    cdata.camera->direction.x = cdata.transform->position.x + dir_offset.x;
    cdata.camera->direction.z = cdata.transform->position.z + dir_offset.y;
    lock.unlock();
}

void RTSCameraSystem::SetCameraPosition(const float2& position, bool centered) {
    lock.lock();
    //Set position keeping the direction
    CameraSystem::CameraData& cdata = cameras.GetData()[0];
    float pdx = cdata.transform->position.x - cdata.camera->final_position.x;
    float pdz = cdata.transform->position.z - cdata.camera->final_position.z;
    float dx = cdata.camera->direction.x - cdata.transform->position.x;
    float dz = cdata.camera->direction.z - cdata.transform->position.z;
    float2 corrected_position;
    if (centered) {
        float3 pdd = cdata.camera->direction - cdata.camera->final_position;
        vector3d xm_pdd = XMLoadFloat3(&pdd);
        XMStoreFloat3(&pdd, XMVector3Normalize(xm_pdd) * 30.0f);
        corrected_position = { position.x + pdx - pdd.x, position.y + pdz - pdd.z };
    }
    else {
        corrected_position = { position.x + pdx, position.y + pdz };
    }
        
    cdata.transform->position.x = corrected_position.x;
    cdata.transform->position.z = corrected_position.y;
    cdata.camera->direction.x = corrected_position.x + dx;
    cdata.camera->direction.z = corrected_position.y + dz;
    cdata.transform->dirty = true;
    lock.unlock();
}

void RTSCameraSystem::SetCameraRelativePosition(const float2& relative_position, bool centered) {
    if (terrain.bounds != nullptr) {
        lock.lock();
        float2 absolute_pos;
        CameraSystem::CameraData& cdata = cameras.GetData()[0];
        absolute_pos.x = terrain.bounds->final_box.Extents.x * (relative_position.x * 2.0f - 1.0f);
        absolute_pos.y = terrain.bounds->final_box.Extents.y * (relative_position.y * 2.0f - 1.0f);
        SetCameraPosition(absolute_pos, centered);
        lock.unlock();
    }
}

void RTSCameraSystem::OnEntitySignatureChanged(Entity entity, const Signature& entity_signature) {
	if ((entity_signature & signature) == signature)
	{
        //Signature match, we register the entity.
		CameraSystem::CameraData cam{ coordinator, entity };
		cameras.Insert(entity, cam);
	}
	else
	{
        cameras.Remove(entity);
	}
}

bool RTSCameraSystem::OnZoomCheck(const Scheduler::TimerData& t) {
    if (terrain.physics != nullptr && check_zoom) {
        if (cameras.GetData().empty()) {
            return true;
        }
        lock.lock();
        physics_mutex.lock();
        //We only work with the first camera
        CameraSystem::CameraData& cdata = cameras.GetData()[0];
        reactphysics3d::Vector3 r0 = { cdata.camera->final_position.x, 1000.0f, cdata.camera->final_position.z };
        reactphysics3d::Vector3 r1 = { cdata.camera->final_position.x, -50.0f, cdata.camera->final_position.z };
        reactphysics3d::Ray ray0 = { r0, r1 };
        reactphysics3d::Vector3 r2 = { cdata.camera->final_position.x, cdata.camera->final_position.y, cdata.camera->final_position.z };
        reactphysics3d::Vector3 r3 = { cdata.camera->direction.x, cdata.camera->direction.y, cdata.camera->direction.z };
        reactphysics3d::Ray ray1 = { r2, r3 };
        reactphysics3d::Vector3 r4 = { cdata.camera->final_position.x, 1000.0f, cdata.camera->final_position.z };
        reactphysics3d::Vector3 r5 = { cdata.camera->direction.x, -50.0f, cdata.camera->direction.z };
        reactphysics3d::Ray ray2 = { r4, r5 };
        //Check hit with terrain
        reactphysics3d::RaycastInfo info = {}, info2 = {};
        bool check = false;
        bool limit_down = false;
        if (terrain.physics->body->raycast(ray0, info) || terrain.physics->body->raycast(ray1, info2)) {
            info.worldPoint.y = max(0.0f, max(info.worldPoint.y, info2.worldPoint.y));
            if (info.worldPoint.y + min_zoom + 1.0f > cdata.camera->final_position.y) {
                if (info.worldPoint.y + min_zoom > cdata.camera->final_position.y) {
                    float delta = abs(info.worldPoint.y + 10.0f - cdata.camera->final_position.y)/ 10.0f;
                    if (info.worldPoint.y > cdata.camera->final_position.y) {
                        delta = info.worldPoint.y - cdata.camera->final_position.y;
                    }
                    current_zoom -= delta;
                    Zoom(cdata, delta);
                    check = true;
                }
                limit_down = true;
            }
        }
        if (terrain.physics->body->raycast(ray2, info)) {
            info.worldPoint.y = max(0.0f, info.worldPoint.y);
            float h = cdata.camera->final_position.y - info.worldPoint.y;
            float delta = abs(h - current_zoom) / 10.0f;
            if (h > current_zoom + 2.0f && !limit_down) {
                delta = min(delta, 1.0f);
                current_zoom += delta;
                Zoom(cdata, -delta);
                check = true;
            }
            else if (h < current_zoom - 2.0f) {
                current_zoom -= delta;
                Zoom(cdata, delta);
                check = true;
            }            
        }
        
        bool limit_x0 = (cdata.camera->final_position.x < -terrain.bounds->final_box.Extents.x); 
        bool limit_x1 = (cdata.camera->final_position.x > terrain.bounds->final_box.Extents.x);
        
        bool limit_z0 = (cdata.camera->final_position.z < -terrain.bounds->final_box.Extents.y);
        bool limit_z1 = (cdata.camera->final_position.z > terrain.bounds->final_box.Extents.y);
                       

        if (limit_x0 || limit_x1 || limit_z0 || limit_z1) {
            float2 limited_pos = { cdata.camera->final_position.x, cdata.camera->final_position.z };
            if (limit_x0) {
                limited_pos.x = -terrain.bounds->final_box.Extents.x;
            }
            else if (limit_x1) {
                limited_pos.x = terrain.bounds->final_box.Extents.x;
            }
            if (limit_z0) {
                limited_pos.y = -terrain.bounds->final_box.Extents.y;
            }
            else if (limit_z1) {
                limited_pos.y = terrain.bounds->final_box.Extents.y;
            }
            SetCameraPosition(limited_pos);
        }
        physics_mutex.unlock();
        lock.unlock();
        check_zoom = check;
    }
    return true;
}

void RTSCameraSystem::OnCameraMoved(ECS::Event& ev) {
    check_zoom = true;
}

void RTSCameraSystem::OnMouseMove(ECS::Event& ev) {
    //If no cameras, nothing to do
    if (cameras.GetData().empty()) {
        return;
    }
    //We only work with the first camera
    CameraSystem::CameraData& cdata = cameras.GetData()[0];
    int sw = DXCore::Get()->GetWidth();
    int sh = DXCore::Get()->GetHeight();

    //We received a mouse move event, the event includes the parameters we need
    int button = ev.GetParam<int>(DXCore::PARAM_ID_BUTTON);
    int x = ev.GetParam<int>(DXCore::PARAM_ID_X);
    int y = ev.GetParam<int>(DXCore::PARAM_ID_Y);

    bool move = false;
    vector3d xm = {};
    if (button == 0) {

        float delta = abs(pow(current_zoom, 0.25f) * 0.2f);
        if (x < 10) {
            xm = XMVectorSet(delta, 0.0f, -delta, 1.0f);
            move = true;
        }
        else if (x + 10 > sw) {
            xm = XMVectorSet(-delta, 0.0f, delta, 1.0f);
            move = true;
        }
        else if (y + 50 > sh) {
            xm = XMVectorSet(delta, 0.0f, delta, 1.0f);
            move = true;
        }
        else if (y < 10) {
            xm = XMVectorSet(-delta, 0.0f, -delta, 1.0f);
            move = true;
        }
    }
    //Middle button rotates camera
    if (button & MK_MBUTTON) {
        if (last_mouse_pos.x > 0 && last_mouse_pos.y > 0) {            
            float w = (float)DXCore::Get()->GetWidth();
            float h = (float)DXCore::Get()->GetHeight();
            float delta_rx = -2.0f * (float)(last_mouse_pos.x - x) / w;
            RotateY(cdata, delta_rx);
#if 0
            float delta_ry = 2.0f * (float)(last_mouse_pos.y - y) / h;
            float new_ry = std::clamp(current_ry + delta_ry, MIN_RY, MAX_RY);
            if (current_ry != new_ry) {
                RotateZ(cdata, delta_ry);
                RotateX(cdata, -delta_ry);
                current_ry = new_ry;
            }
#endif
        }
    }
    
    last_mouse_pos.x = (float)x;
    last_mouse_pos.y = (float)y;

    if (move) {
        if (scroll_timer == Core::Scheduler::INVALID_TIMER_ID) {
            vector4d r = XMQuaternionRotationRollPitchYaw(0.0f, cdata.camera->rotation.y, 0.0f);
            xm = XMVector3Rotate(xm, r);
            XMStoreFloat3(&move_direction, xm);
            scroll_timer = Scheduler::Get(DXCore::BACKGROUND2_THREAD)->RegisterTimer(1000000000 / 60, [this](const Scheduler::TimerData& t) {
                RenderSystem::mutex.lock();
                CameraSystem::CameraData& cdata = cameras.GetData()[0];
                Move(cdata, { move_direction.x, move_direction.z } );
                RenderSystem::mutex.unlock();
                return (scroll_timer != Core::Scheduler::INVALID_TIMER_ID);
            });
        }
    }
    else if (scroll_timer != Core::Scheduler::INVALID_TIMER_ID) {
        Scheduler::Get(DXCore::BACKGROUND2_THREAD)->RemoveTimerAsync(scroll_timer);
        scroll_timer = Core::Scheduler::INVALID_TIMER_ID;
    }
}

void RTSCameraSystem::SetTerrain(ECS::Entity e) {
    terrain = TerrainData{ coordinator, e };
}

void RTSCameraSystem::OnMouseWheel(ECS::Event& ev) {
    float amount = ev.GetParam<float>(DXCore::PARAM_ID_WHEEL)*-2.0f;
    new_zoom = std::clamp(current_zoom + amount, min_zoom, max_zoom);
    if (current_zoom != new_zoom) {
        lock.lock();
        //Small zoom are done directly
        if (abs(current_zoom - new_zoom) < 0.2f) {
            CameraSystem::CameraData& cdata = cameras.GetData()[0];
            float delta = new_zoom - current_zoom;
            Zoom(cdata, delta);
            current_zoom = new_zoom;
        }
        else {
            //Big zooms are smooth
            if (!zooming) {
                zooming = true;
                Scheduler::Get(DXCore::BACKGROUND2_THREAD)->RegisterTimer(1000000000 / 60, [this, &z = zooming](const Scheduler::TimerData& t) {
                    RenderSystem::mutex.lock();
                    bool ret = true;
                    CameraSystem::CameraData& cdata = cameras.GetData()[0];
                    float delta = std::clamp((new_zoom - current_zoom) / 5.0f, -1.0f, 1.0f);
                    current_zoom = std::clamp(current_zoom + delta, min_zoom, max_zoom);
                    if (abs(current_zoom - new_zoom) > 0.1f) {
                        Zoom(cdata, delta);
                    }
                    else {
                        z = false;
                        ret = false;
                    }
                    RenderSystem::mutex.unlock();
                    return ret;
                    });
            }
        }
        lock.unlock();
    }
}

ECS::EntityVector<CameraSystem::CameraData>& RTSCameraSystem::GetCameras() {
	return cameras;
}
