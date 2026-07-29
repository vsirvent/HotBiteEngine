#include "MultiMaterialPanel.h"
#include "MaterialPanel.h"
#include "MaskPaint.h"
#include "EditorHistory.h"
#include "MaterialPreview.h"

#include "imgui.h"
#include <World.h>
#include <Components/Base.h>

#include <algorithm>
#include <cstring>

using namespace HotBite::Engine;
using namespace HotBite::Engine::ECS;
using namespace HotBite::Engine::Components;

namespace HotBiteEditor {
	namespace MultiMaterialOps {
		namespace {

			Core::MultiMaterialData* Find(EditorState& state, const std::string& name) {
				return (state.world != nullptr) ? state.world->GetMultiMaterial(name) : nullptr;
			}

			//Marks the .mat file a stack belongs to as unsaved. Every mutation path goes
			//through here; a missed call makes File/Save Materials skip a real change.
			void Touch(EditorState& state, const std::string& name) {
				const std::string file = state.world->GetMultiMaterialOrigin(name);
				if (!file.empty()) {
					state.dirty_material_files.insert(file);
				}
				//The thumbnails of the materials wearing it are now wrong.
				for (const std::string& material : FindMaterials(state, name)) {
					MaterialPreview::Invalidate(material);
				}
			}

			//Compared through the serialized form rather than field by field: what
			//ToJson writes is exactly what a save would keep, so two stacks that
			//serialize alike are the same as far as any undo could tell.
			bool Same(const Snapshot& a, const Snapshot& b) {
				return a.ToJson() == b.ToJson();
			}

			//Reads a value out of a field bag under any of the names the .mat file, the
			//C++ struct and the automation channel use for it.
			template<class T>
			bool Read(const nlohmann::json& fields, const char* key, T& out) {
				auto it = fields.find(key);
				if (it == fields.end()) {
					return false;
				}
				try {
					out = it->get<T>();
				}
				catch (...) {
					return false;
				}
				return true;
			}

			bool ReadBool(const nlohmann::json& fields, const char* key, bool& out) {
				auto it = fields.find(key);
				if (it == fields.end()) {
					return false;
				}
				//Accepts true/false and 1/0: the .mat format writes the flags as ints and
				//a script typing 1 should not be a silent no-op.
				if (it->is_boolean()) { out = it->get<bool>(); return true; }
				if (it->is_number()) { out = it->get<double>() != 0.0; return true; }
				return false;
			}
		}

		std::vector<std::string> List(EditorState& state) {
			if (state.world == nullptr) {
				return {};
			}
			return state.world->ListMultiMaterials();
		}

		bool GetSnapshot(EditorState& state, const std::string& name, Snapshot& out) {
			Core::MultiMaterialData* mm = Find(state, name);
			if (mm == nullptr) {
				return false;
			}
			out = *mm;
			return true;
		}

		bool ApplySnapshot(EditorState& state, const std::string& name,
			const Snapshot& snapshot, std::string& error) {
			if (state.world == nullptr) {
				error = "no scene loaded";
				return false;
			}
			if (Find(state, name) == nullptr) {
				error = "multi-material not found: " + name;
				return false;
			}
			//SetMultiMaterial rebuilds the GPU arrays and re-resolves the materials
			//pointing at this stack, which is what makes the change visible this frame.
			state.world->SetMultiMaterial(name, snapshot);
			Touch(state, name);
			return true;
		}

		void RecordEdit(EditorState& state, const std::string& name, const Snapshot& before) {
			Snapshot after;
			if (!GetSnapshot(state, name, after)) {
				return;
			}
			if (Same(before, after)) {
				return; //a drag that ended where it started records nothing
			}
			Touch(state, name);

			const std::string stack = name;
			EditorHistory::Push({
				"edit multi-material " + stack,
				[stack, before](EditorState& s) {
					std::string error;
					ApplySnapshot(s, stack, before, error);
				},
				[stack, after](EditorState& s) {
					std::string error;
					ApplySnapshot(s, stack, after, error);
				} });
		}

