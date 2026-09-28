#include "ComponentSchema.h"
#include "SceneEditor.h"

#include <ECS/ComponentRegistry.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

using namespace HotBite::Engine;
namespace fs = std::filesystem;

namespace HotBiteEditor {

	const SchemaField* ComponentSchema::FindField(const std::string& field) const
	{
		for (const SchemaField& f : fields) {
			if (f.name == field) {
				return &f;
			}
		}
		return nullptr;
	}

	namespace ComponentSchemas {

		namespace {

			//The default config key, and the file looked for when it is absent.
			constexpr const char* DEFAULT_SCHEMA_FILE = "components.schema.json";

			bool ParseType(const std::string& text, SchemaField::Type& type)
			{
				static const std::pair<const char*, SchemaField::Type> TYPES[] = {
					{ "bool", SchemaField::Type::Bool },
					{ "int", SchemaField::Type::Int },
					{ "float", SchemaField::Type::Float },
					{ "string", SchemaField::Type::String },
					{ "enum", SchemaField::Type::Enum },
					{ "vec3", SchemaField::Type::Vector3 },
				};
				for (const auto& [name, value] : TYPES) {
					if (text == name) {
						type = value;
						return true;
					}
				}
				return false;
			}

			//'*' any run, '?' one character; case-insensitive, like the file system
			//the patterns name.
			bool WildcardMatch(const char* pattern, const char* text)
			{
				if (*pattern == '\0') {
					return *text == '\0';
				}
				if (*pattern == '*') {
					return WildcardMatch(pattern + 1, text) || (*text != '\0' && WildcardMatch(pattern, text + 1));
				}
				if (*text == '\0') {
					return false;
				}
				if (*pattern == '?' || std::tolower((unsigned char)*pattern) == std::tolower((unsigned char)*text)) {
					return WildcardMatch(pattern + 1, text + 1);
				}
				return false;
			}

			//"dir/*.json#key": the `key` string of every matching file (or of every
			//element when the file is an array). Read once, at Load - an enum picker is
			//drawn every frame and must not touch the disk.
			void ResolveSource(const fs::path& root, SchemaField& field, const std::string& where,
				std::vector<std::string>& errors)
			{
				const size_t hash = field.source.rfind('#');
				if (hash == std::string::npos || hash + 1 >= field.source.size()) {
					errors.push_back(where + ": source '" + field.source + "' needs a '#key' suffix");
					return;
				}
				const std::string key = field.source.substr(hash + 1);
				fs::path pattern_path(field.source.substr(0, hash));
				if (pattern_path.is_relative()) {
					pattern_path = root / pattern_path;
				}
				const fs::path dir = pattern_path.parent_path();
				const std::string pattern = pattern_path.filename().string();
				std::error_code ec;
				if (!fs::is_directory(dir, ec)) {
					errors.push_back(where + ": source folder not found: " + dir.string());
					return;
				}
				std::set<std::string> found;
				for (const auto& entry : fs::directory_iterator(dir, ec)) {
					if (!entry.is_regular_file() ||
						!WildcardMatch(pattern.c_str(), entry.path().filename().string().c_str())) {
						continue;
					}
					nlohmann::json doc;
					try {
						doc = nlohmann::json::parse(std::ifstream(entry.path()));
					}
					catch (const std::exception&) {
						errors.push_back(where + ": source file is not valid JSON: " + entry.path().string());
						continue;
					}
					auto take = [&](const nlohmann::json& obj) {
						if (obj.is_object() && obj.contains(key) && obj[key].is_string()) {
							found.insert(obj[key].get<std::string>());
						}
					};
					if (doc.is_array()) {
						for (const auto& element : doc) {
							take(element);
						}
					}
					else {
						take(doc);
					}
				}
				for (const std::string& v : found) {
					if (std::find(field.values.begin(), field.values.end(), v) == field.values.end()) {
						field.values.push_back(v);
					}
				}
			}

