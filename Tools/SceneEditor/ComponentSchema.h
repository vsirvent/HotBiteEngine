#pragma once

#include <Core/Json.h>
#include <string>
#include <vector>

namespace HotBiteEditor {

	struct EditorState;

	// A game's own components, described as data so the editor can author them
	// without linking a line of game code.
	//
	// The editor is a different executable from the game, so a component a game
	// registers (World::RegisterComponent in *its* process) is never in the editor's
	// ComponentRegistry. Before schemas, such a component was only ever carried
	// through as an opaque block (EditorState::opaque_components): preserved on save,
	// shown read-only, impossible to add. A schema says what the block looks like -
	// its fields, their types, defaults and ranges - and that is all the editor needs
	// to add it, edit it with real widgets, validate it and remove it. The game keeps
	// the behaviour: at runtime it registers a C++ component under the same NAME
	// whose FromJson reads the fields described here.
	//
	// Schema files are JSON, found through the project's config.json:
	//
	//     "editor": { "component_schemas": [ "Data/Editor/components.schema.json" ] }
	//
	// (paths relative to the project root), or, when that key is absent,
	// <project root>/components.schema.json if it exists. Each file:
	//
	//     { "components": [
	//         { "name": "ResourceNode", "description": "...",
	//           "fields": {
	//             "resource": { "type": "enum", "values": ["food", "water"] },
	//             "amount":   { "type": "int", "min": 0, "default": 40 },
	//             "radius":   { "type": "float", "min": 0, "default": 2, "gizmo": "sphere" } } } ] }
	//
	// Field types: bool, int, float, string, enum, vec3 ({"x","y","z"}, the level
	// format's own float3 shape). An enum lists "values", takes them from data with
	// "source": "Data/Buildings/*.json#id" (every matching file's top-level "id", or
	// each element's if the file is an array), or both. "gizmo" draws a hint around
	// the selected entity: "sphere" on a float (a radius), "box" on a vec3 (half
	// extents).
	struct SchemaField {
		enum class Type { Bool, Int, Float, String, Enum, Vector3 };

		std::string name;
		Type type = Type::String;
		nlohmann::json default_value;
		bool has_min = false;
		bool has_max = false;
		double min = 0.0;
		double max = 0.0;
		std::vector<std::string> values; // Enum: allowed values, `source` already resolved
		std::string source;              // Enum: where the values came from, as authored
		std::string gizmo;               // "sphere", "box" or ""
		std::string tooltip;
	};

	struct ComponentSchema {
		std::string name;
		std::string description;
		std::string file; // the schema file that declared it
		std::vector<SchemaField> fields; // in authored order

		const SchemaField* FindField(const std::string& field) const;
	};

	struct ComponentSchemaSet {
		std::vector<ComponentSchema> schemas;
		std::vector<std::string> files;  // the schema files that were read
		std::vector<std::string> errors; // what was wrong with them, one line each
	};

	namespace ComponentSchemas {

		// (Re)reads the project's schema files into state.component_schemas. Never
		// fails: a missing file, a bad field or a name the engine already registers
		// is reported in `errors` and skipped, and the rest still loads.
		void Load(EditorState& state);

		// The schema for `component`, or null. A name the editor's own registry
		// knows is never a schema component (Load refuses it), so this and
		// ComponentRegistry::Find never both answer for the same name.
		const ComponentSchema* Find(const EditorState& state, const std::string& component);

		// Every field at its default, the value a freshly added component gets.
		nlohmann::json Defaults(const ComponentSchema& schema);

		// Applies `payload` over `current` as a delta - absent keys keep their value,
		// exactly as a C++ component's FromJson treats them - checking each key
		// against the schema: a wrong type or an enum value that is not offered is an
		// error; a number out of range is clamped. A key the schema does not declare
		// is refused unless `current` already carries that same value (a field the
		// game has since dropped from its schema survives an undo that re-applies
		// it).
		bool Merge(const ComponentSchema& schema, const nlohmann::json& current,
			const nlohmann::json& payload, nlohmann::json& out, std::string& error);

		const char* TypeName(SchemaField::Type type);

		// A schema as JSON (name, description, file, fields with their type,
		// default, range, values and gizmo) - what `component_schema` reports.
		nlohmann::json Describe(const ComponentSchema& schema);
	}
}