		bool Create(EditorState& state, const std::string& name,
			const std::string& mat_file, std::string& error) {
			if (state.world == nullptr) {
				error = "no scene loaded";
				return false;
			}
			if (name.empty()) {
				error = "multi-material name is empty";
				return false;
			}
			if (Find(state, name) != nullptr) {
				error = "multi-material already exists: " + name;
				return false;
			}
			if (state.world->CreateMultiMaterial(name, mat_file) == nullptr) {
				error = "could not create multi-material in " + mat_file;
				return false;
			}
			state.dirty_material_files.insert(mat_file);
			state.selected_multi_material = name;
			state.selected_multi_material_layer = 0;

			const std::string created = name;
			const std::string file = mat_file;
			EditorHistory::Push({
				"create multi-material " + created,
				[created, file](EditorState& s) {
					s.world->RemoveMultiMaterial(created);
					s.dirty_material_files.insert(file);
					if (s.selected_multi_material == created) {
						s.selected_multi_material.clear();
					}
				},
				[created, file](EditorState& s) {
					//RemoveMultiMaterial retires rather than erases, so this revives the
					//same entry - layers included - instead of a second empty one.
					if (!s.world->RestoreMultiMaterial(created, file)) {
						s.world->CreateMultiMaterial(created, file);
					}
					s.dirty_material_files.insert(file);
					s.selected_multi_material = created;
				} });
			return true;
		}

		bool Duplicate(EditorState& state, const std::string& source_name,
			const std::string& new_name, std::string& error) {
			Snapshot source;
			if (!GetSnapshot(state, source_name, source)) {
				error = "multi-material not found: " + source_name;
				return false;
			}
			const std::string file = state.world->GetMultiMaterialOrigin(source_name);
			if (file.empty()) {
				error = source_name + " belongs to no .mat file and cannot be duplicated";
				return false;
			}
			if (!Create(state, new_name, file, error)) {
				return false;
			}
			Snapshot blank;
			GetSnapshot(state, new_name, blank);
			if (!ApplySnapshot(state, new_name, source, error)) {
				return false;
			}
			//Its own history step, so undoing twice removes the copy entirely.
			RecordEdit(state, new_name, blank);
			return true;
		}

		std::vector<std::string> FindMaterials(EditorState& state, const std::string& name) {
			std::vector<std::string> found;
			if (state.world == nullptr) {
				return found;
			}
			for (const auto& material : state.world->GetMaterials().GetData()) {
				if (material.multi_material_name == name && !material.name.empty() &&
					!state.world->IsMaterialRemoved(material.name)) {
					found.push_back(material.name);
				}
			}
			std::sort(found.begin(), found.end());
			return found;
		}

		std::vector<std::string> FindUsers(EditorState& state, const std::string& name) {
			std::vector<std::string> users;
			for (const std::string& material : FindMaterials(state, name)) {
				const std::vector<std::string> entities = MaterialOps::FindUsers(state, material);
				users.insert(users.end(), entities.begin(), entities.end());
			}
			std::sort(users.begin(), users.end());
			users.erase(std::unique(users.begin(), users.end()), users.end());
			return users;
		}

		bool Remove(EditorState& state, const std::string& name, std::string& error) {
			if (Find(state, name) == nullptr) {
				error = "multi-material not found: " + name;
				return false;
			}
			const std::string file = state.world->GetMultiMaterialOrigin(name);
			//Captured before the removal detaches them, so undo can put them back.
			const std::vector<std::string> wearers = FindMaterials(state, name);
			//A painting session on this stack would be pointing at layers that no
			//longer exist.
			if (MaskPaint::Active() && MaskPaint::CurrentMultiMaterial() == name) {
				MaskPaint::End(state);
			}
			state.world->RemoveMultiMaterial(name);
			if (!file.empty()) {
				state.dirty_material_files.insert(file);
			}
			for (const std::string& material : wearers) {
				const std::string material_file = state.world->GetMaterialOrigin(material);
				if (!material_file.empty()) {
					state.dirty_material_files.insert(material_file);
				}
			}
			if (state.selected_multi_material == name) {
				state.selected_multi_material.clear();
			}

			const std::string removed = name;
			const std::string origin = file;
			EditorHistory::Push({
				"remove multi-material " + removed,
				[removed, origin, wearers](EditorState& s) {
					if (!s.world->RestoreMultiMaterial(removed, origin)) {
						return;
					}
					for (const std::string& material : wearers) {
						s.world->SetMaterialMultiMaterial(material, removed);
					}
					if (!origin.empty()) {
						s.dirty_material_files.insert(origin);
					}
				},
				[removed, origin](EditorState& s) {
					s.world->RemoveMultiMaterial(removed);
					if (!origin.empty()) {
						s.dirty_material_files.insert(origin);
					}
					if (s.selected_multi_material == removed) {
						s.selected_multi_material.clear();
					}
				} });
			return true;
		}

