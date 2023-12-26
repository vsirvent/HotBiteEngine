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
		props["alpha_enabled"] = std::make_shared < Prop<bool>>("Alpha color enabled", false);
		props["x"] = std::make_shared < Prop<float>>("Position X", 0.45f);
		props["y"] = std::make_shared < Prop<float>>("Position Y", 0.45f);
		props["width"] = std::make_shared < Prop<float>>("Position Width", 0.1f);
		props["height"] = std::make_shared < Prop<float>>("Position Height", 0.1f);
	};

	virtual bool FromJson(const json& js) {
		try {
			props["name"]->SetValue<std::string>(js["name"]);
			props["layer"]->SetValue<int32_t>(js["layer"]);
			props["visible"]->SetValue<bool>(js["visible"]);
			props["background_image"]->SetValue<std::string>(js["background_image"]);
			props["background_color"]->SetValue<std::string>(js["background_color"]);
			props["background_alpha_color"]->SetValue<std::string>(js["background_alpha_color"]);
			props["background_alpha"]->SetValue<float>(js["background_alpha"]);
			props["alpha_enabled"]->SetValue<bool>(js["alpha_enabled"]);
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

enum EFontStyle
{
	FONT_STYLE_NORMAL,
	FONT_STYLE_OBLIQUE,
	FONT_STYLE_ITALIC
};

enum ETextAligment
{
	TEXT_ALIGNMENT_LEADING,
	TEXT_ALIGNMENT_TRAILING,
	TEXT_ALIGNMENT_CENTER,
	TEXT_ALIGNMENT_JUSTIFIED
};

enum EVerticalAligment
{
	VERTICAL_ALIGNMENT_NEAR,
	VERTICAL_ALIGNMENT_FAR,
	VERTICAL_ALIGNMENT_CENTER
};

enum EFontWeight
{
	FONT_WEIGHT_THIN = 100,
	FONT_WEIGHT_EXTRA_LIGHT = 200,
	FONT_WEIGHT_ULTRA_LIGHT = 200,
	FONT_WEIGHT_LIGHT = 300,
	FONT_WEIGHT_SEMI_LIGHT = 350,
	FONT_WEIGHT_NORMAL = 400,
	FONT_WEIGHT_REGULAR = 400,
	FONT_WEIGHT_MEDIUM = 500,
	FONT_WEIGHT_DEMI_BOLD = 600,
	FONT_WEIGHT_SEMI_BOLD = 600,
	FONT_WEIGHT_BOLD = 700,
	FONT_WEIGHT_EXTRA_BOLD = 800,
	FONT_WEIGHT_ULTRA_BOLD = 800,
	FONT_WEIGHT_BLACK = 900,
	FONT_WEIGHT_HEAVY = 900,
	FONT_WEIGHT_EXTRA_BLACK = 950,
	FONT_WEIGHT_ULTRA_BLACK = 950
};


class Label : public Widget {
public:
	Label(IWidgetListener^ l) :Widget(l) {
		props["type"] = std::make_shared<Prop<std::string>>("Widget type", "label");
		props["weight"] = std::make_shared<Prop<int32_t>>("Font weight", FONT_WEIGHT_NORMAL);
		props["font_size"] = std::make_shared<Prop<int32_t>>("Font size", 16);
		props["font_name"] = std::make_shared<Prop<std::string>>("Font name", "Arial");
		props["text"] = std::make_shared<Prop<std::string>>("Text", "text");
		props["style"] = std::make_shared<Prop<int32_t>>("Font style", EFontStyle::FONT_STYLE_NORMAL);
		props["halign"] = std::make_shared<Prop<int32_t>>("Horizontal alignment", ETextAligment::TEXT_ALIGNMENT_CENTER);
		props["valign"] = std::make_shared<Prop<int32_t>>("Vertical alignment", EVerticalAligment::VERTICAL_ALIGNMENT_CENTER);
		props["margin"] = std::make_shared<Prop<int32_t>>("Text margin", 0);
		props["text_color"] = std::make_shared<Prop<std::string>>("Text color", "#110000FF");
		props["show_text"] = std::make_shared<Prop<bool>>("Show text", true);
	}

	virtual bool FromJson(const json& js) {
		try {
			Widget::FromJson(js);
			props["weight"]->SetValue<int32_t>(js["weight"]);
			props["font_size"]->SetValue<int32_t>(js["font_size"]);
			props["font_name"]->SetValue<std::string>(js["font_name"]);
			props["text"]->SetValue<std::string>(js["text"]);
			props["style"]->SetValue<int32_t>(js["style"]);
			props["halign"]->SetValue<int32_t>(js["halign"]);
			props["valign"]->SetValue<int32_t>(js["valign"]);
			props["margin"]->SetValue<int32_t>(js["margin"]);
			props["text_color"]->SetValue<std::string>(js["text_color"]);
			props["show_text"]->SetValue<bool>(js["show_text"]);
		}
		catch (...) {
			return false;
		}
		return true;
	}

};

class Button : public ::Label {
public:
	Button(IWidgetListener^ l) :Label(l) {
		props["type"] = std::make_shared<Prop<std::string>>("Widget type", "button");
		props["click_image"] = std::make_shared<Prop<std::string>>("Button click image", "");
		props["idle_image"] = std::make_shared<Prop<std::string>>("Button idle image", "");
		props["hover_image"] = std::make_shared<Prop<std::string>>("Button hover image", "");
		props["click_sound"] = std::make_shared<Prop<std::string>>("Button click sound", "");
		props["hover_sound"] = std::make_shared<Prop<std::string>>("Button hover sound", "");
	}

	virtual bool FromJson(const json& js) {
		try {
			Label::FromJson(js);
			props["click_image"]->SetValue<std::string>(js["click_image"]);
			props["idle_image"]->SetValue<std::string>(js["idle_image"]);
			props["hover_image"]->SetValue<std::string>(js["hover_image"]);
			props["click_sound"]->SetValue<std::string>(js["click_sound"]);
			props["hover_sound"]->SetValue<std::string>(js["hover_sound"]);
		}
		catch (...) {
			return false;
		}
		return true;
	}
};

class ProgressBar : public ::Label {
public:
	ProgressBar(IWidgetListener^ l) :Label(l) {
		props["type"] = std::make_shared<Prop<std::string>>("Widget type", "progress");
		props["max_value"] = std::make_shared<Prop<float>>("Max value", 100.0f);
		props["min_value"] = std::make_shared<Prop<float>>("Min value", 0.0f);
		props["value"] = std::make_shared<Prop<float>>("Value", 50.0f);
		props["bar_color"] = std::make_shared<Prop<std::string>>("Bar color", "#005555ff");
		props["bar_image"] = std::make_shared<Prop<std::string>>("Bar image", "");
	}

	virtual bool FromJson(const json& js) {
		try {
			Label::FromJson(js);
			props["max_value"]->SetValue<float>(js["max_value"]);
			props["min_value"]->SetValue<float>(js["min_value"]);
			props["value"]->SetValue<float>(js["value"]);
			props["bar_color"]->SetValue<std::string>(js["bar_color"]);
			props["bar_image"]->SetValue<std::string>(js["bar_image"]);
		}
		catch (...) {
			return false;
		}
		return true;
	}

};

ref class CustomImageEditor : ImageEditor
{
public:
	virtual Object^ EditValue(ITypeDescriptorContext^ context, System::IServiceProvider^ provider, Object^ value) override
	{
		OpenFileDialog^ openFileDialog = gcnew OpenFileDialog();

		openFileDialog->Filter = "Image Files|*.bmp;*.jpg;*.jpeg;*.gif;*.png|All Files|*.*";
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

ref class CustomAudioEditor : UITypeEditor
{
public:
	UITypeEditorEditStyle GetEditStyle(System::ComponentModel::ITypeDescriptorContext^ context) override
	{
		return UITypeEditorEditStyle::Modal;
	}

	Object^ EditValue(ITypeDescriptorContext^ context, System::IServiceProvider^ provider, Object^ value) override
	{
		OpenFileDialog^ openFileDialog = gcnew OpenFileDialog();

		openFileDialog->Filter = "Raw Audio Files|*.raw|All Files|*.*";
		openFileDialog->Title = "Select an RAW audio file (PCM 16 bits LE 44100 2-Channel)";

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
	[EditorAttribute(CustomImageEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
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
	property Boolean AlphaEnabled {
		Boolean get()
		{
			return ((Widget*)widget.ToPointer())->props["alpha_enabled"]->GetValue<bool>();
		}
		void set(Boolean newValue)
		{
			return ((Widget*)widget.ToPointer())->props["alpha_enabled"]->SetValue<bool>((bool)newValue);
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

public ref class LabelProp : public WidgetProp
{
public:
	LabelProp(IntPtr widget, System::String^ path) : WidgetProp(widget, path) {}

	[CategoryAttribute("Label")]
	property System::Drawing::Font^ Font
	{
		System::Drawing::Font^ get()
		{
			System::Drawing::Font^ font = gcnew System::Drawing::Font(
				gcnew String(((Widget*)widget.ToPointer())->props["font_name"]->GetValue<std::string>().c_str()),
				(float)((Widget*)widget.ToPointer())->props["font_size"]->GetValue<int>(),
				System::Drawing::FontStyle(((Widget*)widget.ToPointer())->props["style"]->GetValue<int>())
			);
			return font;
		}
		void set(System::Drawing::Font^ newValue)
		{
			((Widget*)widget.ToPointer())->props["font_name"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(newValue->Name));
			((Widget*)widget.ToPointer())->props["font_size"]->SetValue<int>((int)newValue->Size);
			((Widget*)widget.ToPointer())->props["style"]->SetValue<int>((int)newValue->Style);
		}
	}

	[CategoryAttribute("Label")]
	property System::String^ Text
	{
		System::String^ get()
		{
			return gcnew String(((Widget*)widget.ToPointer())->props["text"]->GetValue<std::string>().c_str());
		}
		void set(System::String^ newValue)
		{
			((Widget*)widget.ToPointer())->props["text"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(newValue));
		}
	}

	[CategoryAttribute("Label")]
	property int HorizontalAlignment
	{
		int get()
		{
			return ((Widget*)widget.ToPointer())->props["halign"]->GetValue<int>();
		}
		void set(int newValue)
		{
			((Widget*)widget.ToPointer())->props["halign"]->SetValue<int>(newValue);
		}
	}

	[CategoryAttribute("Label")]
	property int VerticalAlignment
	{
		int get()
		{
			return ((Widget*)widget.ToPointer())->props["valign"]->GetValue<int>();
		}
		void set(int newValue)
		{
			((Widget*)widget.ToPointer())->props["valign"]->SetValue<int>(newValue);
		}
	}

	[CategoryAttribute("Label")]
	property int TextMargin
	{
		int get()
		{
			return ((Widget*)widget.ToPointer())->props["margin"]->GetValue<int>();
		}
		void set(int newValue)
		{
			((Widget*)widget.ToPointer())->props["margin"]->SetValue<int>(newValue);
		}
	}

	[CategoryAttribute("Label")]
	property Drawing::Color TextColor
	{
		Drawing::Color get()
		{
			auto color = HotBite::Engine::Core::parseColorStringF4(((Widget*)widget.ToPointer())->props["text_color"]->GetValue<std::string>());
			return Drawing::Color::FromArgb((int)(color.w * 255.0f), (int)(color.x * 255.0f), (int)(color.y * 255.0f), (int)(color.z * 255.0f));
		}
		void set(Drawing::Color newValue)
		{
			char color[64];
			snprintf(color, 64, "#%02X%02X%02X%02X", newValue.R, newValue.G, newValue.B, newValue.A);
			return ((Widget*)widget.ToPointer())->props["text_color"]->SetValue<std::string>(color);
		}
	}

	[CategoryAttribute("Label")]
	property bool ShowText
	{
		bool get()
		{
			return ((Widget*)widget.ToPointer())->props["show_text"]->GetValue<bool>();
		}
		void set(bool newValue)
		{
			((Widget*)widget.ToPointer())->props["show_text"]->SetValue<bool>(newValue);
		}
	}
};

public ref class ButtonProp : public LabelProp
{
public:
	ButtonProp(IntPtr widget, System::String^ path) : LabelProp(widget, path) {}

	[CategoryAttribute("Button")]
	[EditorAttribute(CustomImageEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ ClickImage {
		String^ get()
		{
			return gcnew String(((Widget*)widget.ToPointer())->props["click_image"]->GetValue<std::string>().c_str());
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
			((Widget*)widget.ToPointer())->props["click_image"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}

	[CategoryAttribute("Button")]
	[EditorAttribute(CustomImageEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ IdleImage {
		String^ get()
		{
			return gcnew String(((Widget*)widget.ToPointer())->props["idle_image"]->GetValue<std::string>().c_str());
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
			((Widget*)widget.ToPointer())->props["idle_image"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}
	[CategoryAttribute("Button")]
	[EditorAttribute(CustomImageEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ HoverImage {
		String^ get()
		{
			return gcnew String(((Widget*)widget.ToPointer())->props["hover_image"]->GetValue<std::string>().c_str());
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
			((Widget*)widget.ToPointer())->props["hover_image"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}
	[CategoryAttribute("Button")]
	[EditorAttribute(CustomAudioEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ ClickSound {
		String^ get()
		{
			return gcnew String(((Widget*)widget.ToPointer())->props["click_sound"]->GetValue<std::string>().c_str());
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
			((Widget*)widget.ToPointer())->props["click_sound"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}
	[CategoryAttribute("Button")]
	[EditorAttribute(CustomAudioEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ HoverSound {
		String^ get()
		{
			return gcnew String(((Widget*)widget.ToPointer())->props["hover_sound"]->GetValue<std::string>().c_str());
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
			((Widget*)widget.ToPointer())->props["hover_sound"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}
};


public ref class ProgressBarProp : public LabelProp
{
public:
	ProgressBarProp(IntPtr widget, System::String^ path) : LabelProp(widget, path) {}

	[CategoryAttribute("ProgressBar")]
	[EditorAttribute(CustomImageEditor::typeid, System::Drawing::Design::UITypeEditor::typeid)]
	property String^ BarImage {
		String^ get()
		{
			return gcnew String(((Widget*)widget.ToPointer())->props["bar_image"]->GetValue<std::string>().c_str());
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
			((Widget*)widget.ToPointer())->props["bar_image"]->SetValue<std::string>(msclr::interop::marshal_as<std::string>(file));
		}
	}
	[CategoryAttribute("ProgressBar")]
	property Drawing::Color BarColor
	{
		Drawing::Color get()
		{
			auto color = HotBite::Engine::Core::parseColorStringF4(((Widget*)widget.ToPointer())->props["bar_color"]->GetValue<std::string>());
			return Drawing::Color::FromArgb((int)(color.w * 255.0f), (int)(color.x * 255.0f), (int)(color.y * 255.0f), (int)(color.z * 255.0f));
		}
		void set(Drawing::Color newValue)
		{
			char color[64];
			snprintf(color, 64, "#%02X%02X%02X%02X", newValue.R, newValue.G, newValue.B, newValue.A);
			return ((Widget*)widget.ToPointer())->props["bar_color"]->SetValue<std::string>(color);
		}
	}
	[CategoryAttribute("ProgressBar")]
	property Double MaxValue
	{
		Double get()
		{
			return (Double)((Widget*)widget.ToPointer())->props["max_value"]->GetValue<float>();
		}
		void set(Double newValue)
		{
			((Widget*)widget.ToPointer())->props["max_value"]->SetValue<float>((float)newValue);
		}
	}
	[CategoryAttribute("ProgressBar")]
	property Double MinValue
	{
		Double get()
		{
			return (Double)((Widget*)widget.ToPointer())->props["min_value"]->GetValue<float>();
		}
		void set(Double newValue)
		{
			((Widget*)widget.ToPointer())->props["min_value"]->SetValue<float>((float)newValue);
		}
	}
	[CategoryAttribute("ProgressBar")]
	property Double Value
	{
		Double get()
		{
			return (Double)((Widget*)widget.ToPointer())->props["value"]->GetValue<float>();
		}
		void set(Double newValue)
		{
			((Widget*)widget.ToPointer())->props["value"]->SetValue<float>((float)newValue);
		}
	}
};