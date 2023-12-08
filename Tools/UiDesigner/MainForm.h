#pragma once

#include <HotBiteTool.h>
#include "Widgets.h"
#include "Utils.h"

#include <map>
#include <msclr/marshal_cppstd.h>
#include <vcclr.h>
#include <string>
#include <memory>
#include <iostream>
#include <map>

#pragma comment(lib, "Engine.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "User32.lib")

namespace UiDesigner {

	using namespace System;
	using namespace System::ComponentModel;
	using namespace System::Collections;
	using namespace System::Windows::Forms;
	using namespace System::Data;
	using namespace System::Drawing;
	using namespace System::Collections::Generic;
	
	/// <summary>
	/// Resumen de MainForm
	/// </summary>
	public ref class MainForm : public System::Windows::Forms::Form
	{
	public:
		MainForm(void)
		{
			InitializeComponent();
			widgets = gcnew Generic::List<IntPtr>();
			HotBiteTool::ToolUi::CreateToolUi(GetModuleHandle(NULL), (HWND)preview->Handle.ToPointer());

		}

	protected:
		/// <summary>
		/// Limpiar los recursos que se estén usando.
		/// </summary>
		~MainForm()
		{
			HotBiteTool::ToolUi::DeleteToolUi();
			if (components)
			{
				delete components;
			}
		}
	private: System::Windows::Forms::MenuStrip^ menuStrip1;
	protected:
	private: System::Windows::Forms::ToolStripMenuItem^ fileToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ openToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ saveToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ exitToolStripMenuItem;





	private: System::Windows::Forms::TreeView^ treeView;


	private: System::Windows::Forms::Label^ label2;
	private: System::Windows::Forms::PropertyGrid^ propertyGrid;

	private: System::Windows::Forms::ToolStripMenuItem^ insertToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ labelToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ buttonToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ progressBarToolStripMenuItem;
	private: System::Windows::Forms::ToolStripMenuItem^ widgetToolStripMenuItem;

	Generic::List<IntPtr>^ widgets;
	private: System::Windows::Forms::TabControl^ MainTab;
	private: System::Windows::Forms::TabPage^ Design;
	private: System::Windows::Forms::Panel^ preview;
	private: System::Windows::Forms::TabPage^ Code;
	private: System::Windows::Forms::TextBox^ textEditor;
	private: System::Windows::Forms::SplitContainer^ splitContainer1;
	private: System::Windows::Forms::Label^ label1;
	private: System::Windows::Forms::TextBox^ uiName;




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
			this->menuStrip1 = (gcnew System::Windows::Forms::MenuStrip());
			this->fileToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->openToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->saveToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->exitToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->insertToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->labelToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->buttonToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->progressBarToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->widgetToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->treeView = (gcnew System::Windows::Forms::TreeView());
			this->label2 = (gcnew System::Windows::Forms::Label());
			this->propertyGrid = (gcnew System::Windows::Forms::PropertyGrid());
			this->MainTab = (gcnew System::Windows::Forms::TabControl());
			this->Code = (gcnew System::Windows::Forms::TabPage());
			this->textEditor = (gcnew System::Windows::Forms::TextBox());
			this->Design = (gcnew System::Windows::Forms::TabPage());
			this->preview = (gcnew System::Windows::Forms::Panel());
			this->splitContainer1 = (gcnew System::Windows::Forms::SplitContainer());
			this->uiName = (gcnew System::Windows::Forms::TextBox());
			this->label1 = (gcnew System::Windows::Forms::Label());
			this->menuStrip1->SuspendLayout();
			this->MainTab->SuspendLayout();
			this->Code->SuspendLayout();
			this->Design->SuspendLayout();
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^>(this->splitContainer1))->BeginInit();
			this->splitContainer1->Panel1->SuspendLayout();
			this->splitContainer1->Panel2->SuspendLayout();
			this->splitContainer1->SuspendLayout();
			this->SuspendLayout();
			// 
			// menuStrip1
			// 
			this->menuStrip1->Items->AddRange(gcnew cli::array< System::Windows::Forms::ToolStripItem^  >(2) {
				this->fileToolStripMenuItem,
					this->insertToolStripMenuItem
			});
			this->menuStrip1->Location = System::Drawing::Point(0, 0);
			this->menuStrip1->Name = L"menuStrip1";
			this->menuStrip1->Size = System::Drawing::Size(1159, 24);
			this->menuStrip1->TabIndex = 1;
			this->menuStrip1->Text = L"menuStrip1";
			// 
			// fileToolStripMenuItem
			// 
			this->fileToolStripMenuItem->DropDownItems->AddRange(gcnew cli::array< System::Windows::Forms::ToolStripItem^  >(3) {
				this->openToolStripMenuItem,
					this->saveToolStripMenuItem, this->exitToolStripMenuItem
			});
			this->fileToolStripMenuItem->Name = L"fileToolStripMenuItem";
			this->fileToolStripMenuItem->Size = System::Drawing::Size(37, 20);
			this->fileToolStripMenuItem->Text = L"File";
			// 
			// openToolStripMenuItem
			// 
			this->openToolStripMenuItem->Name = L"openToolStripMenuItem";
			this->openToolStripMenuItem->Size = System::Drawing::Size(112, 22);
			this->openToolStripMenuItem->Text = L"Open...";
			// 
			// saveToolStripMenuItem
			// 
			this->saveToolStripMenuItem->Name = L"saveToolStripMenuItem";
			this->saveToolStripMenuItem->Size = System::Drawing::Size(112, 22);
			this->saveToolStripMenuItem->Text = L"Save...";
			// 
			// exitToolStripMenuItem
			// 
			this->exitToolStripMenuItem->Name = L"exitToolStripMenuItem";
			this->exitToolStripMenuItem->Size = System::Drawing::Size(112, 22);
			this->exitToolStripMenuItem->Text = L"Exit";
			// 
			// insertToolStripMenuItem
			// 
			this->insertToolStripMenuItem->DropDownItems->AddRange(gcnew cli::array< System::Windows::Forms::ToolStripItem^  >(4) {
				this->labelToolStripMenuItem,
					this->buttonToolStripMenuItem, this->progressBarToolStripMenuItem, this->widgetToolStripMenuItem
			});
			this->insertToolStripMenuItem->Name = L"insertToolStripMenuItem";
			this->insertToolStripMenuItem->Size = System::Drawing::Size(48, 20);
			this->insertToolStripMenuItem->Text = L"Insert";
			// 
			// labelToolStripMenuItem
			// 
			this->labelToolStripMenuItem->Name = L"labelToolStripMenuItem";
			this->labelToolStripMenuItem->Size = System::Drawing::Size(139, 22);
			this->labelToolStripMenuItem->Text = L"Widget";
			this->labelToolStripMenuItem->Click += gcnew System::EventHandler(this, &MainForm::labelToolStripMenuItem_Click);
			// 
			// buttonToolStripMenuItem
			// 
			this->buttonToolStripMenuItem->Name = L"buttonToolStripMenuItem";
			this->buttonToolStripMenuItem->Size = System::Drawing::Size(139, 22);
			this->buttonToolStripMenuItem->Text = L"Button";
			// 
			// progressBarToolStripMenuItem
			// 
			this->progressBarToolStripMenuItem->Name = L"progressBarToolStripMenuItem";
			this->progressBarToolStripMenuItem->Size = System::Drawing::Size(139, 22);
			this->progressBarToolStripMenuItem->Text = L"Progress Bar";
			// 
			// widgetToolStripMenuItem
			// 
			this->widgetToolStripMenuItem->Name = L"widgetToolStripMenuItem";
			this->widgetToolStripMenuItem->Size = System::Drawing::Size(139, 22);
			this->widgetToolStripMenuItem->Text = L"Label";
			// 
			// treeView
			// 
			this->treeView->Anchor = static_cast<System::Windows::Forms::AnchorStyles>(((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->treeView->Location = System::Drawing::Point(3, 25);
			this->treeView->Name = L"treeView";
			this->treeView->Size = System::Drawing::Size(348, 273);
			this->treeView->TabIndex = 3;
			this->treeView->NodeMouseClick += gcnew System::Windows::Forms::TreeNodeMouseClickEventHandler(this, &MainForm::NodeClick);
			// 
			// label2
			// 
			this->label2->AutoSize = true;
			this->label2->Location = System::Drawing::Point(3, 301);
			this->label2->Name = L"label2";
			this->label2->Size = System::Drawing::Size(54, 13);
			this->label2->TabIndex = 5;
			this->label2->Text = L"Properties";
			// 
			// propertyGrid
			// 
			this->propertyGrid->Anchor = static_cast<System::Windows::Forms::AnchorStyles>((((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Bottom)
				| System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->propertyGrid->Location = System::Drawing::Point(3, 320);
			this->propertyGrid->Name = L"propertyGrid";
			this->propertyGrid->Size = System::Drawing::Size(348, 277);
			this->propertyGrid->TabIndex = 6;
			this->propertyGrid->PropertyValueChanged += gcnew System::Windows::Forms::PropertyValueChangedEventHandler(this, &MainForm::propertyGrid_PropertyValueChanged);
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
			this->MainTab->Size = System::Drawing::Size(792, 597);
			this->MainTab->TabIndex = 7;
			// 
			// Code
			// 
			this->Code->Controls->Add(this->textEditor);
			this->Code->Location = System::Drawing::Point(4, 22);
			this->Code->Name = L"Code";
			this->Code->Padding = System::Windows::Forms::Padding(3);
			this->Code->Size = System::Drawing::Size(740, 571);
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
			this->textEditor->Size = System::Drawing::Size(740, 573);
			this->textEditor->TabIndex = 0;
			this->textEditor->TextChanged += gcnew System::EventHandler(this, &MainForm::textEditor_TextChanged);
			// 
			// Design
			// 
			this->Design->Controls->Add(this->preview);
			this->Design->Location = System::Drawing::Point(4, 22);
			this->Design->Name = L"Design";
			this->Design->Padding = System::Windows::Forms::Padding(3);
			this->Design->Size = System::Drawing::Size(784, 571);
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
			this->preview->Size = System::Drawing::Size(784, 575);
			this->preview->TabIndex = 0;
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
			this->splitContainer1->Panel2->Controls->Add(this->label1);
			this->splitContainer1->Panel2->Controls->Add(this->uiName);
			this->splitContainer1->Panel2->Controls->Add(this->treeView);
			this->splitContainer1->Panel2->Controls->Add(this->propertyGrid);
			this->splitContainer1->Panel2->Controls->Add(this->label2);
			this->splitContainer1->Size = System::Drawing::Size(1156, 600);
			this->splitContainer1->SplitterDistance = 798;
			this->splitContainer1->TabIndex = 8;
			// 
			// uiName
			// 
			this->uiName->Anchor = static_cast<System::Windows::Forms::AnchorStyles>(((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->uiName->Location = System::Drawing::Point(75, 4);
			this->uiName->Name = L"uiName";
			this->uiName->Size = System::Drawing::Size(276, 20);
			this->uiName->TabIndex = 7;
			this->uiName->Text = L"ui_1";
			// 
			// label1
			// 
			this->label1->Location = System::Drawing::Point(6, 4);
			this->label1->Name = L"label1";
			this->label1->Size = System::Drawing::Size(63, 18);
			this->label1->TabIndex = 8;
			this->label1->Text = L"UI Name:";
			this->label1->TextAlign = System::Drawing::ContentAlignment::MiddleLeft;
			// 
			// MainForm
			// 
			this->AutoScaleDimensions = System::Drawing::SizeF(6, 13);
			this->AutoScaleMode = System::Windows::Forms::AutoScaleMode::Font;
			this->ClientSize = System::Drawing::Size(1159, 631);
			this->Controls->Add(this->splitContainer1);
			this->Controls->Add(this->menuStrip1);
			this->MainMenuStrip = this->menuStrip1;
			this->Name = L"MainForm";
			this->Text = L"Ui Designer";
			this->Load += gcnew System::EventHandler(this, &MainForm::MainForm_Load);
			this->menuStrip1->ResumeLayout(false);
			this->menuStrip1->PerformLayout();
			this->MainTab->ResumeLayout(false);
			this->Code->ResumeLayout(false);
			this->Code->PerformLayout();
			this->Design->ResumeLayout(false);
			this->splitContainer1->Panel1->ResumeLayout(false);
			this->splitContainer1->Panel2->ResumeLayout(false);
			this->splitContainer1->Panel2->PerformLayout();
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^>(this->splitContainer1))->EndInit();
			this->splitContainer1->ResumeLayout(false);
			this->ResumeLayout(false);
			this->PerformLayout();

		}
#pragma endregion
	private: System::Void MainForm_Load(System::Object^ sender, System::EventArgs^ e) {
	}

	private: System::Void labelToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
		Widget* widget = new Widget;
		char name[32];
		snprintf(name, 32, "widget %d", widgets->Count + 1);
		widget->props["name"]->SetValue<std::string>(name);
		IntPtr ptr(widget);
		widgets->Add(ptr);

		UpdateEditor();
	}

    void UpdateEditor() {
		treeView->Nodes->Clear();
		json js;
		js["ui"]["name"] = msclr::interop::marshal_as<std::string>(uiName->Text);
		std::vector<json> widgets_list;
		for each (IntPtr ptr in widgets) {
			Widget* widget = (Widget*)ptr.ToPointer();
			widgets_list.push_back(widget->ToJson());
			treeView->Nodes->Add(gcnew System::String(widget->props["name"]->GetValue<std::string>().c_str()));
		}
		js["ui"]["widgets"] = widgets_list;
		auto ui_text = gcnew System::String(js.dump(4).c_str());
		textEditor->Text = ui_text->Replace("\n", "\r\n");
		HotBiteTool::ToolUi::LoadUI(js);
	}

	private: System::Void NodeClick(System::Object^ sender, System::Windows::Forms::TreeNodeMouseClickEventArgs^ e) {
		Widget* widget = nullptr;
		for each (IntPtr ptr in widgets) {
			Widget* widget = (Widget*)ptr.ToPointer();
			if (String::Compare(e->Node->Text, gcnew String(widget->props["name"]->GetValue<std::string>().c_str())) == 0) {
				WidgetProp^ prop = gcnew WidgetProp(ptr);
				propertyGrid->SelectedObject = prop;
				break;
			}
		}
		
	}
	private: System::Void propertyGrid_PropertyValueChanged(System::Object^ s, System::Windows::Forms::PropertyValueChangedEventArgs^ e) {
		UpdateEditor();
	}
private: System::Void textEditor_TextChanged(System::Object^ sender, System::EventArgs^ e) {
}
};
}
