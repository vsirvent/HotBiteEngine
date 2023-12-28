#pragma once



#include <any>
#include <string>
#include <map>
#include <stdint.h>
#include <optional>

#include <msclr/marshal_cppstd.h>
#include <string>

#include <Core\Json.h>
#include <DirectXMath.h>

using namespace nlohmann;
using namespace System;
using namespace System::ComponentModel;
using namespace System::ComponentModel::DataAnnotations;
using namespace System::ComponentModel;
using namespace System::Globalization;
using namespace System::Windows::Forms;
using namespace System::Drawing::Design;
using namespace System::IO;

namespace HotBite {
	namespace Engine {
		namespace Core {
			typedef DirectX::XMFLOAT4   float4;
			typedef DirectX::XMFLOAT3   float3;

			float3 parseColorStringF3(const std::string& colorString);
			float4 parseColorStringF4(const std::string& colorString);
		}
	}
}

class AbstractProp {
public:
	virtual ~AbstractProp() {};
	template<typename T>
	void SetValue(const T& v);
	template<typename T>
	const T& GetValue();
};

template <typename T>
class Prop : public AbstractProp {
public:
	Prop();
	Prop(T _val): value(_val) {}
	~Prop() {};
	T value;
};

template<typename T>
void AbstractProp::SetValue(const T& v) {
	Prop<T>* p = dynamic_cast<Prop<T>*>(this);
	p->value = v;
}

template<typename T>
const T& AbstractProp::GetValue() {
	Prop<T>* p = dynamic_cast<Prop<T>*>(this);
	return p->value;
}

public interface class IMaterialListener {
public:
	virtual void OnNameChanged(String^ old_name, String^ new_name) abstract;
};

class Material {
public:
	std::map<std::string, std::shared_ptr<AbstractProp>> props;
	gcroot<IMaterialListener^> listener = nullptr;
	Material(IMaterialListener^ l) : listener(l)
	{
		props["name"] = std::make_shared<Prop<std::string>>("material");
		props["diffuse_color"] = std::make_shared<Prop<std::string>>("#FFFFFFFF");
		props["ambient_color"] = std::make_shared<Prop<std::string>>("#FFFFFFFF");
		props["parallax_scale"] = std::make_shared<Prop<float>>(0.0f);
		props["tess_type"] = std::make_shared<Prop<int>>(0);
		props["tess_factor"] = std::make_shared<Prop<float>>(0.0f);
		props["displacement_scale"] = std::make_shared<Prop<float>>(0.0f);
		props["bloom_scale"] = std::make_shared<Prop<float>>(0.0f);
		props["normal_map_enabled"] = std::make_shared<Prop<bool>>(false);
		props["alpha_enabled"] = std::make_shared<Prop<bool>>(false);
		props["blend_enabled"] = std::make_shared<Prop<bool>>(false);
		props["diffuse_textname"] = std::make_shared<Prop<std::string>>("");
		props["normal_textname"] = std::make_shared<Prop<std::string>>("");
		props["high_textname"] = std::make_shared<Prop<std::string>>("");
		props["spec_textname"] = std::make_shared<Prop<std::string>>("");
		props["ao_textname"] = std::make_shared<Prop<std::string>>("");
		props["arm_textname"] = std::make_shared<Prop<std::string>>("");
		props["emission_textname"] = std::make_shared<Prop<std::string>>("");
		props["vs"] = std::make_shared<Prop<std::string>>("");
		props["hs"] = std::make_shared<Prop<std::string>>("");
		props["ds"] = std::make_shared<Prop<std::string>>("");
		props["gs"] = std::make_shared<Prop<std::string>>("");
		props["ps"] = std::make_shared<Prop<std::string>>("");
	};

