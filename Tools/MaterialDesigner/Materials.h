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
		props["specular"] = std::make_shared<Prop<float>>(0.5f);
		props["parallax_scale"] = std::make_shared<Prop<float>>(0.0f);
		props["tess_type"] = std::make_shared<Prop<int>>(0);
		props["tess_factor"] = std::make_shared<Prop<float>>(32.0f);
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
			props["specular"]->SetValue<float>(js["specular"]);
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

public ref class TessellationTypeConverter : public TypeConverter
{
public:
	static String^ TESS_NONE = gcnew String("NONE");
	static String^ TESS_BORDER = gcnew String("BORDER");
	static String^ TESS_FULL = gcnew String("FULL");
	
	static String^ GetTessTypeName(int32_t type) {
		switch (type) {
		case 0: return TESS_NONE;
		case 1: return TESS_BORDER;
		case 2: return TESS_FULL;
		default: return TESS_NONE;
		}
	}

	static int GetTessTypeByName(String^ name) {
		if (String::Compare(name, TESS_NONE) == 0) {
			return 0;
		}
		if (String::Compare(name, TESS_BORDER) == 0) {
			return 1;
		}
		if (String::Compare(name, TESS_FULL) == 0) {
			return 2;
		}
		return 0;
	}

	virtual StandardValuesCollection^ GetStandardValues(ITypeDescriptorContext^ context) override
	{
		array<System::String^>^ values = { TESS_NONE, TESS_BORDER, TESS_FULL };
		return gcnew StandardValuesCollection(values);
	}
	virtual bool GetStandardValuesExclusive(ITypeDescriptorContext^ context) override
	{
		return true;
	}
	virtual bool GetStandardValuesSupported(ITypeDescriptorContext^ context) override
	{
		return true;
	}
};