			//The type's natural zero, used when a field authors no default.
			nlohmann::json ZeroOf(const SchemaField& field)
			{
				switch (field.type) {
				case SchemaField::Type::Bool:    return false;
				case SchemaField::Type::Int:     return 0;
				case SchemaField::Type::Float:   return 0.0;
				case SchemaField::Type::Enum:    return field.values.empty() ? std::string() : field.values.front();
				case SchemaField::Type::Vector3: return nlohmann::json{ {"x", 0.0}, {"y", 0.0}, {"z", 0.0} };
				default:                         return std::string();
				}
			}

			//One value of `field`, normalized to its canonical JSON form. Shared by
			//defaults (checked at load) and edits (checked at Merge).
			bool Normalize(const SchemaField& field, const nlohmann::json& value,
				const nlohmann::json& current, nlohmann::json& out, std::string& error)
			{
				auto clamp = [&](double v) {
					if (field.has_min) { v = (std::max)(v, field.min); }
					if (field.has_max) { v = (std::min)(v, field.max); }
					return v;
				};
				switch (field.type) {
				case SchemaField::Type::Bool:
					if (!value.is_boolean()) {
						error = "'" + field.name + "' must be true or false";
						return false;
					}
					out = value;
					return true;
				case SchemaField::Type::Int:
					if (!value.is_number()) {
						error = "'" + field.name + "' must be a number";
						return false;
					}
					out = (int64_t)std::llround(clamp(value.get<double>()));
					return true;
				case SchemaField::Type::Float:
					if (!value.is_number()) {
						error = "'" + field.name + "' must be a number";
						return false;
					}
					out = clamp(value.get<double>());
					return true;
				case SchemaField::Type::String:
					if (!value.is_string()) {
						error = "'" + field.name + "' must be a string";
						return false;
					}
					out = value;
					return true;
				case SchemaField::Type::Enum:
					if (!value.is_string()) {
						error = "'" + field.name + "' must be a string";
						return false;
					}
					if (std::find(field.values.begin(), field.values.end(), value.get<std::string>()) == field.values.end()) {
						error = "'" + value.get<std::string>() + "' is not one of " + field.name + "'s values";
						return false;
					}
					out = value;
					return true;
				case SchemaField::Type::Vector3: {
					if (!value.is_object()) {
						error = "'" + field.name + "' must be an object {x, y, z}";
						return false;
					}
					nlohmann::json v = current.is_object() ? current : ZeroOf(field);
					for (const char* axis : { "x", "y", "z" }) {
						if (value.contains(axis)) {
							if (!value[axis].is_number()) {
								error = "'" + field.name + "." + axis + "' must be a number";
								return false;
							}
							v[axis] = clamp(value[axis].get<double>());
						}
						else if (!v.contains(axis) || !v[axis].is_number()) {
							v[axis] = 0.0;
						}
					}
					out = nlohmann::json{ {"x", v["x"]}, {"y", v["y"]}, {"z", v["z"]} };
					return true;
				}
				}
				return false;
			}

