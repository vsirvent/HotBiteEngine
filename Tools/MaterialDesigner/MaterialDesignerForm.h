#pragma once

#include <HotBiteTool.h>
#include "Materials.h"

#include <map>
#include <msclr/marshal_cppstd.h>
#include <vcclr.h>
#include <string>
#include <memory>
#include <iostream>
#include <fstream>
#include <map>

using namespace msclr::interop;

namespace MaterialDesigner {

	using namespace System;
	using namespace System::ComponentModel;
	using namespace System::Collections;
	using namespace System::Windows::Forms;
	using namespace System::Data;
	using namespace System::Drawing;

	public ref class MaterialDesignerForm : public System::Windows::Forms::Form, public IMaterialListener
	{

	public:

		MaterialDesignerForm(void)
		{
			InitializeComponent();
			materials = gcnew Generic::List<IntPtr>();
			multiMaterials = gcnew Generic::List<IntPtr>();
			HotBiteTool::ToolUi::CreateToolUi(GetModuleHandle(NULL), (HWND)preview->Handle.ToPointer(), 1920, 1080, 60);
			std::string root = "..\\..\\..\\Tools\\MaterialDesigner\\";

			HotBiteTool::ToolUi::LoadWorld(root + "material_scene.json");
			HotBiteTool::ToolUi::RotateEntity("Cube");
			HotBiteTool::ToolUi::RotateEntity("Plane");
			HotBiteTool::ToolUi::RotateEntity("Sphere");
			HotBiteTool::ToolUi::RotateEntity("Monkey");
			rootFolder->Text = Environment::GetFolderPath(Environment::SpecialFolder::MyDocuments);
			Material* m = new ::Material(this);
			char name[32];
			snprintf(name, 32, "defaultMaterial");
			m->props["name"]->SetValue<std::string>(name);
			m->props["diffuse_color"]->SetValue<std::string>("#7f7f7fff");
			defaultMaterial = gcnew IntPtr(m);
			buttonAddLayer->Enabled = false;
			layerList->Enabled = false;
			UpdateEditor();
		}

		virtual void OnNameChanged(String^ old_name, String^ new_name)
		{
			for (int i = 0; i < materialList->Items->Count; ++i) {
				String^ item = (String^)materialList->Items[i];
				if (String::Compare(item, old_name) == 0) {
					materialList->Items[i] = new_name;
				}
			}
		}
	protected: System::Windows::Forms::Label^ label1;
	protected: System::Windows::Forms::PropertyGrid^ propertyGrid;
	protected: System::Windows::Forms::Label^ label2;
	public:

	protected:
		enum class Model {
			CUBE,
			SPHERE,
			PLANE,
			CUSTOM,
			NONE
		};
		Model currentModel = Model::CUBE;
	private: System::Windows::Forms::ToolStripMenuItem^ sphereToolStripMenuItem;
	protected:
	private: System::Windows::Forms::ToolStripMenuItem^ panelToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ monkeyToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ multiMaterialToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ newToolStripMenuItem2;
	private: System::Windows::Forms::ToolStripMenuItem^ removeToolStripMenuItem1;
	private: System::Windows::Forms::ToolStripMenuItem^ resetToolStripMenuItem1;
	protected: System::Windows::Forms::Label^ label4;
	private:
	private: System::Windows::Forms::ListBox^ multiMaterialList;
	protected:

	protected:


		Generic::List<IntPtr>^ materials;
		Generic::List<IntPtr>^ multiMaterials;
	protected: System::Windows::Forms::Button^ buttonAddLayer;

	protected: System::Windows::Forms::Button^ button3;
	protected: System::Windows::Forms::Button^ button2;
	protected: System::Windows::Forms::Label^ label5;
	private: System::Windows::Forms::ListBox^ layerList;
	private: System::Windows::Forms::ToolStripMenuItem^ emptyToolStripMenuItem;
	protected:

	protected:
		IntPtr^ defaultMaterial = nullptr;
		/// <summary>
		/// Limpiar los recursos que se estén usando.
		/// </summary>
		~MaterialDesignerForm()
		{
			if (components)
			{
				delete components;
			}
		}
	protected: System::Windows::Forms::TabControl^ MainTab;
	protected: System::Windows::Forms::TabPage^ Design;
	protected: System::Windows::Forms::Panel^ preview;
	protected: System::Windows::Forms::TabPage^ Code;
	protected: System::Windows::Forms::TextBox^ textEditor;
	private: System::Windows::Forms::MenuStrip^ menuStrip1;
	private: System::Windows::Forms::ToolStripMenuItem^ fileToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ newToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ openToolStripMenuItem;
	private: System::Windows::Forms::SplitContainer^ splitContainer1;
	private: System::Windows::Forms::ToolStripMenuItem^ saveToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ saveAsToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ materialToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ newToolStripMenuItem1;
	private: System::Windows::Forms::ToolStripMenuItem^ removeToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ resetToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ editorToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ cubeToolStripMenuItem;
	private: System::Windows::Forms::ListBox^ materialList;

	protected: System::Windows::Forms::Button^ button1;
	protected: System::Windows::Forms::TextBox^ rootFolder;
	protected: System::Windows::Forms::Label^ label3;
			 System::String^ fileName;
	private:
		/// <summary>
		/// Variable del diseñador necesaria.
		/// </summary>
		System::ComponentModel::Container^ components;

#pragma region Windows Form Designer generated code
		/// <summary>
		/// Método necesario para admitir el Diseñador. No se puede modificar
		/// el contenido de este método con el editor de código.
		/// </summary>
		void InitializeComponent(void)
		{
			System::ComponentModel::ComponentResourceManager^ resources = (gcnew System::ComponentModel::ComponentResourceManager(MaterialDesignerForm::typeid));
			this->MainTab = (gcnew System::Windows::Forms::TabControl());
			this->Design = (gcnew System::Windows::Forms::TabPage());
			this->preview = (gcnew System::Windows::Forms::Panel());
			this->Code = (gcnew System::Windows::Forms::TabPage());
			this->textEditor = (gcnew System::Windows::Forms::TextBox());
			this->menuStrip1 = (gcnew System::Windows::Forms::MenuStrip());
			this->fileToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->newToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->openToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->saveToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->saveAsToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->materialToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->newToolStripMenuItem1 = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->removeToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->resetToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->multiMaterialToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->newToolStripMenuItem2 = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->removeToolStripMenuItem1 = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->resetToolStripMenuItem1 = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->editorToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->cubeToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->sphereToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->panelToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->monkeyToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->splitContainer1 = (gcnew System::Windows::Forms::SplitContainer());
			this->layerList = (gcnew System::Windows::Forms::ListBox());
			this->buttonAddLayer = (gcnew System::Windows::Forms::Button());
			this->button3 = (gcnew System::Windows::Forms::Button());
			this->button2 = (gcnew System::Windows::Forms::Button());
			this->label5 = (gcnew System::Windows::Forms::Label());
			this->label4 = (gcnew System::Windows::Forms::Label());
			this->multiMaterialList = (gcnew System::Windows::Forms::ListBox());
			this->propertyGrid = (gcnew System::Windows::Forms::PropertyGrid());
			this->label2 = (gcnew System::Windows::Forms::Label());
			this->label1 = (gcnew System::Windows::Forms::Label());
			this->materialList = (gcnew System::Windows::Forms::ListBox());
			this->button1 = (gcnew System::Windows::Forms::Button());
			this->rootFolder = (gcnew System::Windows::Forms::TextBox());
			this->label3 = (gcnew System::Windows::Forms::Label());
			this->emptyToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->MainTab->SuspendLayout();
			this->Design->SuspendLayout();
			this->Code->SuspendLayout();
			this->menuStrip1->SuspendLayout();
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^>(this->splitContainer1))->BeginInit();
			this->splitContainer1->Panel1->SuspendLayout();
			this->splitContainer1->Panel2->SuspendLayout();
			this->splitContainer1->SuspendLayout();
			this->SuspendLayout();
			// 
			// MainTab
			// 
			this->MainTab->Anchor = static_cast<System::Windows::Forms::AnchorStyles>((((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Bottom)
				| System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->MainTab->Controls->Add(this->Design);
			this->MainTab->Controls->Add(this->Code);
			this->MainTab->Location = System::Drawing::Point(3, 3);
			this->MainTab->Name = L"MainTab";
			this->MainTab->SelectedIndex = 0;
			this->MainTab->Size = System::Drawing::Size(917, 649);
			this->MainTab->TabIndex = 8;
			// 
			// Design
			// 
			this->Design->Controls->Add(this->preview);
			this->Design->Location = System::Drawing::Point(4, 22);
			this->Design->Name = L"Design";
			this->Design->Padding = System::Windows::Forms::Padding(3);
			this->Design->Size = System::Drawing::Size(909, 623);
			this->Design->TabIndex = 0;
			this->Design->Text = L"Design";
			this->Design->UseVisualStyleBackColor = true;
			// 
			// preview
			// 
			this->preview->Anchor = static_cast<System::Windows::Forms::AnchorStyles>((((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Bottom)
				| System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->preview->BackColor = System::Drawing::Color::Black;
			this->preview->Location = System::Drawing::Point(0, 0);
			this->preview->Name = L"preview";
			this->preview->Size = System::Drawing::Size(909, 627);
			this->preview->TabIndex = 0;
			// 
			// Code
			// 
			this->Code->Controls->Add(this->textEditor);
			this->Code->Location = System::Drawing::Point(4, 22);
			this->Code->Name = L"Code";
			this->Code->Padding = System::Windows::Forms::Padding(3);
			this->Code->Size = System::Drawing::Size(909, 623);
			this->Code->TabIndex = 1;
			this->Code->Text = L"Code";
			this->Code->UseVisualStyleBackColor = true;
			// 
			// textEditor
			// 
			this->textEditor->Anchor = static_cast<System::Windows::Forms::AnchorStyles>((((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Bottom)
				| System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->textEditor->Font = (gcnew System::Drawing::Font(L"Courier New", 8.25F, System::Drawing::FontStyle::Regular, System::Drawing::GraphicsUnit::Point,
				static_cast<System::Byte>(0)));
			this->textEditor->Location = System::Drawing::Point(0, 0);
			this->textEditor->Multiline = true;
			this->textEditor->Name = L"textEditor";
			this->textEditor->ReadOnly = true;
			this->textEditor->ScrollBars = System::Windows::Forms::ScrollBars::Both;
			this->textEditor->Size = System::Drawing::Size(909, 625);
			this->textEditor->TabIndex = 0;
			// 
			// menuStrip1
			// 
			this->menuStrip1->Items->AddRange(gcnew cli::array< System::Windows::Forms::ToolStripItem^  >(4) {
				this->fileToolStripMenuItem,
					this->materialToolStripMenuItem, this->multiMaterialToolStripMenuItem, this->editorToolStripMenuItem
			});
			this->menuStrip1->Location = System::Drawing::Point(0, 0);
			this->menuStrip1->Name = L"menuStrip1";
			this->menuStrip1->Size = System::Drawing::Size(1206, 24);
			this->menuStrip1->TabIndex = 9;
			this->menuStrip1->Text = L"menuStrip1";
			// 
			// fileToolStripMenuItem
			// 
			this->fileToolStripMenuItem->DropDownItems->AddRange(gcnew cli::array< System::Windows::Forms::ToolStripItem^  >(4) {
				this->newToolStripMenuItem,
					this->openToolStripMenuItem, this->saveToolStripMenuItem, this->saveAsToolStripMenuItem
			});
			this->fileToolStripMenuItem->Name = L"fileToolStripMenuItem";
			this->fileToolStripMenuItem->Size = System::Drawing::Size(37, 20);
			this->fileToolStripMenuItem->Text = L"File";
			// 
			// newToolStripMenuItem
			// 
			this->newToolStripMenuItem->Name = L"newToolStripMenuItem";
			this->newToolStripMenuItem->Size = System::Drawing::Size(123, 22);
			this->newToolStripMenuItem->Text = L"New";
			this->newToolStripMenuItem->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::newToolStripMenuItem_Click_1);
			// 
			// openToolStripMenuItem
			// 
			this->openToolStripMenuItem->Name = L"openToolStripMenuItem";
			this->openToolStripMenuItem->Size = System::Drawing::Size(123, 22);
			this->openToolStripMenuItem->Text = L"Open...";
			this->openToolStripMenuItem->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::openToolStripMenuItem_Click);
			// 
			// saveToolStripMenuItem
			// 
			this->saveToolStripMenuItem->Name = L"saveToolStripMenuItem";
			this->saveToolStripMenuItem->Size = System::Drawing::Size(123, 22);
			this->saveToolStripMenuItem->Text = L"Save";
			this->saveToolStripMenuItem->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::saveToolStripMenuItem_Click);
			// 
			// saveAsToolStripMenuItem
			// 
			this->saveAsToolStripMenuItem->Name = L"saveAsToolStripMenuItem";
			this->saveAsToolStripMenuItem->Size = System::Drawing::Size(123, 22);
			this->saveAsToolStripMenuItem->Text = L"Save As...";
			this->saveAsToolStripMenuItem->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::saveAsToolStripMenuItem_Click);
			// 
			// materialToolStripMenuItem
			// 
			this->materialToolStripMenuItem->DropDownItems->AddRange(gcnew cli::array< System::Windows::Forms::ToolStripItem^  >(3) {
				this->newToolStripMenuItem1,
					this->removeToolStripMenuItem, this->resetToolStripMenuItem
			});
			this->materialToolStripMenuItem->Name = L"materialToolStripMenuItem";
			this->materialToolStripMenuItem->Size = System::Drawing::Size(62, 20);
			this->materialToolStripMenuItem->Text = L"Material";
			// 
			// newToolStripMenuItem1
			// 
			this->newToolStripMenuItem1->Name = L"newToolStripMenuItem1";
			this->newToolStripMenuItem1->Size = System::Drawing::Size(117, 22);
			this->newToolStripMenuItem1->Text = L"New";
			this->newToolStripMenuItem1->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::newToolStripMenuItem1_Click);
			// 
			// removeToolStripMenuItem
			// 
			this->removeToolStripMenuItem->Name = L"removeToolStripMenuItem";
			this->removeToolStripMenuItem->Size = System::Drawing::Size(117, 22);
			this->removeToolStripMenuItem->Text = L"Remove";
			this->removeToolStripMenuItem->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::removeToolStripMenuItem_Click);
			// 
			// resetToolStripMenuItem
			// 
			this->resetToolStripMenuItem->Name = L"resetToolStripMenuItem";
			this->resetToolStripMenuItem->Size = System::Drawing::Size(117, 22);
			this->resetToolStripMenuItem->Text = L"Reset";
			// 
			// multiMaterialToolStripMenuItem
			// 
			this->multiMaterialToolStripMenuItem->DropDownItems->AddRange(gcnew cli::array< System::Windows::Forms::ToolStripItem^  >(3) {
				this->newToolStripMenuItem2,
					this->removeToolStripMenuItem1, this->resetToolStripMenuItem1
			});
			this->multiMaterialToolStripMenuItem->Name = L"multiMaterialToolStripMenuItem";
			this->multiMaterialToolStripMenuItem->Size = System::Drawing::Size(90, 20);
			this->multiMaterialToolStripMenuItem->Text = L"MultiMaterial";
			// 
			// newToolStripMenuItem2
			// 
			this->newToolStripMenuItem2->Name = L"newToolStripMenuItem2";
			this->newToolStripMenuItem2->Size = System::Drawing::Size(117, 22);
			this->newToolStripMenuItem2->Text = L"New";
			this->newToolStripMenuItem2->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::newToolStripMenuItem2_Click);
			// 
			// removeToolStripMenuItem1
			// 
			this->removeToolStripMenuItem1->Name = L"removeToolStripMenuItem1";
			this->removeToolStripMenuItem1->Size = System::Drawing::Size(117, 22);
			this->removeToolStripMenuItem1->Text = L"Remove";
			this->removeToolStripMenuItem1->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::removeToolStripMenuItem1_Click);
			// 
			// resetToolStripMenuItem1
			// 
			this->resetToolStripMenuItem1->Name = L"resetToolStripMenuItem1";
			this->resetToolStripMenuItem1->Size = System::Drawing::Size(117, 22);
			this->resetToolStripMenuItem1->Text = L"Reset";
			// 
			// editorToolStripMenuItem
			// 
			this->editorToolStripMenuItem->DropDownItems->AddRange(gcnew cli::array< System::Windows::Forms::ToolStripItem^  >(5) {
				this->cubeToolStripMenuItem,
					this->sphereToolStripMenuItem, this->panelToolStripMenuItem, this->monkeyToolStripMenuItem, this->emptyToolStripMenuItem
			});
			this->editorToolStripMenuItem->Name = L"editorToolStripMenuItem";
			this->editorToolStripMenuItem->Size = System::Drawing::Size(50, 20);
			this->editorToolStripMenuItem->Text = L"Editor";
			// 
			// cubeToolStripMenuItem
			// 
			this->cubeToolStripMenuItem->Name = L"cubeToolStripMenuItem";
			this->cubeToolStripMenuItem->Size = System::Drawing::Size(180, 22);
			this->cubeToolStripMenuItem->Text = L"Cube";
			this->cubeToolStripMenuItem->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::button2_Click);
			// 
			// sphereToolStripMenuItem
			// 
			this->sphereToolStripMenuItem->Name = L"sphereToolStripMenuItem";
			this->sphereToolStripMenuItem->Size = System::Drawing::Size(180, 22);
			this->sphereToolStripMenuItem->Text = L"Sphere";
			this->sphereToolStripMenuItem->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::button3_Click);
			// 
			// panelToolStripMenuItem
			// 
			this->panelToolStripMenuItem->Name = L"panelToolStripMenuItem";
			this->panelToolStripMenuItem->Size = System::Drawing::Size(180, 22);
			this->panelToolStripMenuItem->Text = L"Panel";
			this->panelToolStripMenuItem->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::button4_Click);
			// 
			// monkeyToolStripMenuItem
			// 
			this->monkeyToolStripMenuItem->Name = L"monkeyToolStripMenuItem";
			this->monkeyToolStripMenuItem->Size = System::Drawing::Size(180, 22);
			this->monkeyToolStripMenuItem->Text = L"Monkey";
			this->monkeyToolStripMenuItem->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::monkeyToolStripMenuItem_Click);
			// 
			// splitContainer1
			// 
			this->splitContainer1->Anchor = static_cast<System::Windows::Forms::AnchorStyles>((((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Bottom)
				| System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->splitContainer1->Location = System::Drawing::Point(0, 27);
			this->splitContainer1->Name = L"splitContainer1";
			// 
			// splitContainer1.Panel1
			// 
			this->splitContainer1->Panel1->Controls->Add(this->MainTab);
			// 
			// splitContainer1.Panel2
			// 
			this->splitContainer1->Panel2->Controls->Add(this->layerList);
			this->splitContainer1->Panel2->Controls->Add(this->buttonAddLayer);
			this->splitContainer1->Panel2->Controls->Add(this->button3);
			this->splitContainer1->Panel2->Controls->Add(this->button2);
			this->splitContainer1->Panel2->Controls->Add(this->label5);
			this->splitContainer1->Panel2->Controls->Add(this->label4);
			this->splitContainer1->Panel2->Controls->Add(this->multiMaterialList);
			this->splitContainer1->Panel2->Controls->Add(this->propertyGrid);
			this->splitContainer1->Panel2->Controls->Add(this->label2);
			this->splitContainer1->Panel2->Controls->Add(this->label1);
			this->splitContainer1->Panel2->Controls->Add(this->materialList);
			this->splitContainer1->Panel2->Controls->Add(this->button1);
			this->splitContainer1->Panel2->Controls->Add(this->rootFolder);
			this->splitContainer1->Panel2->Controls->Add(this->label3);
			this->splitContainer1->Size = System::Drawing::Size(1206, 655);
			this->splitContainer1->SplitterDistance = 923;
			this->splitContainer1->TabIndex = 10;
			// 
			// layerList
			// 
			this->layerList->Anchor = static_cast<System::Windows::Forms::AnchorStyles>(((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->layerList->FormattingEnabled = true;
			this->layerList->Location = System::Drawing::Point(10, 361);
			this->layerList->Name = L"layerList";
			this->layerList->Size = System::Drawing::Size(257, 82);
			this->layerList->TabIndex = 27;
			this->layerList->SelectedIndexChanged += gcnew System::EventHandler(this, &MaterialDesignerForm::OnLayerSelected);
			// 
			// buttonAddLayer
			// 
			this->buttonAddLayer->Anchor = static_cast<System::Windows::Forms::AnchorStyles>((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Right));
			this->buttonAddLayer->Location = System::Drawing::Point(241, 337);
			this->buttonAddLayer->Name = L"buttonAddLayer";
			this->buttonAddLayer->Size = System::Drawing::Size(26, 20);
			this->buttonAddLayer->TabIndex = 26;
			this->buttonAddLayer->Text = L"+";
			this->buttonAddLayer->UseVisualStyleBackColor = true;
			this->buttonAddLayer->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::button4_Click_1);
			// 
			// button3
			// 
			this->button3->Anchor = static_cast<System::Windows::Forms::AnchorStyles>((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Right));
			this->button3->Location = System::Drawing::Point(241, 191);
			this->button3->Name = L"button3";
			this->button3->Size = System::Drawing::Size(26, 20);
			this->button3->TabIndex = 25;
			this->button3->Text = L"+";
			this->button3->UseVisualStyleBackColor = true;
			this->button3->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::newToolStripMenuItem2_Click);
			// 
			// button2
			// 
			this->button2->Anchor = static_cast<System::Windows::Forms::AnchorStyles>((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Right));
			this->button2->Location = System::Drawing::Point(241, 44);
			this->button2->Name = L"button2";
			this->button2->Size = System::Drawing::Size(26, 20);
			this->button2->TabIndex = 24;
			this->button2->Text = L"+";
			this->button2->UseVisualStyleBackColor = true;
			this->button2->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::newToolStripMenuItem1_Click);
			// 
			// label5
			// 
			this->label5->Location = System::Drawing::Point(10, 337);
			this->label5->Name = L"label5";
			this->label5->Size = System::Drawing::Size(102, 18);
			this->label5->TabIndex = 23;
			this->label5->Text = L"Layers";
			this->label5->TextAlign = System::Drawing::ContentAlignment::MiddleLeft;
			// 
			// label4
			// 
			this->label4->Location = System::Drawing::Point(10, 192);
			this->label4->Name = L"label4";
			this->label4->Size = System::Drawing::Size(102, 18);
			this->label4->TabIndex = 22;
			this->label4->Text = L"MultiMaterials";
			this->label4->TextAlign = System::Drawing::ContentAlignment::MiddleLeft;
			// 
			// multiMaterialList
			// 
			this->multiMaterialList->Anchor = static_cast<System::Windows::Forms::AnchorStyles>(((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->multiMaterialList->FormattingEnabled = true;
			this->multiMaterialList->Location = System::Drawing::Point(10, 213);
			this->multiMaterialList->Name = L"multiMaterialList";
			this->multiMaterialList->Size = System::Drawing::Size(257, 121);
			this->multiMaterialList->TabIndex = 21;
			this->multiMaterialList->SelectedIndexChanged += gcnew System::EventHandler(this, &MaterialDesignerForm::OnMultiMaterialSelectedItemChanged);
			// 
			// propertyGrid
			// 
			this->propertyGrid->Anchor = static_cast<System::Windows::Forms::AnchorStyles>((((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Bottom)
				| System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->propertyGrid->Location = System::Drawing::Point(10, 462);
			this->propertyGrid->Name = L"propertyGrid";
			this->propertyGrid->Size = System::Drawing::Size(257, 190);
			this->propertyGrid->TabIndex = 20;
			this->propertyGrid->PropertyValueChanged += gcnew System::Windows::Forms::PropertyValueChangedEventHandler(this, &MaterialDesignerForm::propertyGrid_PropertyValueChanged);
			this->propertyGrid->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::propertyGrid_Click);
			// 
			// label2
			// 
			this->label2->AutoSize = true;
			this->label2->Location = System::Drawing::Point(10, 446);
			this->label2->Name = L"label2";
			this->label2->Size = System::Drawing::Size(54, 13);
			this->label2->TabIndex = 19;
			this->label2->Text = L"Properties";
			// 
			// label1
			// 
			this->label1->Location = System::Drawing::Point(7, 45);
			this->label1->Name = L"label1";
			this->label1->Size = System::Drawing::Size(63, 18);
			this->label1->TabIndex = 18;
			this->label1->Text = L"Materials";
			this->label1->TextAlign = System::Drawing::ContentAlignment::MiddleLeft;
			this->label1->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::label1_Click);
			// 
			// materialList
			// 
			this->materialList->Anchor = static_cast<System::Windows::Forms::AnchorStyles>(((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->materialList->FormattingEnabled = true;
			this->materialList->Location = System::Drawing::Point(10, 66);
			this->materialList->Name = L"materialList";
			this->materialList->Size = System::Drawing::Size(257, 121);
			this->materialList->TabIndex = 17;
			this->materialList->SelectedIndexChanged += gcnew System::EventHandler(this, &MaterialDesignerForm::materialList_SelectedIndexChanged);
			// 
			// button1
			// 
			this->button1->Anchor = static_cast<System::Windows::Forms::AnchorStyles>((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Right));
			this->button1->Location = System::Drawing::Point(241, 6);
			this->button1->Name = L"button1";
			this->button1->Size = System::Drawing::Size(26, 20);
			this->button1->TabIndex = 16;
			this->button1->Text = L"...";
			this->button1->UseVisualStyleBackColor = true;
			this->button1->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::button1_Click);
			// 
			// rootFolder
			// 
			this->rootFolder->Anchor = static_cast<System::Windows::Forms::AnchorStyles>(((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->rootFolder->Location = System::Drawing::Point(76, 6);
			this->rootFolder->Name = L"rootFolder";
			this->rootFolder->ReadOnly = true;
			this->rootFolder->Size = System::Drawing::Size(159, 20);
			this->rootFolder->TabIndex = 14;
			// 
			// label3
			// 
			this->label3->Location = System::Drawing::Point(7, 6);
			this->label3->Name = L"label3";
			this->label3->Size = System::Drawing::Size(63, 18);
			this->label3->TabIndex = 15;
			this->label3->Text = L"Root:";
			this->label3->TextAlign = System::Drawing::ContentAlignment::MiddleLeft;
			// 
			// emptyToolStripMenuItem
			// 
			this->emptyToolStripMenuItem->Name = L"emptyToolStripMenuItem";
			this->emptyToolStripMenuItem->Size = System::Drawing::Size(180, 22);
			this->emptyToolStripMenuItem->Text = L"Empty";
			this->emptyToolStripMenuItem->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::emptyToolStripMenuItem_Click);
			// 
			// MaterialDesignerForm
			// 
			this->AutoScaleDimensions = System::Drawing::SizeF(6, 13);
			this->AutoScaleMode = System::Windows::Forms::AutoScaleMode::Font;
			this->ClientSize = System::Drawing::Size(1206, 685);
			this->Controls->Add(this->splitContainer1);
			this->Controls->Add(this->menuStrip1);
			this->Icon = (cli::safe_cast<System::Drawing::Icon^>(resources->GetObject(L"$this.Icon")));
			this->MainMenuStrip = this->menuStrip1;
			this->Name = L"MaterialDesignerForm";
			this->Text = L"Material Designer";
			this->WindowState = System::Windows::Forms::FormWindowState::Maximized;
			this->MainTab->ResumeLayout(false);
			this->Design->ResumeLayout(false);
			this->Code->ResumeLayout(false);
			this->Code->PerformLayout();
			this->menuStrip1->ResumeLayout(false);
			this->menuStrip1->PerformLayout();
			this->splitContainer1->Panel1->ResumeLayout(false);
			this->splitContainer1->Panel2->ResumeLayout(false);
			this->splitContainer1->Panel2->PerformLayout();
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^>(this->splitContainer1))->EndInit();
			this->splitContainer1->ResumeLayout(false);
			this->ResumeLayout(false);
			this->PerformLayout();

		}
#pragma endregion
	private: System::Void newToolStripMenuItem2_Click(System::Object^ sender, System::EventArgs^ e) {
		MultiMaterial* m = new ::MultiMaterial(this);
		char name[32];
		for (int n = multiMaterials->Count + 1;; ++n) {
			snprintf(name, 32, "multiMaterial %d", n);
			if (GetMaterial<MultiMaterial>(gcnew System::String(name), multiMaterials) == nullptr) {
				break;
			}
		}
		m->props["name"]->SetValue<std::string>(name);
		IntPtr ptr(m);
		multiMaterials->Add(ptr);
		UpdateEditor();
	}

		   bool isLoading = false;
	private: System::Void openToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
		OpenFileDialog^ openFileDialog = gcnew OpenFileDialog();

		openFileDialog->Filter = "Material Files (*.mat)|*.mat";
		openFileDialog->FilterIndex = 1;
		openFileDialog->RestoreDirectory = true;
		isLoading = true;
		if (openFileDialog->ShowDialog() == System::Windows::Forms::DialogResult::OK) {
			try
			{
				std::string strFileName = msclr::interop::marshal_as<std::string>(openFileDialog->FileName);
				std::ifstream inputFile(strFileName);
				if (inputFile.is_open()) {
					std::string content((std::istreambuf_iterator<char>(inputFile)),
						std::istreambuf_iterator<char>());
					json js = json::parse(content);
					materialList->Items->Clear();
					for each (IntPtr p in materials) {
						Material* m = (Material*)p.ToPointer();
						delete m;
					}
					materials->Clear();
					rootFolder->Text = gcnew System::String(((std::string)js["root"]).c_str());
					for (const auto& mjs : js["materials"]) {
						Material* m = new Material(this);
						if (m != nullptr) {
							if (!m->FromJson(mjs)) {
								delete m;
								m = nullptr;
							}
							IntPtr ptr(m);
							materials->Add(ptr);
							materialList->Items->Add(gcnew String(m->props["name"]->GetValue<std::string>().c_str()));
						}
					}

					for (const auto& mjs : js["multi_materials"]) {
						MultiMaterial* m = new MultiMaterial(this);
						if (m != nullptr) {
							if (!m->FromJson(mjs)) {
								delete m;
								m = nullptr;
							}
							IntPtr ptr(m);
							multiMaterials->Add(ptr);
							multiMaterialList->Items->Add(gcnew String(m->props["name"]->GetValue<std::string>().c_str()));
						}
					}
					inputFile.close();
					fileName = openFileDialog->FileName;
					UpdateEditor();
					if (materialList->Items->Count > 0) {
						materialList->SelectedIndex = 0;
					}
				}
			}
			catch (...) {
				MessageBox::Show("Error loading file");
			}
		}
		isLoading = false;
	}
	private: System::Void exitToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
	}
	private: System::Void widgetToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
	}
	private: System::Void buttonToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
	}
	private: System::Void progressBarToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
	}
	private: System::Void labelToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
	}
	private: System::Void newToolStripMenuItem_Click_1(System::Object^ sender, System::EventArgs^ e) {
	}
	private: System::Void label1_Click(System::Object^ sender, System::EventArgs^ e) {
	}

		   json GetJson() {
			   json js;
			   js["root"] = msclr::interop::marshal_as<std::string>(rootFolder->Text);
			   std::vector<json> materials_list;
			   for each (IntPtr ptr in materials) {
				   Material* m = (Material*)ptr.ToPointer();
				   materials_list.push_back(m->ToJson());
			   }
			   js["materials"] = materials_list;

			   std::vector<json> multi_materials_list;
			   for each (IntPtr ptr in multiMaterials) {
				   MultiMaterial* m = (MultiMaterial*)ptr.ToPointer();
				   multi_materials_list.push_back(m->ToJson());
			   }
			   js["multi_materials"] = multi_materials_list;
			   auto ui_text = gcnew System::String(js.dump(4).c_str());
			   return js;
		   }

		   void UpdateMaterial() {
			   if (materialList->SelectedIndex >= 0) {
				   Material* m = GetMaterial<Material>(materialList->SelectedItem->ToString(), materials);
				   std::string mat_json = m->ToJson().dump();
				   HotBiteTool::ToolUi::SetMaterial("Cube", msclr::interop::marshal_as<std::string>(rootFolder->Text), mat_json);
				   HotBiteTool::ToolUi::SetMaterial("Sphere", msclr::interop::marshal_as<std::string>(rootFolder->Text), mat_json);
				   HotBiteTool::ToolUi::SetMaterial("Plane", msclr::interop::marshal_as<std::string>(rootFolder->Text), mat_json);
				   HotBiteTool::ToolUi::SetMaterial("Monkey", msclr::interop::marshal_as<std::string>(rootFolder->Text), mat_json);
			   }else if (multiMaterialList->SelectedIndex >= 0) {
				   MultiMaterial* m = GetMaterial<MultiMaterial>(multiMaterialList->SelectedItem->ToString(), multiMaterials);
				   std::string mat_json = m->ToJson().dump();
				   HotBiteTool::ToolUi::SetMultiMaterial("Cube", msclr::interop::marshal_as<std::string>(rootFolder->Text), mat_json);
				   HotBiteTool::ToolUi::SetMultiMaterial("Sphere", msclr::interop::marshal_as<std::string>(rootFolder->Text), mat_json);
				   HotBiteTool::ToolUi::SetMultiMaterial("Plane", msclr::interop::marshal_as<std::string>(rootFolder->Text), mat_json);
				   HotBiteTool::ToolUi::SetMultiMaterial("Monkey", msclr::interop::marshal_as<std::string>(rootFolder->Text), mat_json);
			   }
			   else {
				   auto dm = (Material*)defaultMaterial->ToPointer();
				   std::string mat_json = dm->ToJson().dump();
				   HotBiteTool::ToolUi::SetMaterial("Cube", msclr::interop::marshal_as<std::string>(rootFolder->Text), mat_json);
				   HotBiteTool::ToolUi::SetMaterial("Sphere", msclr::interop::marshal_as<std::string>(rootFolder->Text), mat_json);
				   HotBiteTool::ToolUi::SetMaterial("Plane", msclr::interop::marshal_as<std::string>(rootFolder->Text), mat_json);
				   HotBiteTool::ToolUi::SetMaterial("Monkey", msclr::interop::marshal_as<std::string>(rootFolder->Text), mat_json);
			   }
		   }

		   System::Void button1_Click(System::Object^ sender, System::EventArgs^ e) {
			   FolderBrowserDialog^ openFileDialog = gcnew FolderBrowserDialog();

			   if (openFileDialog->ShowDialog() == System::Windows::Forms::DialogResult::OK) {
				   try
				   {
					   rootFolder->Text = openFileDialog->SelectedPath;
				   }
				   catch (...) {
					   MessageBox::Show("Error loading file");
				   }
			   }
		   }

		   void UpdateEditor() {
			   HotBiteTool::ToolUi::SetVisible("Cube", false);
			   HotBiteTool::ToolUi::SetVisible("Sphere", false);
			   HotBiteTool::ToolUi::SetVisible("Plane", false);
			   HotBiteTool::ToolUi::SetVisible("Monkey", false);
			   switch (currentModel) {
			   case Model::CUBE: HotBiteTool::ToolUi::SetVisible("Cube", true); break;
			   case Model::SPHERE: HotBiteTool::ToolUi::SetVisible("Sphere", true); break;
			   case Model::PLANE: HotBiteTool::ToolUi::SetVisible("Plane", true); break;
			   case Model::CUSTOM: HotBiteTool::ToolUi::SetVisible("Monkey", true); break;
			   default: break;
			   }
			   materialList->Items->Clear();
			   for each (IntPtr p in materials) {
				   Material* m = (Material*)p.ToPointer();
				   materialList->Items->Add(gcnew System::String(m->props["name"]->GetValue<std::string>().c_str()));
			   }
			   multiMaterialList->Items->Clear();
			   for each (IntPtr p in multiMaterials) {
				   MultiMaterial* m = (MultiMaterial*)p.ToPointer();
				   multiMaterialList->Items->Add(gcnew System::String(m->props["name"]->GetValue<std::string>().c_str()));
			   }

			   UpdateCode();
			   UpdateMaterial();
		   }

		   void UpdateCode() {
			   json js = GetJson();
			   auto ui_text = gcnew System::String(js.dump(4).c_str());
			   textEditor->Text = ui_text->Replace("\n", "\r\n");
		   }

		   void UpdateLayers() {
			   layerList->Items->Clear();
			   if (multiMaterialList->SelectedItem != nullptr) {
				   char layerName[32];
				   int i = 0;
				   auto mm = GetMaterial<MultiMaterial>(multiMaterialList->SelectedItem->ToString(), multiMaterials);
				   for (const auto& layer : mm->layers) {
					   snprintf(layerName, 32, "layer %d", i++);
					   layerList->Items->Add(gcnew String(layerName));
				   }
			   }
			   UpdateCode();
		   }

	private: System::Void newToolStripMenuItem1_Click(System::Object^ sender, System::EventArgs^ e) {
		Material* m = new ::Material(this);
		char name[32];
		for (int n = materials->Count + 1;; ++n) {
			snprintf(name, 32, "material %d", n);
			if (GetMaterial<Material>(gcnew System::String(name), materials) == nullptr) {
				break;
			}
		}
		m->props["name"]->SetValue<std::string>(name);
		IntPtr ptr(m);
		materials->Add(ptr);
		UpdateEditor();
	}

		   System::Void saveAsToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
			   SaveFileDialog^ saveFileDialog = gcnew SaveFileDialog();

			   saveFileDialog->Filter = "Material Files (*.mat)|*.mat";
			   saveFileDialog->FilterIndex = 1;
			   saveFileDialog->RestoreDirectory = true;

			   if (saveFileDialog->ShowDialog() == System::Windows::Forms::DialogResult::OK) {
				   auto content = GetJson().dump(4);
				   std::string strFileName = msclr::interop::marshal_as<std::string>(saveFileDialog->FileName);

				   std::ofstream outputFile(strFileName);
				   if (outputFile.is_open()) {
					   outputFile << content;
					   outputFile.close();
					   fileName = saveFileDialog->FileName;
				   }
				   else {
					   // Handle error opening the file
					   MessageBox::Show("Error opening file for writing.");
				   }
			   }
		   }

	private: System::Void materialList_SelectedIndexChanged(System::Object^ sender, System::EventArgs^ e) {
		if (materialList->SelectedIndex >= 0)
		{
			Material* m = GetMaterial<Material>(materialList->Text, materials);
			if (m != nullptr) {
				MaterialProp^ prop = gcnew MaterialProp(m, rootFolder->Text);
				propertyGrid->SelectedObject = prop;
			}
			multiMaterialList->SelectedIndex = -1;
		}
		else {
			propertyGrid->SelectedObject = nullptr;
		}
		UpdateMaterial();
	}

		   System::Void saveToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
			   if (fileName == nullptr) {
				   return saveAsToolStripMenuItem_Click(sender, e);
			   }

			   auto content = GetJson().dump(4);
			   marshal_context context;
			   std::string strFileName = context.marshal_as<std::string>(fileName);

			   std::ofstream outputFile(strFileName);
			   if (outputFile.is_open()) {
				   outputFile << content;
				   outputFile.close();
			   }
			   else {
				   // Handle error opening the file
				   MessageBox::Show("Error opening file for writing.");
			   }
		   }


	private: System::Void propertyGrid_Click(System::Object^ sender, System::EventArgs^ e) {
	}
	private: System::Void propertyGrid_PropertyValueChanged(System::Object^ s, System::Windows::Forms::PropertyValueChangedEventArgs^ e) {
		UpdateMaterial();
	}
	private: System::Void button2_Click(System::Object^ sender, System::EventArgs^ e) {
		currentModel = Model::CUBE;
		UpdateEditor();
	}
	private: System::Void button3_Click(System::Object^ sender, System::EventArgs^ e) {
		currentModel = Model::SPHERE;
		UpdateEditor();
	}
	private: System::Void button4_Click(System::Object^ sender, System::EventArgs^ e) {
		currentModel = Model::PLANE;
		UpdateEditor();
	}

	private: System::Void emptyToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
		currentModel = Model::NONE;
		UpdateEditor();
	}

	private: System::Void monkeyToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
		currentModel = Model::CUSTOM;
		UpdateEditor();
	}

	private: System::Void removeToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
		if (materialList->SelectedItem != nullptr) {
			DeleteMaterial<Material>(materialList->SelectedItem->ToString(), materials);
			UpdateEditor();
		}
	}


	private: System::Void removeToolStripMenuItem1_Click(System::Object^ sender, System::EventArgs^ e) {
		if (multiMaterialList->SelectedItem != nullptr) {
			DeleteMaterial<MultiMaterial>(multiMaterialList->SelectedItem->ToString(), multiMaterials);
			UpdateEditor();
		}
	}

	private: System::Void button4_Click_1(System::Object^ sender, System::EventArgs^ e) {
		if (multiMaterialList->SelectedItem != nullptr) {
			auto mm = GetMaterial<MultiMaterial>(multiMaterialList->SelectedItem->ToString(), multiMaterials);
			mm->layers.push_back(MultiMaterialLayer());
			UpdateLayers();
		}
	}
	private: System::Void OnMultiMaterialSelectedItemChanged(System::Object^ sender, System::EventArgs^ e) {
		bool layer_enabled = multiMaterialList->SelectedIndex >= 0;
		if (layer_enabled) {
			materialList->SelectedIndex = -1;
			auto mm = GetMaterial<MultiMaterial>(multiMaterialList->SelectedItem->ToString(), multiMaterials);
			MultiMaterialProp^ prop = gcnew MultiMaterialProp(mm, rootFolder->Text);
			propertyGrid->SelectedObject = prop;
		}
		buttonAddLayer->Enabled = layer_enabled;
		layerList->Enabled = layer_enabled;
		UpdateLayers();
	}
	private: System::Void OnLayerSelected(System::Object^ sender, System::EventArgs^ e) {
		bool layer_selected = (layerList->SelectedIndex >= 0 && multiMaterialList->SelectedIndex >= 0);
		propertyGrid->SelectedObject = nullptr;
		if (layer_selected) {
			materialList->SelectedIndex = -1;
			auto mm = GetMaterial<MultiMaterial>(multiMaterialList->SelectedItem->ToString(), multiMaterials);
			if (mm != nullptr) {
				if (mm->layers.size() >= layerList->SelectedIndex) {
					MultiMaterialLayerProp^ prop = gcnew MultiMaterialLayerProp(&mm->layers[layerList->SelectedIndex], rootFolder->Text, materials);
					propertyGrid->SelectedObject = prop;
				}
			}
		}
	}
};
}