		bool AddLayer(EditorState& state, const std::string& name,
			const std::string& material, std::string& error) {
			Snapshot before;
			if (!GetSnapshot(state, name, before)) {
				error = "multi-material not found: " + name;
				return false;
			}
			if (before.layers.size() >= MAX_MULTI_TEXTURE) {
				error = "a multi-material holds at most " + std::to_string(MAX_MULTI_TEXTURE) +
					" layers";
				return false;
			}
			if (state.world->GetMaterials().Get(material) == nullptr ||
				state.world->IsMaterialRemoved(material)) {
				error = "material not found: " + material;
				return false;
			}
			Snapshot after = before;
			Core::MultiMaterialLayer layer;
			layer.material = material;
			//The first layer has nothing under it to mix with, so it is the base; the
			//ones above it default to mixing over what is already there, which is what
			//"paint this material on top" means and the only mode that reads as a blend.
			layer.op = TEXT_OP_MIX;
			after.layers.push_back(layer);
			if (!ApplySnapshot(state, name, after, error)) {
				return false;
			}
			state.selected_multi_material_layer = (int)after.layers.size() - 1;
			RecordEdit(state, name, before);
			return true;
		}

		bool RemoveLayer(EditorState& state, const std::string& name, int index,
			std::string& error) {
			Snapshot before;
			if (!GetSnapshot(state, name, before)) {
				error = "multi-material not found: " + name;
				return false;
			}
			if (index < 0 || index >= (int)before.layers.size()) {
				error = "no layer " + std::to_string(index) + " in " + name;
				return false;
			}
			if (MaskPaint::Active() && MaskPaint::CurrentMultiMaterial() == name) {
				MaskPaint::End(state);
			}
			Snapshot after = before;
			after.layers.erase(after.layers.begin() + index);
			if (!ApplySnapshot(state, name, after, error)) {
				return false;
			}
			state.selected_multi_material_layer =
				(std::min)(state.selected_multi_material_layer, (int)after.layers.size() - 1);
			RecordEdit(state, name, before);
			return true;
		}

		bool MoveLayer(EditorState& state, const std::string& name, int index, int delta,
			std::string& error) {
			Snapshot before;
			if (!GetSnapshot(state, name, before)) {
				error = "multi-material not found: " + name;
				return false;
			}
			if (index < 0 || index >= (int)before.layers.size()) {
				error = "no layer " + std::to_string(index) + " in " + name;
				return false;
			}
			const int target = (std::max)(0, (std::min)((int)before.layers.size() - 1, index + delta));
			if (target == index) {
				return true; //already at the end it was pushed towards
			}
			Snapshot after = before;
			std::swap(after.layers[index], after.layers[target]);
			if (!ApplySnapshot(state, name, after, error)) {
				return false;
			}
			state.selected_multi_material_layer = target;
			RecordEdit(state, name, before);
			return true;
		}

		bool SetLayer(EditorState& state, const std::string& name, int index,
			const nlohmann::json& fields, std::string& error) {
			Snapshot before;
			if (!GetSnapshot(state, name, before)) {
				error = "multi-material not found: " + name;
				return false;
			}
			if (index < 0 || index >= (int)before.layers.size()) {
				error = "no layer " + std::to_string(index) + " in " + name;
				return false;
			}
			if (!fields.is_object()) {
				error = "expected a JSON object of layer fields";
				return false;
			}
			Snapshot after = before;
			Core::MultiMaterialLayer& layer = after.layers[index];

			std::string material;
			if (Read(fields, "material", material) || Read(fields, "texture", material)) {
				if (state.world->GetMaterials().Get(material) == nullptr ||
					state.world->IsMaterialRemoved(material)) {
					error = "material not found: " + material;
					return false;
				}
				layer.material = material;
			}
			Read(fields, "mask", layer.mask);
			Read(fields, "value", layer.value);
			Read(fields, "uv_scale", layer.uv_scale);
			Read(fields, "mask_channel", layer.mask_channel);
			layer.mask_channel = (std::max)(0, (std::min)(3, layer.mask_channel));
			ReadBool(fields, "mask_invert", layer.mask_invert);
			Read(fields, "mask_uv_scale", layer.mask_uv_scale);
			Read(fields, "mask_uv_offset_u", layer.mask_uv_offset.x);
			Read(fields, "mask_uv_offset_v", layer.mask_uv_offset.y);
			ReadBool(fields, "mask_noise", layer.mask_noise);
			ReadBool(fields, "uv_noise", layer.uv_noise);
			uint32_t op = 0;
			if (Read(fields, "op", op)) {
				if (op < TEXT_OP_MIX || op > TEXT_OP_MULT) {
					error = "op must be 1 (mix), 2 (add) or 3 (mult)";
					return false;
				}
				layer.op = op;
			}
			ReadBool(fields, "slope_enabled", layer.slope_enabled);
			Read(fields, "slope_min", layer.slope_min);
			Read(fields, "slope_max", layer.slope_max);
			Read(fields, "slope_fade", layer.slope_fade);
			ReadBool(fields, "height_enabled", layer.height_enabled);
			Read(fields, "height_min", layer.height_min);
			Read(fields, "height_max", layer.height_max);
			Read(fields, "height_fade", layer.height_fade);

			if (!ApplySnapshot(state, name, after, error)) {
				return false;
			}
			RecordEdit(state, name, before);
			return true;
		}