	virtual bool FromJson(const json& js) {
		try {
			props["name"]->SetValue<std::string>(js["name"]);
			props["diffuse_color"]->SetValue<std::string>(js["diffuse_color"]);
			props["ambient_color"]->SetValue<std::string>(js["ambient_color"]);
			props["parallax_scale"]->SetValue<float>(js["parallax_scale"]);
			props["tess_type"]->SetValue<int>(js["tess_type"]);
			props["tess_factor"]->SetValue<float>(js["tess_factor"]);
			props["displacement_scale"]->SetValue<float>(js["displacement_scale"]);
			props["bloom_scale"]->SetValue<float>(js["bloom_scale"]);
			props["normal_map_enabled"]->SetValue<bool>(js["normal_map_enabled"]);
			props["alpha_enabled"]->SetValue<bool>(js["alpha_enabled"]);
			props["blend_enabled"]->SetValue<bool>(js["blend_enabled"]);
			props["diffuse_textname"]->SetValue<std::string>(js["diffuse_textname"]);
			props["normal_textname"]->SetValue<std::string>(js["normal_textname"]);
			props["high_textname"]->SetValue<std::string>(js["high_textname"]);
			props["spec_textname"]->SetValue<std::string>(js["spec_textname"]);
			props["ao_textname"]->SetValue<std::string>(js["ao_textname"]);
			props["arm_textname"]->SetValue<std::string>(js["arm_textname"]);
			props["emission_textname"]->SetValue<std::string>(js["emission_textname"]);
			props["vs"]->SetValue<std::string>(js["vs"]);
			props["hs"]->SetValue<std::string>(js["hs"]);
			props["ds"]->SetValue<std::string>(js["ds"]);
			props["gs"]->SetValue<std::string>(js["gs"]);
			props["ps"]->SetValue<std::string>(js["ps"]);
		}
		catch (...) {
			return false;
		}
		return true;
	}

	json ToJson() {
		json js;
		for (const auto& ap : props) {
			if (Prop<int32_t>* ip = dynamic_cast<Prop<int32_t>*>(ap.second.get()); ip != nullptr) {
				js[ap.first] = std::any_cast<decltype(ip->value)>(ip->value);
			}
			else if (Prop<float>* fp = dynamic_cast<Prop<float>*>(ap.second.get()); fp != nullptr) {
				js[ap.first] = std::any_cast<decltype(fp->value)>(fp->value);
			}
			else if (Prop<std::string>* sp = dynamic_cast<Prop<std::string>*>(ap.second.get()); sp != nullptr) {
				js[ap.first] = std::any_cast<decltype(sp->value)>(sp->value);
			}
			else if (Prop<bool>* bp = dynamic_cast<Prop<bool>*>(ap.second.get()); bp != nullptr) {
				js[ap.first] = std::any_cast<decltype(bp->value)>(bp->value);
			}
		}
		return js;
	}

	std::optional<std::string> GetString(const std::string& id) const {
		if (const auto& it = props.find(id); it != props.cend()) {
			return it->second->GetValue<std::string>();
		}
		return std::nullopt;
	}

	std::optional<int> GetInt(const std::string& id) const {
		if (const auto& it = props.find(id); it != props.cend()) {
			return it->second->GetValue<int>();
		}
		return std::nullopt;
	}

	std::optional<float> GetFloat(const std::string& id) const {
		if (const auto& it = props.find(id); it != props.cend()) {
			return it->second->GetValue<float>();
		}
		return std::nullopt;
	}

	std::string ToString() {
		json js = ToJson();
		std::string str = js.dump(4);
		size_t pos = str.find("\n");
		while (pos != std::string::npos) {
			str.replace(pos, 1, "\r\n");
			pos = str.find("\n", pos + 2); // Move to the next position after the replacement
		}
		str += ",\r\n";
		return str;
	}
};

public ref class MaterialProp
{
public:
	Material* material;
	System::String^ root_folder;

	[CategoryAttribute("Material")]
	property String^ Name {
		String^ get()
		{
			return gcnew String(material->props["name"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			material->listener->OnNameChanged(Name, newValue);
			std::string str = msclr::interop::marshal_as<std::string>(newValue);
			return material->props["name"]->SetValue<std::string>(str);
		}
	}
	[CategoryAttribute("Material")]
	property Drawing::Color DiffuseColor {
		Drawing::Color get()
		{
			auto color = HotBite::Engine::Core::parseColorStringF4(material->props["diffuse_color"]->GetValue<std::string>());
			return Drawing::Color::FromArgb((int)(color.w * 255.0f), (int)(color.x * 255.0f), (int)(color.y * 255.0f), (int)(color.z * 255.0f));
		}

		void set(Drawing::Color newValue)
		{
			char color[64];
			snprintf(color, 64, "#%02X%02X%02X%02X", newValue.R, newValue.G, newValue.B, newValue.A);
			return material->props["diffuse_color"]->SetValue<std::string>(color);
		}
	}
	
	MaterialProp(Material* material, System::String^ path) {
		this->material = material;
		this->root_folder = path + "\\";
	}
};
