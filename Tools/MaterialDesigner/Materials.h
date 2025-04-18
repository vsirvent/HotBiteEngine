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
using namespace System::Collections;

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

template<typename T>
T* GetMaterial(System::String^ name, Generic::List<IntPtr>^ list) {
	for each (IntPtr p in list) {
		T* m = (T*)p.ToPointer();
		if (String::Compare(gcnew System::String(m->props["name"]->GetValue<std::string>().c_str()), name) == 0) {
			return m;
		}
	}
	return nullptr;
}

template<typename T>
bool DeleteMaterial(System::String^ name, Generic::List<IntPtr>^ list) {
	for each (IntPtr p in list) {
		T* m = (T*)p.ToPointer();
		if (String::Compare(gcnew System::String(m->props["name"]->GetValue<std::string>().c_str()), name) == 0) {
			delete m;
			list->Remove(p);
			return true;
		}
	}
	return false;
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

class Element {
public:
	std::map<std::string, std::shared_ptr<AbstractProp>> props;

	virtual ~Element() {}

	virtual json ToJson() const {
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

	std::string ToString() const {
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

class Material: public Element {
public:
	gcroot<IMaterialListener^> listener = nullptr;
	Material(IMaterialListener^ l) : listener(l)
	{
		props["name"] = std::make_shared<Prop<std::string>>("material");
		props["diffuse_color"] = std::make_shared<Prop<std::string>>("#FFFFFFFF");
		props["ambient_color"] = std::make_shared<Prop<std::string>>("#FFFFFFFF");
		props["specular"] = std::make_shared<Prop<float>>(0.5f);
		props["rt_reflex"] = std::make_shared<Prop<float>>(0.2f);
		props["emission"] = std::make_shared<Prop<float>>(0.0f);
		props["emission_color"] = std::make_shared<Prop<std::string>>("#00000000");
		props["raytrace"] = std::make_shared<Prop<bool>>(true);
		props["opacity"] = std::make_shared<Prop<float>>(1.0f);
		props["density"] = std::make_shared<Prop<float>>(1.0f);
		props["parallax_scale"] = std::make_shared<Prop<float>>(0.0f);
		props["parallax_steps"] = std::make_shared<Prop<float>>(4.0f);
		props["parallax_angle_steps"] = std::make_shared<Prop<float>>(5.0f);
		props["parallax_shadows"] = std::make_shared<Prop<bool>>(false);
		props["parallax_shadow_scale"] = std::make_shared<Prop<int>>(false);
		props["tess_type"] = std::make_shared<Prop<int>>(0);
		props["tess_factor"] = std::make_shared<Prop<float>>(32.0f);
		props["displacement_scale"] = std::make_shared<Prop<float>>(0.0f);
		props["bloom_scale"] = std::make_shared<Prop<float>>(0.0f);
		props["normal_map_enabled"] = std::make_shared<Prop<bool>>(false);
		props["alpha_enabled"] = std::make_shared<Prop<bool>>(false);
		props["alpha_color"] = std::make_shared<Prop<std::string>>("#00000000");
		props["alpha_threshold"] = std::make_shared<Prop<float>>(0.4f);
		props["blend_enabled"] = std::make_shared<Prop<bool>>(false);
		props["diffuse_textname"] = std::make_shared<Prop<std::string>>("");
		props["normal_textname"] = std::make_shared<Prop<std::string>>("");
		props["high_textname"] = std::make_shared<Prop<std::string>>("");
		props["spec_textname"] = std::make_shared<Prop<std::string>>("");
		props["ao_textname"] = std::make_shared<Prop<std::string>>("");
		props["arm_textname"] = std::make_shared<Prop<std::string>>("");
		props["emission_textname"] = std::make_shared<Prop<std::string>>("");
		props["opacity_textname"] = std::make_shared<Prop<std::string>>("");
		props["vs"] = std::make_shared<Prop<std::string>>("");
		props["hs"] = std::make_shared<Prop<std::string>>("");
		props["ds"] = std::make_shared<Prop<std::string>>("");
		props["gs"] = std::make_shared<Prop<std::string>>("");
		props["ps"] = std::make_shared<Prop<std::string>>("");
		props["draw_vs"] = std::make_shared < Prop<std::string>>("MainRenderVS.cso");
		props["draw_hs"] = std::make_shared < Prop<std::string>>("MainRenderHS.cso");
		props["draw_ds"] = std::make_shared < Prop<std::string>>("MainRenderDS.cso");
		props["draw_gs"] = std::make_shared < Prop<std::string>>("MainRenderGS.cso");
		props["draw_ps"] = std::make_shared < Prop<std::string>>("MainRenderPS.cso");
		props["shadow_vs"] = std::make_shared < Prop<std::string>>("ShadowVS.cso");
		props["shadow_gs"] = std::make_shared < Prop<std::string>>("ShadowMapCubeGS.cso");
		props["depth_vs"] = std::make_shared < Prop<std::string>>("DepthVS.cso");
		props["depth_ps"] = std::make_shared < Prop<std::string>>("DepthPS.cso");
	};

	virtual bool FromJson(const json& js) {
		try {
			props["name"]->SetValue<std::string>(js.value("name", ""));
			props["diffuse_color"]->SetValue<std::string>(js.value("diffuse_color", "#FFFFFFFF"));
			props["ambient_color"]->SetValue<std::string>(js.value("ambient_color", "#FFFFFFFF"));
			props["parallax_scale"]->SetValue<float>(js.value("parallax_scale", 0.0f));
			props["parallax_steps"]->SetValue<float>(js.value("parallax_steps", 0.0f));
			props["parallax_angle_steps"]->SetValue<float>(js.value("parallax_angle_steps", 0.0f));
			props["parallax_shadows"]->SetValue<bool>(js.value("parallax_shadows", false));
			props["parallax_shadow_scale"]->SetValue<int>(js.value("parallax_shadow_scale", 0));
			
			props["specular"]->SetValue<float>(js.value("specular", 1.0f));
			props["emission"]->SetValue<float>(js.value("emission", 0.0f));
			props["rt_reflex"]->SetValue<float>(js.value("rt_reflex", 0.2f));
			props["emission_color"]->SetValue<std::string>(js.value("emission_color", "#00000000"));
			props["raytrace"]->SetValue<bool>(js.value("raytrace", true));
			props["opacity"]->SetValue<float>(js.value("opacity", 1.0f)); 
			props["density"]->SetValue<float>(js.value("density", 1.0f));
			props["tess_type"]->SetValue<int>(js.value("tess_type", 0));
			props["tess_factor"]->SetValue<float>(js.value("tess_factor", 0.0f));
			props["displacement_scale"]->SetValue<float>(js.value("displacement_scale", 0.0f));
			props["bloom_scale"]->SetValue<float>(js.value("bloom_scale", 0.0f));
			props["normal_map_enabled"]->SetValue<bool>(js.value("normal_map_enabled", true));
			props["alpha_enabled"]->SetValue<bool>(js.value("alpha_enabled", false));
			props["alpha_color"]->SetValue<std::string>(js.value("alpha_color", "#00000000"));
			props["alpha_threshold"]->SetValue<float>(js.value("alpha_threshold", 0.4f));
			props["blend_enabled"]->SetValue<bool>(js.value("blend_enabled", false));
			props["diffuse_textname"]->SetValue<std::string>(js.value("diffuse_textname", ""));
			props["normal_textname"]->SetValue<std::string>(js.value("normal_textname", ""));
			props["high_textname"]->SetValue<std::string>(js.value("high_textname", ""));
			props["spec_textname"]->SetValue<std::string>(js.value("spec_textname", ""));
			props["ao_textname"]->SetValue<std::string>(js.value("ao_textname", ""));
			props["arm_textname"]->SetValue<std::string>(js.value("arm_textname", ""));
			props["emission_textname"]->SetValue<std::string>(js.value("emission_textname", ""));
			props["opacity_textname"]->SetValue<std::string>(js.value("opacity_textname", ""));

			props["draw_vs"]->SetValue<std::string>(js.value("draw_vs", "MainRenderVS.cso"));
			props["draw_hs"]->SetValue<std::string>(js.value("draw_hs", "MainRenderHS.cso"));
			props["draw_ds"]->SetValue<std::string>(js.value("draw_ds", "MainRenderDS.cso"));
			props["draw_gs"]->SetValue<std::string>(js.value("draw_gs", "MainRenderGS.cso"));
			props["draw_ps"]->SetValue<std::string>(js.value("draw_ps", "MainRenderPS.cso"));

			props["shadow_vs"]->SetValue<std::string>(js.value("shadow_vs", "ShadowVS.cso"));
			props["shadow_gs"]->SetValue<std::string>(js.value("shadow_gs", "ShadowMapCubeGS.cso"));
			props["depth_vs"]->SetValue<std::string>(js.value("depth_vs", "DepthVS.cso"));
			props["depth_ps"]->SetValue<std::string>(js.value("depth_ps", "DepthPS.cso"));
		}
		catch (...) {
			return false;
		}
		return true;
	}
};

class MultiMaterialLayer : public Element {
public:
	MultiMaterialLayer()
	{
		props["mask"] = std::make_shared<Prop<std::string>>("");
		props["mask_noise"] = std::make_shared<Prop<int>>(0);
		props["uv_noise"] = std::make_shared<Prop<int>>(0);
		props["texture"] = std::make_shared<Prop<std::string>>("");
		props["op"] = std::make_shared<Prop<int>>(1);
		props["value"] = std::make_shared<Prop<float>>(1.0f);
		props["uv_scale"] = std::make_shared<Prop<float>>(1.0f);
	};

	MultiMaterialLayer(const json& js):MultiMaterialLayer() {
		FromJson(js);
	}

	virtual bool FromJson(const json& js) {
		try {
			props["mask"]->SetValue<std::string>(js.value("mask", ""));
			props["mask_noise"]->SetValue<int>(js.value("mask_noise", 0));
			props["uv_noise"]->SetValue<int>(js.value("uv_noise", 0));
			props["texture"]->SetValue<std::string>(js.value("texture", ""));
			props["op"]->SetValue<int>(js.value("op", 0));
			props["value"]->SetValue<float>(js.value("value", 0.0f));
			props["uv_scale"]->SetValue<float>(js.value("uv_scale", 0.0f));
		}
		catch (...) {
			return false;
		}
		return true;
	}
};

class MultiMaterial : public Element {
public:
	std::vector<MultiMaterialLayer> layers;
	gcroot<IMaterialListener^> listener = nullptr;
	MultiMaterial(IMaterialListener^ l) : listener(l)
	{
		props["name"] = std::make_shared<Prop<std::string>>("material");
		props["parallax_scale"] = std::make_shared<Prop<float>>(0.0f);
		props["parallax_steps"] = std::make_shared<Prop<float>>(4.0f);
		props["parallax_angle_steps"] = std::make_shared<Prop<float>>(5.0f);
		props["tess_type"] = std::make_shared<Prop<int>>(0);
		props["tess_factor"] = std::make_shared<Prop<float>>(32.0f);
		props["displacement_scale"] = std::make_shared<Prop<float>>(0.0f);
		
	};

	virtual bool FromJson(const json& js) {
		try {
			props["name"]->SetValue<std::string>(js.value("name", ""));
			props["parallax_scale"]->SetValue<float>(js.value("parallax_scale", 0.0f));
			props["parallax_steps"]->SetValue<float>(js.value("parallax_steps", 0.0f));
			props["parallax_angle_steps"]->SetValue<float>(js.value("parallax_angle_steps", 0.0f));
			props["tess_type"]->SetValue<int>(js.value("tess_type", 0));
			props["tess_factor"]->SetValue<float>(js.value("tess_factor", 0.0f));
			props["displacement_scale"]->SetValue<float>(js.value("displacement_scale", 0.0f));

			for (const auto& t : js["textures"]) {
				layers.push_back(MultiMaterialLayer(t));
			}
		}
		catch (...) {
			return false;
		}
		return true;
	}

	virtual json ToJson() const override {
		json js = Element::ToJson();
		js["count"] = layers.size();
		json layers_js;
		int i = 0;
		for (const auto& layer : layers) {
			auto layer_js = layer.ToJson();
			layer_js["layer"] = i++;
			layers_js.push_back(layer_js);
		}
		js["textures"] = layers_js;
		return js;
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

public ref class MultiLayerOpTypeConverter : public TypeConverter
{
public:
	static String^ TEXT_OP_MIX = gcnew String("MIX");
	static String^ TEXT_OP_ADD = gcnew String("ADD");
	static String^ TEXT_OP_MULT = gcnew String("MULT");

	static String^ GetLayerOpName(int32_t type) {
		switch (type) {
		case 0: return TEXT_OP_MIX;
		case 1: return TEXT_OP_ADD;
		case 2: return TEXT_OP_MULT;
		default: return TEXT_OP_ADD;
		}
	}

	static int GetLayerOpByName(String^ name) {
		if (String::Compare(name, TEXT_OP_MIX) == 0) {
			return 0;
		}
		if (String::Compare(name, TEXT_OP_ADD) == 0) {
			return 1;
		}
		if (String::Compare(name, TEXT_OP_MULT) == 0) {
			return 2;
		}
		return 0;
	}

	virtual StandardValuesCollection^ GetStandardValues(ITypeDescriptorContext^ context) override
	{
		array<System::String^>^ values = { TEXT_OP_MIX, TEXT_OP_ADD, TEXT_OP_MULT };
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

public ref class TextureTypeConverter : public TypeConverter
{
public:
	Generic::List<IntPtr>^ materials;

	void Setup(Generic::List<IntPtr>^ materials) {
		this->materials = materials;
	}

	virtual StandardValuesCollection^ GetStandardValues(ITypeDescriptorContext^ context) override
	{
		System::Collections::ArrayList^ values = gcnew System::Collections::ArrayList();
		for each (IntPtr p in materials) {
			Material* m = (Material*)p.ToPointer();
			values->Add(gcnew String(m->GetString("name").value().c_str()));
		}
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

ref class ShaderFileEditor : ImageEditor
{
public:
	virtual Object^ EditValue(ITypeDescriptorContext^ context, System::IServiceProvider^ provider, Object^ value) override
	{
		OpenFileDialog^ openFileDialog = gcnew OpenFileDialog();

		openFileDialog->Filter = "Shader Files|*.cso";
		openFileDialog->Title = "Select a shader";

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

	void CopyFile(System::String^ orig, System::String^ dest) {
		if (dest->Equals(orig) == false) {
			try {
				File::Delete(dest);
			}
			catch (...) {}
			File::Copy(orig, dest);
		}
	}

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
	property float EmissionIntensity {
		float get()
		{
			return material->props["emission"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return material->props["emission"]->SetValue<float>((float)newValue);
		}
	}

	[CategoryAttribute("Material")]
	property Drawing::Color EmissionColor {
		Drawing::Color get()
		{
			auto color = HotBite::Engine::Core::parseColorStringF4(material->props["emission_color"]->GetValue<std::string>());
			return Drawing::Color::FromArgb((int)(color.w * 255.0f), (int)(color.x * 255.0f), (int)(color.y * 255.0f), (int)(color.z * 255.0f));
		}

		void set(Drawing::Color newValue)
		{
			char color[64];
			snprintf(color, 64, "#%02X%02X%02X%02X", newValue.R, newValue.G, newValue.B, newValue.A);
			return material->props["emission_color"]->SetValue<std::string>(color);
		}
	}

	[CategoryAttribute("Material")]
	property bool RayTracing {
		bool get()
		{
			return material->props["raytrace"]->GetValue<bool>();
		}

		void set(bool newValue)
		{
			return material->props["raytrace"]->SetValue<bool>(newValue);
		}
	}

	[CategoryAttribute("Material")]
	property float RayTraceReflex {
		float get()
		{
			return material->props["rt_reflex"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return material->props["rt_reflex"]->SetValue<float>(newValue);
		}
	}

	[CategoryAttribute("Material")]
	property float OpacityValue {
		float get()
		{
			return material->props["opacity"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return material->props["opacity"]->SetValue<float>((float)newValue);
		}
	}

	[CategoryAttribute("Material")]
	property float Density {
		float get()
		{
			return material->props["density"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return material->props["density"]->SetValue<float>((float)newValue);
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
	property float ParallaxSteps {
		float get()
		{
			return material->props["parallax_steps"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return material->props["parallax_steps"]->SetValue<float>((float)newValue);
		}
	}

	[CategoryAttribute("Material")]
	property float ParallaxAngleSteps {
		float get()
		{
			return material->props["parallax_angle_steps"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return material->props["parallax_angle_steps"]->SetValue<float>((float)newValue);
		}
	}

	[CategoryAttribute("Material")]
	property bool ParallaxShadows {
		bool get()
		{
			return material->props["parallax_shadows"]->GetValue<bool>();
		}

		void set(bool newValue)
		{
			return material->props["parallax_shadows"]->SetValue<bool>(newValue);
		}
	}

	[CategoryAttribute("Material")]
	property int ParallaxShadowScale {
		int get()
		{
			return material->props["parallax_shadow_scale"]->GetValue<int>();
		}

		void set(int newValue)
		{
			return material->props["parallax_shadow_scale"]->SetValue<int>(newValue);
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
	property Drawing::Color AlphaColor {
		Drawing::Color get()
		{
			auto color = HotBite::Engine::Core::parseColorStringF4(material->props["alpha_color"]->GetValue<std::string>());
			return Drawing::Color::FromArgb((int)(color.w * 255.0f), (int)(color.x * 255.0f), (int)(color.y * 255.0f), (int)(color.z * 255.0f));
		}

		void set(Drawing::Color newValue)
		{
			char color[64];
			snprintf(color, 64, "#%02X%02X%02X%02X", newValue.R, newValue.G, newValue.B, newValue.A);
			return material->props["alpha_color"]->SetValue<std::string>(color);
		}
	}

	[CategoryAttribute("Material")]
	property float AlphaThreshold {
		float get()
		{
			return material->props["alpha_threshold"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return material->props["alpha_threshold"]->SetValue<float>((float)newValue);
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
				CopyFile(newValue, root_folder + file);
			}
			material->props["diffuse_textname"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}


	[CategoryAttribute("Textures")]
	[EditorAttribute(CustomImageEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ Opacity {
		String^ get()
		{
			return gcnew String(material->props["opacity_textname"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			System::String^ file = Path::GetFileName(newValue);
			if (file->Length > 0) {
				CopyFile(newValue, root_folder + file);
			}
			material->props["opacity_textname"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
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
				CopyFile(newValue, root_folder + file);
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
				CopyFile(newValue, root_folder + file);
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
				CopyFile(newValue, root_folder + file);
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
				CopyFile(newValue, root_folder + file);
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
				CopyFile(newValue, root_folder + file);
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
				CopyFile(newValue, root_folder + file);
			}
			material->props["emission_textname"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}

#if 0
	props["draw_vs"] = std::make_shared < Prop<std::string>>("MainRenderVS.cso");
	props["draw_hs"] = std::make_shared < Prop<std::string>>("MainRenderHS.cso");
	props["draw_ds"] = std::make_shared < Prop<std::string>>("MainRenderDS.cso");
	props["draw_gs"] = std::make_shared < Prop<std::string>>("MainRenderGS.cso");
	props["draw_ps"] = std::make_shared < Prop<std::string>>("MainRenderPS.cso");
	props["shadow_vs"] = std::make_shared < Prop<std::string>>("ShadowVS.cso");
	props["shadow_gs"] = std::make_shared < Prop<std::string>>("ShadowMapCubeGS.cso");
	props["depth_vs"] = std::make_shared < Prop<std::string>>("DepthVS.cso");
	props["depth_vs"] = std::make_shared < Prop<std::string>>("DepthPS.cso");
#endif
	[CategoryAttribute("Shaders")]
	[EditorAttribute(ShaderFileEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ DrawVS {
		String^ get()
		{
			return gcnew String(material->props["draw_vs"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			System::String^ file = Path::GetFileName(newValue);
			material->props["draw_vs"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}

	[CategoryAttribute("Shaders")]
	[EditorAttribute(ShaderFileEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ DrawHS {
		String^ get()
		{
			return gcnew String(material->props["draw_hs"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			System::String^ file = Path::GetFileName(newValue);
			material->props["draw_hs"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}

	[CategoryAttribute("Shaders")]
	[EditorAttribute(ShaderFileEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ DrawDS {
		String^ get()
		{
			return gcnew String(material->props["draw_ds"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			System::String^ file = Path::GetFileName(newValue);
			material->props["draw_ds"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}

	[CategoryAttribute("Shaders")]
	[EditorAttribute(ShaderFileEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ DrawGS {
		String^ get()
		{
			return gcnew String(material->props["draw_gs"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			System::String^ file = Path::GetFileName(newValue);
			material->props["draw_gs"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}

	[CategoryAttribute("Shaders")]
	[EditorAttribute(ShaderFileEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ DrawPS {
		String^ get()
		{
			return gcnew String(material->props["draw_ps"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			System::String^ file = Path::GetFileName(newValue);
			material->props["draw_ps"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}



	MaterialProp(Material* material, System::String^ path) {
		this->material = material;
		this->root_folder = path + "\\";
	}
};

public ref class MultiMaterialLayerProp
{
public:
	MultiMaterialLayer* layer;
	Generic::List<IntPtr>^ materials;
	System::String^ root_folder;

	[CategoryAttribute("MultiMaterialLayer")]
	[TypeConverterAttribute(MultiLayerOpTypeConverter::typeid)]
	property System::String^ Operation {
		System::String^ get()
		{
			return MultiLayerOpTypeConverter::GetLayerOpName((layer->props["op"]->GetValue<int32_t>()) & 0x03);
		}

		void set(System::String^ newValue)
		{
			int32_t val = layer->props["op"]->GetValue<int32_t>() & ~0x03 | MultiLayerOpTypeConverter::GetLayerOpByName(newValue);
			return layer->props["op"]->SetValue<int32_t>(val);
		}
	}

	[CategoryAttribute("MultiMaterialLayer")]
	[EditorAttribute(CustomImageEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ Mask {
		String^ get()
		{
			return gcnew String(layer->props["mask"]->GetValue<std::string>().c_str());
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
			layer->props["mask"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}
	
	[CategoryAttribute("MultiMaterialLayer")]
	[TypeConverterAttribute(TextureTypeConverter::typeid)]
	property String^ Texture {
		String^ get()
		{
			return gcnew String(layer->props["texture"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			layer->props["texture"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(newValue));
		}
	}

	[CategoryAttribute("MultiMaterialLayer")]
	property bool MaskNoise {
		bool get()
		{
			return (bool)layer->props["mask_noise"]->GetValue<int>();
		}

		void set(bool newValue)
		{
			return layer->props["mask_noise"]->SetValue<int>((int)newValue);
		}
	}

	[CategoryAttribute("MultiMaterialLayer")]
	property bool UVNoise {
		bool get()
		{
			return (bool)layer->props["uv_noise"]->GetValue<int>();
		}

		void set(bool newValue)
		{
			return layer->props["uv_noise"]->SetValue<int>((int)newValue);
		}
	}

	[CategoryAttribute("MultiMaterialLayer")]
	property float Value {
		float get()
		{
			return layer->props["value"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return layer->props["value"]->SetValue<float>(newValue);
		}
	}

	MultiMaterialLayerProp(MultiMaterialLayer* layer, System::String^ path, Generic::List<IntPtr>^ materials) {
		this->layer = layer;
		this->materials = materials;
		this->root_folder = path + "\\";

		PropertyDescriptor^ propertyDescriptor = TypeDescriptor::GetProperties(this)["Texture"];
		if (propertyDescriptor != nullptr)
		{
			TextureTypeConverter^ textureTypeConverter = dynamic_cast<TextureTypeConverter^>(propertyDescriptor->Converter);
			textureTypeConverter->Setup(materials);
		}
	}
};

public ref class MultiMaterialProp
{
public:
	MultiMaterial* material;
	System::String^ root_folder;

	[CategoryAttribute("MultiMaterial")]
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

	[CategoryAttribute("MultiMaterial")]
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

	[CategoryAttribute("MultiMaterial")]
	property float ParallaxSteps {
		float get()
		{
			return material->props["parallax_steps"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return material->props["parallax_steps"]->SetValue<float>((float)newValue);
		}
	}

	[CategoryAttribute("MultiMaterial")]
	property float ParallaxAngleSteps {
		float get()
		{
			return material->props["parallax_angle_steps"]->GetValue<float>();
		}

		void set(float newValue)
		{
			return material->props["parallax_angle_steps"]->SetValue<float>((float)newValue);
		}
	}

	[CategoryAttribute("MultiMaterial")]
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

	[CategoryAttribute("MultiMaterial")]
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

	[CategoryAttribute("MultiMaterial")]
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
	
	MultiMaterialProp(MultiMaterial* material, System::String^ path) {
		this->material = material;
		this->root_folder = path + "\\";
	}
};
