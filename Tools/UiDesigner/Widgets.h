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
class Prop: public AbstractProp {
public:
	Prop();
	Prop(const std::string& _desc, T _val) : description(_desc), value(_val) {}
	~Prop() {};
	std::string description;
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

public interface class IWidgetListener {
public:
	virtual void OnNameChanged(String^ old_name, String^ new_name) abstract;
};

class Widget {
public:
	std::map<std::string, std::shared_ptr<AbstractProp>> props;
	std::list<std::string> widgets;
	gcroot<IWidgetListener^> listener = nullptr;
	Widget(IWidgetListener^ l): listener(l)
	{ 
		props["type"] = std::make_shared<Prop<std::string>>("Widget type", "widget");
		props["parent"] = std::make_shared<Prop<std::string>>("Widget parent", "");
		props["name"] = std::make_shared<Prop<std::string>>("Widget name", "name");
		props["layer"] = std::make_shared < Prop<int32_t>>("Draw layer number", 0);
		props["visible"] = std::make_shared < Prop<bool>>("Visibility of the widget", true);
		props["background_image"] = std::make_shared < Prop<std::string>>("Widget background image", "");
		props["background_color"] = std::make_shared < Prop<std::string>>("Widget background color", "#FFFFFFFF");
		props["background_alpha_color"] = std::make_shared < Prop<std::string>>("Alpha color of the background image", "#FFFFFFFF");
		props["background_alpha"] = std::make_shared < Prop<float>>("Alpha value of the background color", 1.0f);
		props["x"] = std::make_shared < Prop<float>>("Position X", 0.45f);
		props["y"] = std::make_shared < Prop<float>>("Position Y", 0.45f);
		props["width"] = std::make_shared < Prop<float>>("Position Width", 0.1f);
		props["height"] = std::make_shared < Prop<float>>("Position Height", 0.1f);
	};