			bool ParseField(const fs::path& root, const std::string& component, const std::string& name,
				const nlohmann::ordered_json& spec, SchemaField& field, std::vector<std::string>& errors)
			{
				const std::string where = component + "." + name;
				field.name = name;
				if (!spec.is_object() || !spec.contains("type") || !spec["type"].is_string()) {
					errors.push_back(where + ": needs a \"type\"");
					return false;
				}
				if (!ParseType(spec["type"].get<std::string>(), field.type)) {
					errors.push_back(where + ": unknown type '" + spec["type"].get<std::string>() +
						"' (bool, int, float, string, enum, vec3)");
					return false;
				}
				if (spec.contains("min") && spec["min"].is_number()) {
					field.has_min = true;
					field.min = spec["min"].get<double>();
				}
				if (spec.contains("max") && spec["max"].is_number()) {
					field.has_max = true;
					field.max = spec["max"].get<double>();
				}
				if (spec.contains("tooltip") && spec["tooltip"].is_string()) {
					field.tooltip = spec["tooltip"].get<std::string>();
				}
				if (field.type == SchemaField::Type::Enum) {
					if (spec.contains("values") && spec["values"].is_array()) {
						for (const auto& v : spec["values"]) {
							if (v.is_string()) {
								field.values.push_back(v.get<std::string>());
							}
						}
					}
					if (spec.contains("source") && spec["source"].is_string()) {
						field.source = spec["source"].get<std::string>();
						ResolveSource(root, field, where, errors);
					}
					if (field.values.empty()) {
						//Still loads, so the rest of the component is usable - as a free
						//string, which is what the game will read anyway.
						errors.push_back(where + ": enum has no values (none listed, none found at its source); "
							"edited as a plain string");
						field.type = SchemaField::Type::String;
					}
				}
				if (spec.contains("gizmo") && spec["gizmo"].is_string()) {
					const std::string gizmo = spec["gizmo"].get<std::string>();
					if (gizmo == "sphere" && field.type == SchemaField::Type::Float) {
						field.gizmo = gizmo;
					}
					else if (gizmo == "box" && field.type == SchemaField::Type::Vector3) {
						field.gizmo = gizmo;
					}
					else {
						errors.push_back(where + ": gizmo '" + gizmo + "' needs a float (sphere) or a vec3 (box); ignored");
					}
				}

				const nlohmann::json zero = ZeroOf(field);
				field.default_value = zero;
				if (spec.contains("default")) {
					nlohmann::json normalized;
					std::string error;
					//ordered_json -> json: the value is a scalar or {x,y,z}, so key order
					//is irrelevant from here on.
					const nlohmann::json authored = nlohmann::json::parse(spec["default"].dump());
					if (Normalize(field, authored, zero, normalized, error)) {
						field.default_value = normalized;
					}
					else {
						errors.push_back(where + ": default ignored: " + error);
					}
				}
				else if (field.type == SchemaField::Type::Int || field.type == SchemaField::Type::Float) {
					//A zero outside the range would be a default the field itself refuses.
					nlohmann::json normalized;
					std::string ignored;
					if (Normalize(field, zero, zero, normalized, ignored)) {
						field.default_value = normalized;
					}
				}
				return true;
			}

			void LoadFile(const fs::path& root, const fs::path& file, ComponentSchemaSet& set)
			{
				const std::string file_name = file.string();
				std::error_code ec;
				if (!fs::is_regular_file(file, ec)) {
					set.errors.push_back(file_name + ": not found");
					return;
				}
				nlohmann::ordered_json doc;
				try {
					doc = nlohmann::ordered_json::parse(std::ifstream(file));
				}
				catch (const std::exception& ex) {
					set.errors.push_back(file_name + ": not valid JSON: " + ex.what());
					return;
				}
				set.files.push_back(file_name);
				if (!doc.is_object() || !doc.contains("components") || !doc["components"].is_array()) {
					set.errors.push_back(file_name + ": needs a \"components\" array");
					return;
				}
				for (const auto& entry : doc["components"]) {
					if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string() ||
						entry["name"].get<std::string>().empty()) {
						set.errors.push_back(file_name + ": a component without a \"name\" was skipped");
						continue;
					}
					ComponentSchema schema;
					schema.name = entry["name"].get<std::string>();
					schema.file = file_name;
					if (ECS::ComponentRegistry::Instance().Find(schema.name) != nullptr) {
						//The editor's own type wins: it has real code behind it, and a
						//schema cannot describe (or safely replace) what that code does.
						set.errors.push_back(schema.name + ": is a component the editor already defines; "
							"the schema entry is ignored");
						continue;
					}
					if (std::any_of(set.schemas.begin(), set.schemas.end(),
						[&](const ComponentSchema& s) { return s.name == schema.name; })) {
						set.errors.push_back(schema.name + ": declared twice; keeping the first (" +
							file_name + ")");
						continue;
					}
					if (entry.contains("description") && entry["description"].is_string()) {
						schema.description = entry["description"].get<std::string>();
					}
					if (entry.contains("fields") && entry["fields"].is_object()) {
						for (const auto& [field_name, spec] : entry["fields"].items()) {
							SchemaField field;
							if (ParseField(root, schema.name, field_name, spec, field, set.errors)) {
								schema.fields.push_back(std::move(field));
							}
						}
					}
					set.schemas.push_back(std::move(schema));
				}
			}
		}

