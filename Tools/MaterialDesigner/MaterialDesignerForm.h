#pragma once

#include <HotBiteTool.h>
#include "Materials.h"

namespace MaterialDesigner {

	using namespace System;
	using namespace System::ComponentModel;
	using namespace System::Collections;
	using namespace System::Windows::Forms;
	using namespace System::Data;
	using namespace System::Drawing;

	/// <summary>
	/// Resumen de MaterialDesignerForm
	/// </summary>
	public ref class MaterialDesignerForm : public System::Windows::Forms::Form, public IMaterialListener
	{
		
	public:
		
		MaterialDesignerForm(void)
		{
			InitializeComponent();
			materials = gcnew Generic::List<IntPtr>();
			HotBiteTool::ToolUi::CreateToolUi(GetModuleHandle(NULL), (HWND)preview->Handle.ToPointer(), 1280, 800, 60);
			std::string root = "..\\..\\..\\Tools\\MaterialDesigner\\";

			HotBiteTool::ToolUi::LoadWorld(root + "material_scene.json");
			HotBiteTool::ToolUi::RotateEntity("Cube");
			rootFolder->Text = Environment::GetFolderPath(Environment::SpecialFolder::MyDocuments);

		}

		virtual void OnNameChanged(String^ old_name, String^ new_name)
		{

		}
	protected: System::Windows::Forms::Label^ label1;
	protected: System::Windows::Forms::PropertyGrid^ propertyGrid;
	protected: System::Windows::Forms::Label^ label2;
	public:

	protected:

		Generic::List<IntPtr>^ materials;
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

	private:
		/// <summary>
		/// Variable del diseñador necesaria.
		/// </summary>
		System::ComponentModel::Container ^components;

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
			this->editorToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->cubeToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->splitContainer1 = (gcnew System::Windows::Forms::SplitContainer());
			this->propertyGrid = (gcnew System::Windows::Forms::PropertyGrid());
			this->label2 = (gcnew System::Windows::Forms::Label());
			this->label1 = (gcnew System::Windows::Forms::Label());
			this->materialList = (gcnew System::Windows::Forms::ListBox());
			this->button1 = (gcnew System::Windows::Forms::Button());
			this->rootFolder = (gcnew System::Windows::Forms::TextBox());
			this->label3 = (gcnew System::Windows::Forms::Label());
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
			this->menuStrip1->Items->AddRange(gcnew cli::array< System::Windows::Forms::ToolStripItem^  >(3) {
				this->fileToolStripMenuItem,
					this->materialToolStripMenuItem, this->editorToolStripMenuItem
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
			// 
			// saveToolStripMenuItem
			// 
			this->saveToolStripMenuItem->Name = L"saveToolStripMenuItem";
			this->saveToolStripMenuItem->Size = System::Drawing::Size(123, 22);
			this->saveToolStripMenuItem->Text = L"Save";
			// 
			// saveAsToolStripMenuItem
			// 
			this->saveAsToolStripMenuItem->Name = L"saveAsToolStripMenuItem";
			this->saveAsToolStripMenuItem->Size = System::Drawing::Size(123, 22);
			this->saveAsToolStripMenuItem->Text = L"Save As...";
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
			// 
			// resetToolStripMenuItem
			// 
			this->resetToolStripMenuItem->Name = L"resetToolStripMenuItem";
			this->resetToolStripMenuItem->Size = System::Drawing::Size(117, 22);
			this->resetToolStripMenuItem->Text = L"Reset";
			// 
			// editorToolStripMenuItem
			// 
			this->editorToolStripMenuItem->DropDownItems->AddRange(gcnew cli::array< System::Windows::Forms::ToolStripItem^  >(1) { this->cubeToolStripMenuItem });
			this->editorToolStripMenuItem->Name = L"editorToolStripMenuItem";
			this->editorToolStripMenuItem->Size = System::Drawing::Size(50, 20);
			this->editorToolStripMenuItem->Text = L"Editor";
			// 
			// cubeToolStripMenuItem
			// 
			this->cubeToolStripMenuItem->Name = L"cubeToolStripMenuItem";
			this->cubeToolStripMenuItem->Size = System::Drawing::Size(102, 22);
			this->cubeToolStripMenuItem->Text = L"Cube";
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
			// propertyGrid
			// 
			this->propertyGrid->Anchor = static_cast<System::Windows::Forms::AnchorStyles>((((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Bottom)
				| System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->propertyGrid->Location = System::Drawing::Point(10, 356);
			this->propertyGrid->Name = L"propertyGrid";
			this->propertyGrid->Size = System::Drawing::Size(257, 296);
			this->propertyGrid->TabIndex = 20;
			this->propertyGrid->PropertyValueChanged += gcnew System::Windows::Forms::PropertyValueChangedEventHandler(this, &MaterialDesignerForm::propertyGrid_PropertyValueChanged);
			this->propertyGrid->Click += gcnew System::EventHandler(this, &MaterialDesignerForm::propertyGrid_Click);
			// 
			// label2
			// 
			this->label2->AutoSize = true;
			this->label2->Location = System::Drawing::Point(10, 337);
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
			this->materialList->Size = System::Drawing::Size(257, 264);
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
	private: System::Void newToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
	}
private: System::Void openToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
	}
private: System::Void saveToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
	}
private: System::Void saveAsToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
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
		   auto ui_text = gcnew System::String(js.dump(4).c_str());
		   return js;
	   }

	   Material* GetMaterial(System::String^ name) {
		   for each (IntPtr p in materials) {
			   Material* m = (Material*)p.ToPointer();
			   if (String::Compare(gcnew System::String(m->props["name"]->GetValue<std::string>().c_str()), name) == 0) {
				   return m;
			   }
		   }
		   return nullptr;
	   }

	   void UpdateMaterial() {
		   if (materialList->SelectedIndex >= 0) {
			   Material* m = GetMaterial(materialList->SelectedItem->ToString());
			   HotBiteTool::ToolUi::SetMaterial("Cube", m->ToJson().dump());
		   }
	   }

	   void UpdateEditor() {
		   materialList->Items->Clear();
		   for each (IntPtr p in materials) {
			   Material* m = (Material*)p.ToPointer();
			   materialList->Items->Add(gcnew System::String(m->props["name"]->GetValue<std::string>().c_str()));
		   }

		   json js = GetJson();
		   auto ui_text = gcnew System::String(js.dump(4).c_str());
		   textEditor->Text = ui_text->Replace("\n", "\r\n");
		   UpdateMaterial();
	   }

private: System::Void newToolStripMenuItem1_Click(System::Object^ sender, System::EventArgs^ e) {
	Material* m = new ::Material(this);
	char name[32];
	for (int n = materials->Count + 1;; ++n) {
		snprintf(name, 32, "material %d", n);
		if (GetMaterial(gcnew System::String(name)) == nullptr) {
			break;
		}
	}
	m->props["name"]->SetValue<std::string>(name);
	IntPtr ptr(m);
	materials->Add(ptr);
	UpdateEditor();
}
private: System::Void materialList_SelectedIndexChanged(System::Object^ sender, System::EventArgs^ e) {
		Material* m = GetMaterial(materialList->Text);
		if (m != nullptr) {
			MaterialProp^ prop = gcnew MaterialProp(m, rootFolder->Text);
			propertyGrid->SelectedObject = prop;
		}
	}
private: System::Void propertyGrid_Click(System::Object^ sender, System::EventArgs^ e) {
}
private: System::Void propertyGrid_PropertyValueChanged(System::Object^ s, System::Windows::Forms::PropertyValueChangedEventArgs^ e) {
	UpdateMaterial();
}
};
}
