#include "SceneSerializer.h"

#include <Components/Base.h>
#include <Core/Json.h>
#include <filesystem>
#include <fstream>

using namespace nlohmann;
using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;
namespace fs = std::filesystem;

namespace HotBiteEditor {
	namespace SceneSerializer {

		static json Float3ToJson(const float3& v) {
			return json{ {"x", v.x}, {"y", v.y}, {"z", v.z} };
		}

		static json Float4ToJson(const float4& v) {
			return json{ {"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w} };
		}

		void Save(EditorState& state)
		{
			if (state.current_level_path.empty()) {
				state.status_message = "No level open, nothing to save.";
				return;
			}

			json level;
			try {
				level = json::parse(std::ifstream(state.current_level_path));
			}
			catch (std::exception&) {
				state.status_message = "Save failed: could not re-read " + state.current_level_path;
				return;
			}
			json& jw = level["world"];

			Coordinator* c = state.world->GetCoordinator();

			//1) Transform overrides for existing, FBX-authored entities that were edited.
			if (!state.overridden_entities.empty()) {
				if (!jw.contains("entities")) {
					jw["entities"] = json::array();
				}
				for (const std::string& name : state.overridden_entities) {
					Entity e = c->GetEntityByName(name);
					if (e == INVALID_ENTITY_ID || !c->ContainsComponent<Transform>(e)) {
						continue;
					}
					const Transform& t = c->GetComponent<Transform>(e);

					json* target = nullptr;
					for (auto& entry : jw["entities"]) {
						if (entry.contains("name") && entry["name"] == name) {
							target = &entry;
							break;
						}
					}
					if (target == nullptr) {
						json new_entry;
						new_entry["name"] = name;
						jw["entities"].push_back(new_entry);
						target = &jw["entities"].back();
					}
					(*target)["position"] = Float3ToJson(t.position);
					(*target)["scale"] = Float3ToJson(t.scale);
					(*target)["rotation"] = Float4ToJson(t.rotation);
				}
			}

			//2) Editor-placed instances: fully replace the "instances" array with the
			//   session's current bookkeeping, refreshed from live Transform data for
			//   entities that were also moved after being placed.
			json instances = json::array();
			for (const auto& inst : state.placed_instances) {
				json entry;
				entry["name"] = inst.name;
				entry["template"] = inst.template_name;
				entry["position"] = Float3ToJson(inst.position);
				entry["rotation"] = Float4ToJson(inst.rotation);
				entry["scale"] = Float3ToJson(inst.scale);
				if (!inst.material_name.empty()) {
					entry["material"] = inst.material_name;
				}
				instances.push_back(entry);
			}
			jw["instances"] = instances;

			//3) Newly imported templates: ensure they're listed so a future load pulls
			//   them in automatically (existing/pre-existing ones are already present).
			if (!jw.contains("templates")) {
				jw["templates"] = json::array();
			}
			for (const auto& t : state.templates) {
				if (!t.newly_imported) {
					continue;
				}
				fs::path assets_root = fs::path(state.project_root) / "Assets";
				std::error_code ec;
				fs::path rel = fs::relative(t.file_path, assets_root, ec);
				std::string rel_str = ec ? (std::string("Objects\\") + fs::path(t.file_path).filename().string()) : rel.string();

				bool already_listed = false;
				for (auto& entry : jw["templates"]) {
					if (entry.contains("file") && entry["file"] == rel_str) {
						already_listed = true;
						break;
					}
				}
				if (!already_listed) {
					json entry;
					entry["file"] = rel_str;
					entry["triangulate"] = false;
					jw["templates"].push_back(entry);
				}
			}

			std::ofstream out(state.current_level_path);
			out << level.dump(4);
			out.close();

			state.status_message = "Saved: " + state.current_level_path;
		}

	}
}