		bool SetParams(EditorState& state, const std::string& name,
			const nlohmann::json& fields, std::string& error) {
			Snapshot before;
			if (!GetSnapshot(state, name, before)) {
				error = "multi-material not found: " + name;
				return false;
			}
			if (!fields.is_object()) {
				error = "expected a JSON object of parameters";
				return false;
			}
			Snapshot after = before;
			Read(fields, "parallax_scale", after.multi_parallax_scale);
			Read(fields, "tess_type", after.tessellation_type);
			Read(fields, "tess_factor", after.tessellation_factor);
			Read(fields, "displacement_scale", after.displacement_scale);
			if (!ApplySnapshot(state, name, after, error)) {
				return false;
			}
			RecordEdit(state, name, before);
			return true;
		}

		bool Assign(EditorState& state, const std::string& material_name,
			const std::string& name, std::string& error) {
			if (state.world == nullptr) {
				error = "no scene loaded";
				return false;
			}
			Core::MaterialData* material = state.world->GetMaterials().Get(material_name);
			if (material == nullptr || state.world->IsMaterialRemoved(material_name)) {
				error = "material not found: " + material_name;
				return false;
			}
			if (!name.empty() && Find(state, name) == nullptr) {
				error = "multi-material not found: " + name;
				return false;
			}
			const std::string previous = material->multi_material_name;
			if (previous == name) {
				return true; //no-op, nothing to record
			}
			if (!state.world->SetMaterialMultiMaterial(material_name, name)) {
				error = "could not attach " + name + " to " + material_name;
				return false;
			}
			const std::string file = state.world->GetMaterialOrigin(material_name);
			if (!file.empty()) {
				state.dirty_material_files.insert(file);
			}
			MaterialPreview::Invalidate(material_name);

			const std::string target = material_name;
			const std::string assigned = name;
			EditorHistory::Push({
				assigned.empty() ? ("detach multi-material from " + target)
								 : ("attach " + assigned + " to " + target),
				[target, previous, file](EditorState& s) {
					s.world->SetMaterialMultiMaterial(target, previous);
					if (!file.empty()) { s.dirty_material_files.insert(file); }
					MaterialPreview::Invalidate(target);
				},
				[target, assigned, file](EditorState& s) {
					s.world->SetMaterialMultiMaterial(target, assigned);
					if (!file.empty()) { s.dirty_material_files.insert(file); }
					MaterialPreview::Invalidate(target);
				} });
			return true;
		}

		nlohmann::json LayerJson(EditorState& state, const std::string& name, int index) {
			Core::MultiMaterialData* mm = Find(state, name);
			if (mm == nullptr || index < 0 || index >= (int)mm->layers.size()) {
				return nlohmann::json::object();
			}
			const Core::MultiMaterialLayer& layer = mm->layers[index];
			return nlohmann::json{
				{"index", index},
				{"material", layer.material},
				{"mask", layer.mask},
				{"mask_channel", layer.mask_channel},
				{"mask_invert", layer.mask_invert},
				{"mask_uv_scale", layer.mask_uv_scale},
				{"mask_uv_offset_u", layer.mask_uv_offset.x},
				{"mask_uv_offset_v", layer.mask_uv_offset.y},
				{"mask_noise", layer.mask_noise},
				{"uv_noise", layer.uv_noise},
				{"op", layer.op & TEXT_OP_MASK},
				{"value", layer.value},
				{"uv_scale", layer.uv_scale},
				{"slope_enabled", layer.slope_enabled},
				{"slope_min", layer.slope_min},
				{"slope_max", layer.slope_max},
				{"slope_fade", layer.slope_fade},
				{"height_enabled", layer.height_enabled},
				{"height_min", layer.height_min},
				{"height_max", layer.height_max},
				{"height_fade", layer.height_fade},
				//Derived, not authored: which maps the layer's material actually turned
				//out to have, and whether a mask texture resolved. This is the readback
				//that answers "why is my layer invisible".
				{"flags", (index < (int)mm->multi_texture_operation.size())
							? mm->multi_texture_operation[index] : 0u},
				{"resolved", index < (int)mm->multi_texture_data.size() &&
							 mm->multi_texture_data[index] != nullptr}
			};
		}
	}

