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

#include <Components/Physics.h>
#include "DirectionalLightSystem.h"

#include <algorithm>
#include <cmath>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Systems;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::Core;
using namespace DirectX;

void DirectionalLightSystem::OnRegister(ECS::Coordinator* c) {
	this->coordinator = c;
	dirlight_signature.set(coordinator->GetComponentType<Base>(), true);
	dirlight_signature.set(coordinator->GetComponentType<DirectionalLight>(), true);

	camera_signature.set(coordinator->GetComponentType<Base>(), true);
	camera_signature.set(coordinator->GetComponentType<Transform>(), true);
	camera_signature.set(coordinator->GetComponentType<Camera>(), true);
}


void DirectionalLightSystem::OnEntityDestroyed(Entity entity) {
	lights.Remove(entity);
	cameras.Remove(entity);
}

void DirectionalLightSystem::OnEntitySignatureChanged(Entity entity, const Signature& entity_signature) {
	if ((entity_signature & dirlight_signature) == dirlight_signature)
	{
		lights.Insert(entity, DirectionalLightEntity{ coordinator, entity });
	}
	else
	{
		lights.Remove(entity);
	}
	if ((entity_signature & camera_signature) == camera_signature)
	{
		cameras.Insert(entity, CameraSystem::CameraData{ coordinator, entity });
	}
	else
	{
		cameras.Remove(entity);
	}
}

namespace {

	//The camera parameters the cascade fit needs, as Components::Camera reports them.
	struct FrustumParams {
		float tan_half_h = 0.0f; //tan(horizontal half angle)
		float tan_half_v = 0.0f; //tan(vertical half angle)
		float near_z = 0.0f;
		float far_z = 0.0f;
	};

	//Cascade boundaries in camera view-space depth: the practical split scheme.
	//
	//A perspective camera's screen-space texel footprint shrinks as 1/z, so the
	//distribution that gives every cascade the same shadow-texels-per-screen-pixel is
	//the logarithmic one. Taken alone it puts the first boundary absurdly close to the
	//camera (with near=1 and 150 units of range, at z=5), which spends a whole cascade
	//on almost nothing; the uniform term pulls it back out. `lambda` is the blend, and
	//is the single knob worth tuning when cascade 0 looks either wasted or too coarse.
	void ComputeSplits(const FrustumParams& fp, float distance, int count, float lambda,
		float* splits) {
		const float z_near = (std::max)(fp.near_z, 0.01f);
		const float z_far = (std::min)(fp.far_z, (std::max)(distance, z_near + 1.0f));
		const float ratio = z_far / z_near;
		splits[0] = z_near;
		for (int i = 1; i < count; ++i) {
			const float p = (float)i / (float)count;
			const float log_split = z_near * powf(ratio, p);
			const float uni_split = z_near + (z_far - z_near) * p;
			splits[i] = lambda * log_split + (1.0f - lambda) * uni_split;
		}
		splits[count] = z_far;
	}
}

