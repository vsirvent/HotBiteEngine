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

#include <Components/Platform.h>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::ECS;

//Every field below is authored; the `rt` block is live state PlatformSystem owns and
//is deliberately absent. Everything is written unconditionally rather than "only when
//it differs from the default": undo replays an earlier ToJson through FromJson, which
//reads a missing key as "leave alone", so a key omitted for matching the default could
//never be restored (the trap documented for Mesh::ToJson's `smooth`).

nlohmann::json Platform::ToJson(const SerializeContext& ctx) const {
	nlohmann::json j;
	j["linear_dir"] = JsonUtil::FromFloat3(linear_dir);
	j["amplitude"] = amplitude;
	j["freq"] = freq;
	j["phase"] = phase;
	j["angular_dir"] = JsonUtil::FromFloat3(angular_dir);
	j["angular_speed"] = angular_speed;
	j["delay"] = delay;
	j["fall_delay"] = fall_delay;
	return j;
}

void Platform::FromJson(const nlohmann::json& j, const SerializeContext& ctx) {
	JsonUtil::ToFloat3(j, "linear_dir", linear_dir);
	amplitude = j.value("amplitude", amplitude);
	freq = j.value("freq", freq);
	phase = j.value("phase", phase);
	JsonUtil::ToFloat3(j, "angular_dir", angular_dir);
	angular_speed = j.value("angular_speed", angular_speed);
	delay = j.value("delay", delay);
	fall_delay = j.value("fall_delay", fall_delay);
	//An edited platform restarts from wherever it is authored now. Re-latching is what
	//makes an amplitude or direction change take effect about the platform's authored
	//centre instead of about the point it happened to be swinging through: the system
	//captures the centre on the first tick, so clearing the flag is how an editor asks
	//for that to happen again.
	rt.latched = false;
	rt.clock = 0.0f;
	rt.fallen = false;
}

nlohmann::json LinearPlatform::ToJson(const SerializeContext& ctx) const {
	nlohmann::json j;
	j["travel"] = JsonUtil::FromFloat3(travel);
	j["speed"] = speed;
	j["delay"] = delay;
	j["ping_pong"] = ping_pong;
	return j;
}

void LinearPlatform::FromJson(const nlohmann::json& j, const SerializeContext& ctx) {
	JsonUtil::ToFloat3(j, "travel", travel);
	speed = j.value("speed", speed);
	delay = j.value("delay", delay);
	ping_pong = j.value("ping_pong", ping_pong);
	rt.latched = false;
	rt.clock = 0.0f;
	rt.t = 0.0f;
	rt.forward = true;
}
