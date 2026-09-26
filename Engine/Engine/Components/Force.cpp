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

#include <Components/Force.h>

#include <string>

using namespace HotBite::Engine;
using namespace HotBite::Engine::Components;
using namespace HotBite::Engine::ECS;

const char* Force::TypeName(eType t) {
	switch (t) {
	case TOUCH: return "TOUCH";
	case PROJECTION: return "PROJECTION";
	default: return "NONE";
	}
}

Force::eType Force::TypeFromName(const std::string& name, eType fallback) {
	if (name == "NONE") { return NONE; }
	if (name == "TOUCH") { return TOUCH; }
	if (name == "PROJECTION") { return PROJECTION; }
	return fallback;
}

nlohmann::json Force::ToJson(const SerializeContext& ctx) const {
	nlohmann::json j;
	j["type"] = TypeName(type);
	j["dir"] = JsonUtil::FromFloat3(dir);
	j["local_dir"] = local_dir;
	j["force"] = force;
	j["origin_force"] = origin_force;
	j["range"] = range;
	j["radius"] = radius;
	return j;
}

void Force::FromJson(const nlohmann::json& j, const SerializeContext& ctx) {
	if (j.contains("type") && j["type"].is_string()) {
		type = TypeFromName(j["type"].get<std::string>(), type);
	}
	JsonUtil::ToFloat3(j, "dir", dir);
	local_dir = j.value("local_dir", local_dir);
	force = j.value("force", force);
	origin_force = j.value("origin_force", origin_force);
	range = j.value("range", range);
	radius = j.value("radius", radius);
}