	namespace MultiMaterialPanel {
		namespace {

			//Coalesced drag state, same shape as the Materials panel's: the pre-edit
			//snapshot is taken when a widget is grabbed and the history action pushed
			//when it is released, so a slider drag is one undo step.
			MultiMaterialOps::Snapshot pending_before;
			bool pending_valid = false;
			//Which stack `pending_before` belongs to - switching selection mid-drag
			//would otherwise write one stack's values into another's history.
			std::string pending_name;

			const char* const OP_NAMES[4] = { "(none)", "Mix", "Add", "Multiply" };
			const char* const CHANNEL_NAMES[4] = { "R", "G", "B", "A" };

			void DrawCreateModal(EditorState& state) {
				static char name_buf[128] = "";
				static int file_index = 0;

				if (!ImGui::BeginPopupModal("Create Multi-Material", nullptr,
					ImGuiWindowFlags_AlwaysAutoResize)) {
					return;
				}
				std::vector<std::string> files;
				for (const auto& entry : state.world->GetMaterialFiles()) {
					files.push_back(entry.first);
				}
				if (files.empty()) {
					ImGui::TextWrapped("This level declares no material files, so there is "
						"nowhere to put a multi-material.");
					if (ImGui::Button("Close")) {
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
					return;
				}
				file_index = (std::min)(file_index, (int)files.size() - 1);

				ImGui::InputText("Name", name_buf, sizeof(name_buf));
				if (ImGui::BeginCombo("File", files[file_index].c_str())) {
					for (int i = 0; i < (int)files.size(); ++i) {
						if (ImGui::Selectable(files[i].c_str(), i == file_index)) {
							file_index = i;
						}
					}
					ImGui::EndCombo();
				}
				if (ImGui::Button("Create")) {
					std::string error;
					if (MultiMaterialOps::Create(state, name_buf, files[file_index], error)) {
						name_buf[0] = '\0';
						ImGui::CloseCurrentPopup();
					}
					else {
						state.status_message = "Create multi-material failed: " + error;
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			//Which materials wear this stack, and a picker to add another. This is how a
			//stack gets used at all, so it sits at the top of the editor rather than in
			//a collapsed section.
			void DrawWearers(EditorState& state, const std::string& name) {
				const std::vector<std::string> wearers = MultiMaterialOps::FindMaterials(state, name);
				ImGui::Text("Worn by %d material(s)", (int)wearers.size());
				for (const std::string& material : wearers) {
					ImGui::PushID(material.c_str());
					ImGui::BulletText("%s", material.c_str());
					ImGui::SameLine();
					if (ImGui::SmallButton("Detach")) {
						std::string error;
						if (!MultiMaterialOps::Assign(state, material, std::string(), error)) {
							state.status_message = "Detach failed: " + error;
						}
					}
					ImGui::PopID();
				}
				if (ImGui::BeginCombo("Attach to", "(pick a material)")) {
					for (const std::string& material : MaterialOps::ListMaterials(state)) {
						if (std::find(wearers.begin(), wearers.end(), material) != wearers.end()) {
							continue;
						}
						if (ImGui::Selectable(material.c_str())) {
							std::string error;
							if (!MultiMaterialOps::Assign(state, material, name, error)) {
								state.status_message = "Attach failed: " + error;
							}
						}
					}
					ImGui::EndCombo();
				}
			}

			//One layer's fields. Edits the working copy and reports what changed; the
			//caller applies and records, so a whole section is one undo step.
			void DrawLayerFields(EditorState& state, Core::MultiMaterialLayer& layer,
				bool& changed, bool& activated, bool& finished) {
				auto track = [&](bool widget_changed) {
					changed |= widget_changed;
					activated |= ImGui::IsItemActivated();
					finished |= ImGui::IsItemDeactivatedAfterEdit();
				};
				//A dropdown's value is picked inside a popup, so it never reports the
				//activate/deactivate pair a drag does - it has to commit on the spot.
				auto discrete = [&](bool widget_changed) {
					if (widget_changed) {
						changed = true;
						finished = true;
					}
				};

				if (ImGui::BeginCombo("Material", layer.material.c_str())) {
					for (const std::string& material : MaterialOps::ListMaterials(state)) {
						if (ImGui::Selectable(material.c_str(), material == layer.material) &&
							material != layer.material) {
							layer.material = material;
							discrete(true);
						}
					}
					ImGui::EndCombo();
				}
				const int op_index = (int)(layer.op & TEXT_OP_MASK);
				if (ImGui::BeginCombo("Blend", OP_NAMES[op_index & 3])) {
					for (int i = TEXT_OP_MIX; i <= TEXT_OP_MULT; ++i) {
						if (ImGui::Selectable(OP_NAMES[i], i == op_index) && i != op_index) {
							layer.op = (uint32_t)i;
							discrete(true);
						}
					}
					ImGui::EndCombo();
				}
				track(ImGui::SliderFloat("Weight", &layer.value, 0.0f, 1.0f));
				track(ImGui::DragFloat("UV scale", &layer.uv_scale, 0.01f, 0.0f, 256.0f));

				ImGui::SeparatorText("Mask");
				char mask_buf[512];
				snprintf(mask_buf, sizeof(mask_buf), "%s", layer.mask.c_str());
				if (ImGui::InputText("Image", mask_buf, sizeof(mask_buf),
					ImGuiInputTextFlags_EnterReturnsTrue)) {
					layer.mask = mask_buf;
					discrete(true);
				}
				ImGui::TextDisabled("Relative to the assets path. Press Enter to apply.");
				if (ImGui::BeginCombo("Channel", CHANNEL_NAMES[layer.mask_channel & 3])) {
					for (int i = 0; i < 4; ++i) {
						if (ImGui::Selectable(CHANNEL_NAMES[i], i == layer.mask_channel) &&
							i != layer.mask_channel) {
							layer.mask_channel = i;
							discrete(true);
						}
					}
					ImGui::EndCombo();
				}
				//The mask's own UV transform, not the layer's uv_scale above: a splat map
				//covers the surface once while its detail maps tile many times over, and
				//mesh UVs are laid out for the second. A terrain whose UVs run 0..24 needs
				//a mask scale of 1/24 to be painted across it exactly once.
				track(ImGui::DragFloat("Mask UV scale", &layer.mask_uv_scale, 0.001f, 0.0f, 256.0f,
					"%.4f"));
				track(ImGui::DragFloat2("Mask UV offset", &layer.mask_uv_offset.x, 0.005f));
				discrete(ImGui::Checkbox("Invert mask", &layer.mask_invert));
				ImGui::SameLine();
				discrete(ImGui::Checkbox("Break up with noise", &layer.mask_noise));
				discrete(ImGui::Checkbox("Noisy UVs", &layer.uv_noise));

				ImGui::SeparatorText("Orientation");
				discrete(ImGui::Checkbox("By slope", &layer.slope_enabled));
				ImGui::SameLine();
				ImGui::TextDisabled("(1 = facing up, 0 = vertical wall, -1 = overhang)");
				ImGui::BeginDisabled(!layer.slope_enabled);
				track(ImGui::SliderFloat("Slope from", &layer.slope_min, -1.0f, 1.0f));
				track(ImGui::SliderFloat("Slope to", &layer.slope_max, -1.0f, 1.0f));
				track(ImGui::SliderFloat("Slope fade", &layer.slope_fade, 0.0f, 1.0f));
				ImGui::EndDisabled();

				ImGui::SeparatorText("Altitude");
				discrete(ImGui::Checkbox("By height", &layer.height_enabled));
				ImGui::BeginDisabled(!layer.height_enabled);
				track(ImGui::DragFloat("Height from", &layer.height_min, 0.1f));
				track(ImGui::DragFloat("Height to", &layer.height_max, 0.1f));
				track(ImGui::DragFloat("Height fade", &layer.height_fade, 0.1f, 0.0f, 1000.0f));
				ImGui::EndDisabled();
			}

			void DrawLayers(EditorState& state, const std::string& name) {
				MultiMaterialOps::Snapshot working;
				if (!MultiMaterialOps::GetSnapshot(state, name, working)) {
					return;
				}
				const int layer_count = (int)working.layers.size();

				ImGui::BeginDisabled(layer_count >= MAX_MULTI_TEXTURE);
				if (ImGui::Button("Add layer")) {
					ImGui::OpenPopup("##add_layer");
				}
				ImGui::EndDisabled();
				if (ImGui::BeginPopup("##add_layer")) {
					for (const std::string& material : MaterialOps::ListMaterials(state)) {
						if (ImGui::Selectable(material.c_str())) {
							std::string error;
							if (!MultiMaterialOps::AddLayer(state, name, material, error)) {
								state.status_message = "Add layer failed: " + error;
							}
						}
					}
					ImGui::EndPopup();
				}
				if (layer_count == 0) {
					ImGui::TextDisabled("No layers yet. A multi-material with no layers draws "
						"as the plain material it is attached to.");
					return;
				}

				state.selected_multi_material_layer =
					(std::max)(0, (std::min)(state.selected_multi_material_layer, layer_count - 1));

				//The layer list: order is the blend order, so the up/down buttons are as
				//much a part of authoring as the values are.
				for (int i = 0; i < layer_count; ++i) {
					ImGui::PushID(i);
					const bool selected = (i == state.selected_multi_material_layer);
					char label[256];
					snprintf(label, sizeof(label), "%d  %s  (%s)", i,
						working.layers[i].material.c_str(),
						OP_NAMES[working.layers[i].op & TEXT_OP_MASK & 3]);
					if (ImGui::Selectable(label, selected)) {
						state.selected_multi_material_layer = i;
					}
					ImGui::SameLine();
					std::string error;
					if (ImGui::SmallButton("^") &&
						!MultiMaterialOps::MoveLayer(state, name, i, -1, error)) {
						state.status_message = "Move layer failed: " + error;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("v") &&
						!MultiMaterialOps::MoveLayer(state, name, i, 1, error)) {
						state.status_message = "Move layer failed: " + error;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("X") &&
						!MultiMaterialOps::RemoveLayer(state, name, i, error)) {
						state.status_message = "Remove layer failed: " + error;
					}
					ImGui::PopID();
					if (i >= (int)working.layers.size()) {
						break; //a button above removed one; the rest is redrawn next frame
					}
				}

				ImGui::Separator();
				const int index = state.selected_multi_material_layer;
				if (index < 0 || index >= (int)working.layers.size()) {
					return;
				}
				ImGui::PushID("layer_fields");
				bool changed = false, activated = false, finished = false;
				DrawLayerFields(state, working.layers[index], changed, activated, finished);
				ImGui::PopID();

				if (activated && (!pending_valid || pending_name != name)) {
					MultiMaterialOps::GetSnapshot(state, name, pending_before);
					pending_name = name;
					pending_valid = true;
				}
				if (changed) {
					//Applied live, so the viewport shows the blend as the slider moves;
					//history is only pushed when the drag ends.
					MultiMaterialOps::Snapshot before;
					const bool have_before = MultiMaterialOps::GetSnapshot(state, name, before);
					std::string error;
					if (!MultiMaterialOps::ApplySnapshot(state, name, working, error)) {
						state.status_message = "Layer edit failed: " + error;
					}
					else if (finished && !pending_valid && have_before) {
						//A discrete widget with no grab: record against what was there a
						//moment ago rather than dropping the step.
						MultiMaterialOps::RecordEdit(state, name, before);
					}
				}
				if (finished && pending_valid && pending_name == name) {
					MultiMaterialOps::RecordEdit(state, name, pending_before);
					pending_valid = false;
				}

				ImGui::Separator();
				MaskPaint::DrawSection(state, name, index);
			}

			void DrawParams(EditorState& state, const std::string& name) {
				MultiMaterialOps::Snapshot working;
				if (!MultiMaterialOps::GetSnapshot(state, name, working)) {
					return;
				}
				//Displacement and tessellation come from the stack rather than the
				//material when one is attached (RenderSystem::PrepareMultiMaterial), so
				//they are edited here or not at all.
				bool changed = false;
				changed |= ImGui::DragFloat("Parallax", &working.multi_parallax_scale, 0.005f, 0.0f, 1.0f);
				changed |= ImGui::DragFloat("Displace", &working.displacement_scale, 0.01f);
				changed |= ImGui::DragFloat("Tessellate", &working.tessellation_factor, 0.1f, 0.0f, 64.0f);
				int tess_type = (int)working.tessellation_type;
				if (ImGui::DragInt("Tess type", &tess_type, 1.0f, 0, 3)) {
					working.tessellation_type = (uint32_t)tess_type;
					changed = true;
				}
				if (ImGui::IsItemActivated() || changed) {
					if (!pending_valid || pending_name != name) {
						MultiMaterialOps::GetSnapshot(state, name, pending_before);
						pending_name = name;
						pending_valid = true;
					}
					std::string error;
					MultiMaterialOps::ApplySnapshot(state, name, working, error);
				}
				if (ImGui::IsItemDeactivatedAfterEdit() && pending_valid && pending_name == name) {
					MultiMaterialOps::RecordEdit(state, name, pending_before);
					pending_valid = false;
				}
			}
		}

		void Draw(EditorState& state) {
			if (state.world == nullptr) {
				return;
			}
			if (ImGui::Button("New...")) {
				ImGui::OpenPopup("Create Multi-Material");
			}
			ImGui::SameLine();
			const bool has_selection = !state.selected_multi_material.empty();
			ImGui::BeginDisabled(!has_selection);
			if (ImGui::Button("Duplicate")) {
				std::string candidate = state.selected_multi_material + "_copy";
				int suffix = 2;
				while (state.world->GetMultiMaterial(candidate) != nullptr) {
					candidate = state.selected_multi_material + "_copy" + std::to_string(suffix++);
				}
				std::string error;
				if (!MultiMaterialOps::Duplicate(state, state.selected_multi_material,
					candidate, error)) {
					state.status_message = "Duplicate failed: " + error;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove")) {
				ImGui::OpenPopup("Remove Multi-Material");
			}
			ImGui::EndDisabled();

			DrawCreateModal(state);
			if (ImGui::BeginPopupModal("Remove Multi-Material", nullptr,
				ImGuiWindowFlags_AlwaysAutoResize)) {
				const std::vector<std::string> wearers =
					MultiMaterialOps::FindMaterials(state, state.selected_multi_material);
				ImGui::Text("Remove multi-material '%s'?", state.selected_multi_material.c_str());
				if (!wearers.empty()) {
					ImGui::TextWrapped("%d material(s) wearing it will go back to drawing "
						"with their own maps.", (int)wearers.size());
				}
				if (ImGui::Button("Remove")) {
					std::string error;
					if (!MultiMaterialOps::Remove(state, state.selected_multi_material, error)) {
						state.status_message = "Remove failed: " + error;
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			ImGui::Separator();
			const std::vector<std::string> names = MultiMaterialOps::List(state);
			if (names.empty()) {
				ImGui::TextWrapped("This level has no multi-materials. A multi-material blends "
					"several materials over one surface - a terrain of dirt, grass and rock "
					"through one mask image, with snow on whatever faces up. Create one, add "
					"layers to it, then attach it to the material the surface already uses.");
				return;
			}

			const float list_width = ImGui::GetContentRegionAvail().x * 0.30f;
			if (ImGui::BeginChild("##mm_list", ImVec2(list_width, 0.0f), ImGuiChildFlags_Border)) {
				for (const std::string& name : names) {
					const std::string file = state.world->GetMultiMaterialOrigin(name);
					ImGui::PushID(name.c_str());
					if (ImGui::Selectable(name.c_str(), name == state.selected_multi_material)) {
						state.selected_multi_material = name;
						state.selected_multi_material_layer = 0;
					}
					Core::MultiMaterialData* mm = state.world->GetMultiMaterial(name);
					ImGui::TextDisabled("%d layer(s)%s", mm != nullptr ? (int)mm->layers.size() : 0,
						state.dirty_material_files.count(file) != 0 ? "  *" : "");
					ImGui::PopID();
				}
			}
			ImGui::EndChild();
			ImGui::SameLine();
			if (ImGui::BeginChild("##mm_edit", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border)) {
				if (state.selected_multi_material.empty() ||
					state.world->GetMultiMaterial(state.selected_multi_material) == nullptr) {
					ImGui::TextDisabled("Select a multi-material to edit it.");
				}
				else {
					const std::string& name = state.selected_multi_material;
					ImGui::Text("%s", name.c_str());
					const std::string file = state.world->GetMultiMaterialOrigin(name);
					ImGui::TextDisabled("File: %s%s", file.empty() ? "(none)" : file.c_str(),
						state.dirty_material_files.count(file) != 0 ? "  (unsaved)" : "");
					ImGui::Separator();
					DrawWearers(state, name);
					ImGui::Separator();
					if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
						DrawParams(state, name);
					}
					if (ImGui::CollapsingHeader("Layers", ImGuiTreeNodeFlags_DefaultOpen)) {
						DrawLayers(state, name);
					}
				}
			}
			ImGui::EndChild();
		}
	}
}