		void Load(EditorState& state)
		{
			state.component_schemas = ComponentSchemaSet{};
			if (state.project_root.empty()) {
				return;
			}
			const fs::path root(state.project_root);

			std::vector<fs::path> files;
			bool configured = false;
			std::error_code ec;
			const fs::path config = root / "config.json";
			if (fs::is_regular_file(config, ec)) {
				try {
					nlohmann::json cfg = nlohmann::json::parse(std::ifstream(config));
					if (cfg.contains("editor") && cfg["editor"].is_object() &&
						cfg["editor"].contains("component_schemas")) {
						configured = true;
						const nlohmann::json& list = cfg["editor"]["component_schemas"];
						if (!list.is_array()) {
							state.component_schemas.errors.push_back(
								"config.json: editor.component_schemas must be an array of paths");
						}
						else {
							for (const auto& p : list) {
								if (p.is_string()) {
									fs::path file(p.get<std::string>());
									files.push_back(file.is_relative() ? root / file : file);
								}
							}
						}
					}
				}
				catch (const std::exception& ex) {
					state.component_schemas.errors.push_back(std::string("config.json: ") + ex.what());
				}
			}
			if (!configured) {
				const fs::path fallback = root / DEFAULT_SCHEMA_FILE;
				if (fs::is_regular_file(fallback, ec)) {
					files.push_back(fallback);
				}
			}
			for (const fs::path& file : files) {
				LoadFile(root, file.lexically_normal(), state.component_schemas);
			}
		}

		const ComponentSchema* Find(const EditorState& state, const std::string& component)
		{
			for (const ComponentSchema& s : state.component_schemas.schemas) {
				if (s.name == component) {
					return &s;
				}
			}
			return nullptr;
		}

		nlohmann::json Defaults(const ComponentSchema& schema)
		{
			nlohmann::json out = nlohmann::json::object();
			for (const SchemaField& f : schema.fields) {
				out[f.name] = f.default_value;
			}
			return out;
		}

		bool Merge(const ComponentSchema& schema, const nlohmann::json& current,
			const nlohmann::json& payload, nlohmann::json& out, std::string& error)
		{
			if (!payload.is_object()) {
				error = "a component value must be a JSON object";
				return false;
			}
			nlohmann::json result = current.is_object() ? current : nlohmann::json::object();
			for (const auto& [key, value] : payload.items()) {
				const SchemaField* field = schema.FindField(key);
				if (field == nullptr) {
					if (result.contains(key) && result[key] == value) {
						continue;
					}
					error = schema.name + " has no field '" + key + "'";
					return false;
				}
				nlohmann::json normalized;
				const nlohmann::json before = result.contains(key) ? result[key] : field->default_value;
				if (!Normalize(*field, value, before, normalized, error)) {
					error = schema.name + ": " + error;
					return false;
				}
				result[key] = normalized;
			}
			out = std::move(result);
			return true;
		}

		const char* TypeName(SchemaField::Type type)
		{
			switch (type) {
			case SchemaField::Type::Bool:    return "bool";
			case SchemaField::Type::Int:     return "int";
			case SchemaField::Type::Float:   return "float";
			case SchemaField::Type::Enum:    return "enum";
			case SchemaField::Type::Vector3: return "vec3";
			default:                         return "string";
			}
		}

		nlohmann::json Describe(const ComponentSchema& schema)
		{
			nlohmann::json fields = nlohmann::json::array();
			for (const SchemaField& f : schema.fields) {
				nlohmann::json j;
				j["name"] = f.name;
				j["type"] = TypeName(f.type);
				j["default"] = f.default_value;
				if (f.has_min) { j["min"] = f.min; }
				if (f.has_max) { j["max"] = f.max; }
				if (f.type == SchemaField::Type::Enum) { j["values"] = f.values; }
				if (!f.source.empty()) { j["source"] = f.source; }
				if (!f.gizmo.empty()) { j["gizmo"] = f.gizmo; }
				fields.push_back(j);
			}
			return nlohmann::json{ {"name", schema.name}, {"description", schema.description},
				{"file", schema.file}, {"fields", fields} };
		}
	}
}