	bool FromJson(const json& js) {
		try {
			props["name"]->SetValue<std::string>(js["name"]);
			props["layer"]->SetValue<int32_t>(js["layer"]);
			props["visible"]->SetValue<bool>(js["visible"]);
			props["background_image"]->SetValue<std::string>(js["background_image"]);
			props["background_color"]->SetValue<std::string>(js["background_color"]);
			props["background_alpha_color"]->SetValue<std::string>(js["background_alpha_color"]);
			props["background_alpha"]->SetValue<float>(js["background_alpha"]);
			props["x"]->SetValue<float>(js["x"]);
			props["y"]->SetValue<float>(js["y"]);
			props["width"]->SetValue<float>(js["width"]);
			props["height"]->SetValue<float>(js["height"]);
			props["parent"]->SetValue<std::string>(js["parent"]);
			if (js.contains("widgets")) {
				for (const std::string& w : js["widgets"]) {
					widgets.push_back(w);
				}
			}
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
		js["widgets"] = widgets;
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

public ref class WidgetProp
{
public:
	IntPtr widget;
	System::String^ root_folder;

	[CategoryAttribute("Widget")]
	property String^ Name {
		String^ get()
		{
			return gcnew String(((Widget*)widget.ToPointer())->props["name"]->GetValue<std::string>().c_str());
		}
		void set(String^ newValue)
		{
			Widget* w = (Widget*)widget.ToPointer();
			w->listener->OnNameChanged(Name, newValue);
			std::string str = msclr::interop::marshal_as<std::string>(newValue);
			return ((Widget*)widget.ToPointer())->props["name"]->SetValue<std::string>(str);
		}
	}
	[CategoryAttribute("Widget")]
	property bool Visible {
		bool get()
		{
			return ((Widget*)widget.ToPointer())->props["visible"]->GetValue<bool>();
		}
		void set(bool newValue)
		{
			return ((Widget*)widget.ToPointer())->props["visible"]->SetValue<bool>(newValue);
		}
	}
	[CategoryAttribute("Widget")]
	property Int32 Layer {
		int get()
		{
			return ((Widget*)widget.ToPointer())->props["layer"]->GetValue<int>();
		}
		void set(int newValue)
		{
			return ((Widget*)widget.ToPointer())->props["layer"]->SetValue<int>(newValue);
		}
	}
	[CategoryAttribute("Widget")]
	property Drawing::Color BackgroundColor {
		Drawing::Color get()
		{
			auto color = HotBite::Engine::Core::parseColorStringF4(((Widget*)widget.ToPointer())->props["background_color"]->GetValue<std::string>());
			return Drawing::Color::FromArgb((int)(color.w * 255.0f), (int)(color.x * 255.0f), (int)(color.y * 255.0f), (int)(color.z * 255.0f));
		}

		void set(Drawing::Color newValue)
		{
			char color[64];
			snprintf(color, 64, "#%02X%02X%02X%02X", newValue.R, newValue.G, newValue.B, newValue.A);
			return ((Widget*)widget.ToPointer())->props["background_color"]->SetValue<std::string>(color);
		}
	}
	[CategoryAttribute("Widget")]
	property Drawing::Color BackgroundAlphaColor {
		Drawing::Color get()
		{
			auto color = HotBite::Engine::Core::parseColorStringF4(((Widget*)widget.ToPointer())->props["background_alpha_color"]->GetValue<std::string>());
			return Drawing::Color::FromArgb((int)(color.w * 255.0f), (int)(color.x * 255.0f), (int)(color.y * 255.0f), (int)(color.z * 255.0f));
		}

		void set(Drawing::Color newValue)
		{
			char color[64];
			snprintf(color, 64, "#%02X%02X%02X%02X", newValue.R, newValue.G, newValue.B, newValue.A);
			return ((Widget*)widget.ToPointer())->props["background_alpha_color"]->SetValue<std::string>(color);
		}
	}
	[CategoryAttribute("Widget")]
	[EditorAttribute(System::Windows::Forms::Design::FileNameEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ BackgroundImage {
		String^ get()
		{
			return gcnew String(((Widget*)widget.ToPointer())->props["background_image"]->GetValue<std::string>().c_str());
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
			((Widget*)widget.ToPointer())->props["background_image"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}
	[CategoryAttribute("Widget")]
	property Double BackgroundAlpha {
		Double get()
		{
			return ((Widget*)widget.ToPointer())->props["background_alpha"]->GetValue<float>();
		}
		void set(Double newValue)
		{
			return ((Widget*)widget.ToPointer())->props["background_alpha"]->SetValue<float>((float)newValue);
		}
	}
	[CategoryAttribute("Widget")]
	property Double X {
		Double get()
		{
			return ((Widget*)widget.ToPointer())->props["x"]->GetValue<float>();
		}
		void set(Double newValue)
		{
			return ((Widget*)widget.ToPointer())->props["x"]->SetValue<float>((float)newValue);
		}
	}
	[CategoryAttribute("Widget")]
	property Double Y {
		Double get()
		{
			return ((Widget*)widget.ToPointer())->props["y"]->GetValue<float>();
		}
		void set(Double newValue)
		{
			return ((Widget*)widget.ToPointer())->props["y"]->SetValue<float>((float)newValue);
		}
	}
	[CategoryAttribute("Widget")]
	property Double Width {
		Double get()
		{
			return ((Widget*)widget.ToPointer())->props["width"]->GetValue<float>();
		}
		void set(Double newValue)
		{
			return ((Widget*)widget.ToPointer())->props["width"]->SetValue<float>((float)newValue);
		}
	}
	[CategoryAttribute("Widget")]
	property Double Height {
		Double get()
		{
			return ((Widget*)widget.ToPointer())->props["height"]->GetValue<float>();
		}
		void set(Double newValue)
		{
			return ((Widget*)widget.ToPointer())->props["height"]->SetValue<float>((float)newValue);
		}
	}
	WidgetProp(IntPtr widget, System::String^ path) {
		this->widget = widget;
		this->root_folder = path + "\\";
		Widget* w = (Widget*)widget.ToPointer();
	}
};