//Fits one orthographic box per cascade around the slice of the camera frustum that
//cascade covers, and builds the world -> shadow-clip matrix for it.
//
//The fit is to the slice's *bounding sphere*, not to the tight box of its eight
//corners. A tight box is smaller - it would buy roughly a third more texel density -
//but its size and orientation change as the camera turns, so every shadow edge in the
//scene crawls as you look around. A sphere is invariant under rotation, so a given
//cascade has one fixed extent and the only thing that moves is its centre, which is
//then snapped to whole shadow texels below. That combination is what makes the edges
//hold still. The sphere itself is the exact minimal one for a symmetric frustum slice
//(see the centre solved for below), so no area is given away beyond the sphere-vs-box
//difference.
void DirectionalLightSystem::Update(DirectionalLightEntity& entity, int64_t elapsed_nsec, int64_t total_nsec) {
	if (cameras.GetData().empty()) {
		return;
	}
	Components::DirectionalLight* light = entity.light;
	if (!light->CastShadow()) {
		return;
	}
	CameraSystem::CameraData& camera = cameras.GetData()[0];

	//Cascades follow where the camera looks as well as where it stands: the slice
	//being bounded is in front of the camera, so a pure rotation moves every cascade
	//just as much as walking does. (The pre-cascade code watched position only, which
	//is why turning on the spot used to leave the shadow map behind.)
	float3 cam_dir{};
	XMStoreFloat3(&cam_dir, camera.camera->xm_direction);
	if (!light->dirty &&
		light->last_cam_pos == camera.camera->world_position &&
		light->last_cam_dir == cam_dir) {
		return;
	}

	FrustumParams fp;
	if (!camera.camera->GetFrustumParams(fp.tan_half_h, fp.tan_half_v, fp.near_z, fp.far_z)) {
		return;
	}
	if (LENGHT_SQUARE_F3(light->data.direction) == 0.0f) {
		return;
	}
	const int resolution = light->texture.Width();
	if (resolution <= 0) {
		return;
	}

	light->last_cam_pos = camera.camera->world_position;
	light->last_cam_dir = cam_dir;
	light->dirty = false;

	const Components::DirectionalLight::CascadeSettings& cs = light->cascade_settings;
	const int count = (std::min)((std::max)(cs.count, 1), MAX_SHADOW_CASCADES);

	//Light basis. `data.direction` points *towards* the light, so the direction light
	//travels - and the direction the shadow "camera" looks - is its negation.
	vector3d dir = XMVector3Normalize(XMVectorSet(
		-light->data.direction.x, -light->data.direction.y, -light->data.direction.z, 0.0f));
	vector3d up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	vector3d right = XMVector3Cross(up, dir);
	if (XMVectorGetX(XMVector3LengthSq(right)) < 1e-8f) {
		//Light straight up or straight down: any perpendicular will do.
		up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
		right = XMVector3Cross(up, dir);
	}
	right = XMVector3Normalize(right);
	up = XMVector3Normalize(XMVector3Cross(dir, right));

	//Rotation-only light space (origin at the world origin). Snapping happens in here,
	//so the snap grid is anchored to the world rather than to the light's eye - an eye
	//that moves with the camera would drag the grid along and defeat the snapping.
	const matrix light_rot = XMMatrixLookToLH(XMVectorZero(), dir, up);
	const matrix light_rot_inv = XMMatrixInverse(nullptr, light_rot);
	const matrix inv_view = XMMatrixInverse(nullptr, camera.camera->xm_view);

	float splits[MAX_SHADOW_CASCADES + 1] = {};
	ComputeSplits(fp, cs.distance, count, cs.split_lambda, splits);

	//Squared tangent of the cone that contains the frustum's corner rays: a corner at
	//view depth z sits sqrt(k2)*z off the view axis.
	const float k2 = fp.tan_half_h * fp.tan_half_h + fp.tan_half_v * fp.tan_half_v;

	for (int i = 0; i < count; ++i) {
		const float n = splits[i];
		const float f = splits[i + 1];

		//Minimal sphere enclosing the slice, centred on the view axis at depth c.
		//Equating the distance to a near corner and to a far corner,
		//    (n-c)^2 + n^2*k2 = (f-c)^2 + f^2*k2
		//solves to c = (n+f)*(1+k2)/2. Past the far plane that solution stops being
		//reachable - the near corners are already inside by then - and the smallest
		//sphere is the one around the far ring alone, so c clamps to f. c is never
		//below n, since (n+f)/2 >= n whenever f >= n.
		float c = (n + f) * (1.0f + k2) * 0.5f;
		if (c > f) {
			c = f;
		}
		const float dn = sqrtf((n - c) * (n - c) + n * n * k2);
		const float df = sqrtf((f - c) * (f - c) + f * f * k2);
		float radius = (std::max)(dn, df);
		if (radius < 1e-3f) {
			radius = 1e-3f;
		}

		//Sphere centre: on the camera's view axis, taken to world space.
		vector3d center = XMVector3TransformCoord(XMVectorSet(0.0f, 0.0f, c, 1.0f), inv_view);

		//Snap the centre to whole shadow texels. Without this the box slides
		//continuously as the camera moves, every texel covers a slightly different
		//patch of world each frame, and shadow edges boil. The extent is already
		//rotation-invariant, so a texel-quantized centre makes the whole mapping
		//frame-to-frame stable for as long as the cascade's radius does not change -
		//which, being derived from the splits alone, it never does.
		const float texel = (2.0f * radius) / (float)resolution;
		vector3d center_ls = XMVector3TransformCoord(center, light_rot);
		const float cx = floorf(XMVectorGetX(center_ls) / texel + 0.5f) * texel;
		const float cy = floorf(XMVectorGetY(center_ls) / texel + 0.5f) * texel;
		const float cz = XMVectorGetZ(center_ls);
		center = XMVector3TransformCoord(XMVectorSet(cx, cy, cz, 1.0f), light_rot_inv);

		//Pull the eye back past the sphere so that casters standing between the light
		//and the view still land in the map. Everything from the eye (depth 0) to the
		//far side of the sphere is covered.
		const float extrusion = (std::max)(cs.caster_extrusion, 0.0f);
		const vector3d eye = XMVector3TransformCoord(
			XMVectorSet(cx, cy, cz - (radius + extrusion), 1.0f), light_rot_inv);

		const matrix view = XMMatrixLookToLH(eye, dir, up);
		const matrix projection = XMMatrixOrthographicLH(
			2.0f * radius, 2.0f * radius, 0.0f, 2.0f * radius + extrusion);
		XMStoreFloat4x4(&light->viewMatrix[i], XMMatrixTranspose(view * projection));

		if (i == 0) {
			//Kept for the accessors that predate cascades and describe "the" shadow
			//projection; cascade 0 is the one they meant.
			XMStoreFloat4x4(&light->lightPerspectiveValues, projection);
			XMStoreFloat4x4(&light->projectionMatrix, projection);
		}

		Components::DirectionalLight::CascadeInfo& info = light->cascade_info[i];
		XMStoreFloat3(&info.center, center);
		XMStoreFloat3(&info.dir, dir);
		XMStoreFloat3(&info.right, right);
		XMStoreFloat3(&info.up, up);
		info.radius = radius;
		info.extrusion = extrusion;
		info.near_split = n;
		info.far_split = f;
		info.texel_density = (float)resolution / (2.0f * radius);
	}

	//Slices past the live count hold nothing. Their matrices are zeroed so that a
	//shader reaching one by mistake projects to a degenerate point rather than to the
	//middle of the map, and cascade_count is what stops it reaching them at all.
	for (int i = count; i < MAX_SHADOW_CASCADES; ++i) {
		light->viewMatrix[i] = {};
		light->cascade_info[i] = {};
	}
	light->data.cascade_count = count;
}

void DirectionalLightSystem::Update(int64_t elapsed_nsec, int64_t total_nsec) {
	for (auto it = lights.GetData().begin(); it != lights.GetData().end(); ++it)
	{
		Update(*it, elapsed_nsec, total_nsec);
	}
}