ref class CustomImageEditor : ImageEditor
{
public:
	virtual Object^ EditValue(ITypeDescriptorContext^ context, System::IServiceProvider^ provider, Object^ value) override
	{
		OpenFileDialog^ openFileDialog = gcnew OpenFileDialog();

		openFileDialog->Filter = "Image Files|*.bmp;*.jpg;*.jpeg;*.gif;*.png;*.dds|All Files|*.*";
		openFileDialog->Title = "Select an Image";

		if (value != nullptr && value->GetType() == String::typeid)
		{
			openFileDialog->FileName = dynamic_cast<String^>(value);
		}

		if (openFileDialog->ShowDialog() == DialogResult::OK)
		{
			return openFileDialog->FileName;
		}
		else
		{
			return value;
		}
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
			String^ oldName = Name;
			std::string str = msclr::interop::marshal_as<std::string>(newValue);
			material->props["name"]->SetValue<std::string>(str);
			material->listener->OnNameChanged(oldName, newValue);
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

	[CategoryAttribute("Material")]
	property float SpecularIntensity {
		float get()
		{
			return material->props["specular"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return material->props["specular"]->SetValue<float>((float)newValue);
		}
	}

	[CategoryAttribute("Material")]
	property float ParallaxScale {
		float get()
		{
			return material->props["parallax_scale"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return material->props["parallax_scale"]->SetValue<float>((float)newValue);
		}
	}

	[CategoryAttribute("Material")]
	[TypeConverterAttribute(TessellationTypeConverter::typeid)]
	property System::String^ TessellationType {
		System::String^ get()
		{
			return TessellationTypeConverter::GetTessTypeName(material->props["tess_type"]->GetValue<int32_t>());
		}

		void set(System::String^ newValue)
		{
			return material->props["tess_type"]->SetValue<int32_t>(TessellationTypeConverter::GetTessTypeByName(newValue));
		}
	}

	[CategoryAttribute("Material")]
	property float TessFactor {
		float get()
		{
			return material->props["tess_factor"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return material->props["tess_factor"]->SetValue<float>((float)newValue);
		}
	}
	
	[CategoryAttribute("Material")]
	property float Displacement {
		float get()
		{
			return material->props["displacement_scale"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return material->props["displacement_scale"]->SetValue<float>((float)newValue);
		}
	}

	[CategoryAttribute("Material")]
	property float Bloom {
		float get()
		{
			return material->props["bloom_scale"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return material->props["bloom_scale"]->SetValue<float>((float)newValue);
		}
	}

	[CategoryAttribute("Material")]
	property bool Alpha {
		bool get()
		{
			return material->props["alpha_enabled"]->GetValue<bool>();
		}

		void set(bool newValue)
		{
			return material->props["alpha_enabled"]->SetValue<bool>(newValue);
		}
	}

	[CategoryAttribute("Material")]
	property bool Blend {
		bool get()
		{
			return material->props["blend_enabled"]->GetValue<bool>();
		}

		void set(bool newValue)
		{
			return material->props["blend_enabled"]->SetValue<bool>(newValue);
		}
	}

	[CategoryAttribute("Textures")]
	[EditorAttribute(CustomImageEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ Diffuse {
		String^ get()
		{
			return gcnew String(material->props["diffuse_textname"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			System::String^ file = Path::GetFileName(newValue);
			if (file->Length > 0) {
				System::String^ dest_file = root_folder + file;
				try {
					File::Delete(dest_file);
				}
				catch (...) {}
				File::Copy(newValue, dest_file);
			}
			material->props["diffuse_textname"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}

	[CategoryAttribute("Textures")]
	[EditorAttribute(CustomImageEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ Normal {
		String^ get()
		{
			return gcnew String(material->props["normal_textname"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			System::String^ file = Path::GetFileName(newValue);
			if (file->Length > 0) {
				System::String^ dest_file = root_folder + file;
				try {
					File::Delete(dest_file);
				}
				catch (...) {}
				File::Copy(newValue, dest_file);
			}
			material->props["normal_textname"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}

	[CategoryAttribute("Textures")]
	[EditorAttribute(CustomImageEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ High {
		String^ get()
		{
			return gcnew String(material->props["high_textname"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			System::String^ file = Path::GetFileName(newValue);
			if (file->Length > 0) {
				System::String^ dest_file = root_folder + file;
				try {
					File::Delete(dest_file);
				}
				catch (...) {}
				File::Copy(newValue, dest_file);
			}
			material->props["high_textname"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}

	[CategoryAttribute("Textures")]
	[EditorAttribute(CustomImageEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ Specular {
		String^ get()
		{
			return gcnew String(material->props["spec_textname"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			System::String^ file = Path::GetFileName(newValue);
			if (file->Length > 0) {
				System::String^ dest_file = root_folder + file;
				try {
					File::Delete(dest_file);
				}
				catch (...) {}
				File::Copy(newValue, dest_file);
			}
			material->props["spec_textname"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}

	[CategoryAttribute("Textures")]
	[EditorAttribute(CustomImageEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ AO {
		String^ get()
		{
			return gcnew String(material->props["ao_textname"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			System::String^ file = Path::GetFileName(newValue);
			if (file->Length > 0) {
				System::String^ dest_file = root_folder + file;
				try {
					File::Delete(dest_file);
				}
				catch (...) {}
				File::Copy(newValue, dest_file);
			}
			material->props["ao_textname"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}

	[CategoryAttribute("Textures")]
	[EditorAttribute(CustomImageEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ ARM {
		String^ get()
		{
			return gcnew String(material->props["arm_textname"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			System::String^ file = Path::GetFileName(newValue);
			if (file->Length > 0) {
				System::String^ dest_file = root_folder + file;
				try {
					File::Delete(dest_file);
				}
				catch (...) {}
				File::Copy(newValue, dest_file);
			}
			material->props["arm_textname"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}

	[CategoryAttribute("Textures")]
	[EditorAttribute(CustomImageEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ Emission {
		String^ get()
		{
			return gcnew String(material->props["emission_textname"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			System::String^ file = Path::GetFileName(newValue);
			if (file->Length > 0) {
				System::String^ dest_file = root_folder + file;
				try {
					File::Delete(dest_file);
				}
				catch (...) {}
				File::Copy(newValue, dest_file);
			}
			material->props["emission_textname"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}

	MaterialProp(Material* material, System::String^ path) {
		this->material = material;
		this->root_folder = path + "\\";
	}
};
