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
#include <fstream>
#include <map>
using namespace msclr::interop;

#pragma comment(lib, "Engine.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "User32.lib")

namespace UiDesigner {

	using namespace System;
	using namespace System::ComponentModel;
	using namespace System::Collections;
	using namespace System::Data;
	using namespace System::Drawing;
	using namespace System::Collections::Generic;

	/// <summary>
	/// Resumen de MainForm
	/// </summary>
	public ref class MainForm : public System::Windows::Forms::Form, public IWidgetListener
	{
	public:
		MainForm(void)
		{
			InitializeComponent();
			widgets = gcnew Generic::List<IntPtr>();
			widgets_tree = gcnew Generic::List<IntPtr>();
			HotBiteTool::ToolUi::CreateToolUi(GetModuleHandle(NULL), (HWND)preview->Handle.ToPointer());
			rootFolder->Text = Environment::GetFolderPath(Environment::SpecialFolder::MyDocuments);
			UpdateEditor();
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
		bool isLoading = false;
		System::Windows::Forms::MenuStrip^ menuStrip1;
	protected:
		System::Windows::Forms::ToolStripMenuItem^ fileToolStripMenuItem;
		System::Windows::Forms::ToolStripMenuItem^ openToolStripMenuItem;
		System::Windows::Forms::ToolStripMenuItem^ saveToolStripMenuItem;
		System::Windows::Forms::ToolStripMenuItem^ exitToolStripMenuItem;
		System::Windows::Forms::TreeView^ treeView;
		System::Windows::Forms::Label^ label2;
		System::Windows::Forms::PropertyGrid^ propertyGrid;
		System::Windows::Forms::ToolStripMenuItem^ insertToolStripMenuItem;
		System::Windows::Forms::ToolStripMenuItem^ labelToolStripMenuItem;
		System::Windows::Forms::ToolStripMenuItem^ buttonToolStripMenuItem;
		System::Windows::Forms::ToolStripMenuItem^ progressBarToolStripMenuItem;
		System::Windows::Forms::ToolStripMenuItem^ widgetToolStripMenuItem;
		System::Windows::Forms::TabControl^ MainTab;
		System::Windows::Forms::TabPage^ Design;
		System::Windows::Forms::Panel^ preview;
		System::Windows::Forms::TabPage^ Code;
		System::Windows::Forms::TextBox^ textEditor;
		System::Windows::Forms::SplitContainer^ splitContainer1;
		System::Windows::Forms::Label^ label1;
		System::Windows::Forms::TextBox^ uiName;
		System::Windows::Forms::ToolStripMenuItem^ newToolStripMenuItem;
		System::Windows::Forms::ToolStripMenuItem^ saveAsToolStripMenuItem;
		System::Windows::Forms::Button^ button1;
		System::Windows::Forms::Label^ label3;
		System::Windows::Forms::TextBox^ rootFolder;
		System::Windows::Forms::ToolStripSeparator^ toolStripSeparator1;
		System::ComponentModel::Container^ components;

		Generic::List<IntPtr>^ widgets;
		Generic::List<IntPtr>^ widgets_tree;

#pragma region Windows Form Designer generated code>
		void InitializeComponent(void)
		{
			System::ComponentModel::ComponentResourceManager^ resources = (gcnew System::ComponentModel::ComponentResourceManager(MainForm::typeid));
			this->menuStrip1 = (gcnew System::Windows::Forms::MenuStrip());
			this->fileToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->newToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->openToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->saveToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->saveAsToolStripMenuItem = (gcnew System::Windows::Forms::ToolStripMenuItem());
			this->toolStripSeparator1 = (gcnew System::Windows::Forms::ToolStripSeparator());
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
			this->Design = (gcnew System::Windows::Forms::TabPage());
			this->preview = (gcnew System::Windows::Forms::Panel());
			this->Code = (gcnew System::Windows::Forms::TabPage());
			this->textEditor = (gcnew System::Windows::Forms::TextBox());
			this->splitContainer1 = (gcnew System::Windows::Forms::SplitContainer());
			this->button1 = (gcnew System::Windows::Forms::Button());
			this->label3 = (gcnew System::Windows::Forms::Label());
			this->rootFolder = (gcnew System::Windows::Forms::TextBox());
			this->label1 = (gcnew System::Windows::Forms::Label());
			this->uiName = (gcnew System::Windows::Forms::TextBox());
			this->menuStrip1->SuspendLayout();
			this->MainTab->SuspendLayout();
			this->Design->SuspendLayout();
			this->Code->SuspendLayout();
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
			this->menuStrip1->Size = System::Drawing::Size(1179, 24);
			this->menuStrip1->TabIndex = 1;
			this->menuStrip1->Text = L"menuStrip1";
			// 
			// fileToolStripMenuItem
			// 
			this->fileToolStripMenuItem->DropDownItems->AddRange(gcnew cli::array< System::Windows::Forms::ToolStripItem^  >(6) {
				this->newToolStripMenuItem,
					this->openToolStripMenuItem, this->saveToolStripMenuItem, this->saveAsToolStripMenuItem, this->toolStripSeparator1, this->exitToolStripMenuItem
			});
			this->fileToolStripMenuItem->Name = L"fileToolStripMenuItem";
			this->fileToolStripMenuItem->Size = System::Drawing::Size(37, 20);
			this->fileToolStripMenuItem->Text = L"File";
			// 
			// newToolStripMenuItem
			// 
			this->newToolStripMenuItem->Name = L"newToolStripMenuItem";
			this->newToolStripMenuItem->Size = System::Drawing::Size(121, 22);
			this->newToolStripMenuItem->Text = L"New";
			this->newToolStripMenuItem->Click += gcnew System::EventHandler(this, &MainForm::newToolStripMenuItem_Click);
			// 
			// openToolStripMenuItem
			// 
			this->openToolStripMenuItem->Name = L"openToolStripMenuItem";
			this->openToolStripMenuItem->Size = System::Drawing::Size(121, 22);
			this->openToolStripMenuItem->Text = L"Open...";
			this->openToolStripMenuItem->Click += gcnew System::EventHandler(this, &MainForm::openToolStripMenuItem_Click);
			// 
			// saveToolStripMenuItem
			// 
			this->saveToolStripMenuItem->Name = L"saveToolStripMenuItem";
			this->saveToolStripMenuItem->Size = System::Drawing::Size(121, 22);
			this->saveToolStripMenuItem->Text = L"Save...";
			this->saveToolStripMenuItem->Click += gcnew System::EventHandler(this, &MainForm::saveToolStripMenuItem_Click);
			// 
			// saveAsToolStripMenuItem
			// 
			this->saveAsToolStripMenuItem->Name = L"saveAsToolStripMenuItem";
			this->saveAsToolStripMenuItem->Size = System::Drawing::Size(121, 22);
			this->saveAsToolStripMenuItem->Text = L"Save as...";
			this->saveAsToolStripMenuItem->Click += gcnew System::EventHandler(this, &MainForm::saveAsToolStripMenuItem_Click);
			// 
			// toolStripSeparator1
			// 
			this->toolStripSeparator1->Name = L"toolStripSeparator1";
			this->toolStripSeparator1->Size = System::Drawing::Size(118, 6);
			// 
			// exitToolStripMenuItem
			// 
			this->exitToolStripMenuItem->Name = L"exitToolStripMenuItem";
			this->exitToolStripMenuItem->Size = System::Drawing::Size(121, 22);
			this->exitToolStripMenuItem->Text = L"Exit";
			this->exitToolStripMenuItem->Click += gcnew System::EventHandler(this, &MainForm::exitToolStripMenuItem_Click);
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
			this->labelToolStripMenuItem->Click += gcnew System::EventHandler(this, &MainForm::widgetToolStripMenuItem_Click);
			// 
			// buttonToolStripMenuItem
			// 
			this->buttonToolStripMenuItem->Name = L"buttonToolStripMenuItem";
			this->buttonToolStripMenuItem->Size = System::Drawing::Size(139, 22);
			this->buttonToolStripMenuItem->Text = L"Button";
			this->buttonToolStripMenuItem->Click += gcnew System::EventHandler(this, &MainForm::buttonToolStripMenuItem_Click);
			// 
			// progressBarToolStripMenuItem
			// 
			this->progressBarToolStripMenuItem->Name = L"progressBarToolStripMenuItem";
			this->progressBarToolStripMenuItem->Size = System::Drawing::Size(139, 22);
			this->progressBarToolStripMenuItem->Text = L"Progress Bar";
			this->progressBarToolStripMenuItem->Click += gcnew System::EventHandler(this, &MainForm::progressBarToolStripMenuItem_Click);
			// 
			// widgetToolStripMenuItem
			// 
			this->widgetToolStripMenuItem->Name = L"widgetToolStripMenuItem";
			this->widgetToolStripMenuItem->Size = System::Drawing::Size(139, 22);
			this->widgetToolStripMenuItem->Text = L"Label";
			this->widgetToolStripMenuItem->Click += gcnew System::EventHandler(this, &MainForm::labelToolStripMenuItem_Click);
			// 
			// treeView
			// 
			this->treeView->AllowDrop = true;
			this->treeView->Anchor = static_cast<System::Windows::Forms::AnchorStyles>(((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->treeView->Location = System::Drawing::Point(3, 61);
			this->treeView->Name = L"treeView";
			this->treeView->Size = System::Drawing::Size(274, 237);
			this->treeView->TabIndex = 3;
			this->treeView->ItemDrag += gcnew System::Windows::Forms::ItemDragEventHandler(this, &MainForm::treeView_ItemDrag);
			this->treeView->NodeMouseClick += gcnew System::Windows::Forms::TreeNodeMouseClickEventHandler(this, &MainForm::NodeClick);
			this->treeView->DragDrop += gcnew System::Windows::Forms::DragEventHandler(this, &MainForm::treeView_DragDrop);
			this->treeView->DragEnter += gcnew System::Windows::Forms::DragEventHandler(this, &MainForm::treeView_DragEnter);
			this->treeView->KeyDown += gcnew System::Windows::Forms::KeyEventHandler(this, &MainForm::treeView_KeyDown);
			this->treeView->KeyPress += gcnew System::Windows::Forms::KeyPressEventHandler(this, &MainForm::treeView_KeyPress);
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
			this->propertyGrid->Size = System::Drawing::Size(274, 321);
			this->propertyGrid->TabIndex = 6;
			this->propertyGrid->PropertyValueChanged += gcnew System::Windows::Forms::PropertyValueChangedEventHandler(this, &MainForm::propertyGrid_PropertyValueChanged);
			this->propertyGrid->Click += gcnew System::EventHandler(this, &MainForm::propertyGrid_Click);
			this->propertyGrid->Paint += gcnew System::Windows::Forms::PaintEventHandler(this, &MainForm::propertyGrid_Paint);
			this->propertyGrid->Leave += gcnew System::EventHandler(this, &MainForm::propertyGrid_Click);
			this->propertyGrid->MouseClick += gcnew System::Windows::Forms::MouseEventHandler(this, &MainForm::propertyGrid_MouseClick);
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
			this->MainTab->Size = System::Drawing::Size(886, 641);
			this->MainTab->TabIndex = 7;
			// 
			// Design
			// 
			this->Design->Controls->Add(this->preview);
			this->Design->Location = System::Drawing::Point(4, 22);
			this->Design->Name = L"Design";
			this->Design->Padding = System::Windows::Forms::Padding(3);
			this->Design->Size = System::Drawing::Size(878, 615);
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
			this->preview->Size = System::Drawing::Size(878, 619);
			this->preview->TabIndex = 0;
			// 
			// Code
			// 
			this->Code->Controls->Add(this->textEditor);
			this->Code->Location = System::Drawing::Point(4, 22);
			this->Code->Name = L"Code";
			this->Code->Padding = System::Windows::Forms::Padding(3);
			this->Code->Size = System::Drawing::Size(863, 571);
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
			this->textEditor->Size = System::Drawing::Size(863, 573);
			this->textEditor->TabIndex = 0;
			this->textEditor->TextChanged += gcnew System::EventHandler(this, &MainForm::textEditor_TextChanged);
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
			this->splitContainer1->Panel2->Controls->Add(this->button1);
			this->splitContainer1->Panel2->Controls->Add(this->label3);
			this->splitContainer1->Panel2->Controls->Add(this->rootFolder);
			this->splitContainer1->Panel2->Controls->Add(this->label1);
			this->splitContainer1->Panel2->Controls->Add(this->uiName);
			this->splitContainer1->Panel2->Controls->Add(this->treeView);
			this->splitContainer1->Panel2->Controls->Add(this->propertyGrid);
			this->splitContainer1->Panel2->Controls->Add(this->label2);
			this->splitContainer1->Size = System::Drawing::Size(1176, 644);
			this->splitContainer1->SplitterDistance = 892;
			this->splitContainer1->TabIndex = 8;
			// 
			// button1
			// 
			this->button1->Anchor = static_cast<System::Windows::Forms::AnchorStyles>((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Right));
			this->button1->Location = System::Drawing::Point(245, 9);
			this->button1->Name = L"button1";
			this->button1->Size = System::Drawing::Size(26, 20);
			this->button1->TabIndex = 11;
			this->button1->Text = L"...";
			this->button1->UseVisualStyleBackColor = true;
			this->button1->Click += gcnew System::EventHandler(this, &MainForm::button1_Click);
			// 
			// label3
			// 
			this->label3->Location = System::Drawing::Point(6, 9);
			this->label3->Name = L"label3";
			this->label3->Size = System::Drawing::Size(63, 18);
			this->label3->TabIndex = 10;
			this->label3->Text = L"Root:";
			this->label3->TextAlign = System::Drawing::ContentAlignment::MiddleLeft;
			// 
			// rootFolder
			// 
			this->rootFolder->Anchor = static_cast<System::Windows::Forms::AnchorStyles>(((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->rootFolder->Location = System::Drawing::Point(75, 9);
			this->rootFolder->Name = L"rootFolder";
			this->rootFolder->ReadOnly = true;
			this->rootFolder->Size = System::Drawing::Size(164, 20);
			this->rootFolder->TabIndex = 9;
			this->rootFolder->TextChanged += gcnew System::EventHandler(this, &MainForm::rootFolder_TextChanged);
			// 
			// label1
			// 
			this->label1->Location = System::Drawing::Point(6, 35);
			this->label1->Name = L"label1";
			this->label1->Size = System::Drawing::Size(63, 18);
			this->label1->TabIndex = 8;
			this->label1->Text = L"UI Name:";
			this->label1->TextAlign = System::Drawing::ContentAlignment::MiddleLeft;
			// 
			// uiName
			// 
			this->uiName->Anchor = static_cast<System::Windows::Forms::AnchorStyles>(((System::Windows::Forms::AnchorStyles::Top | System::Windows::Forms::AnchorStyles::Left)
				| System::Windows::Forms::AnchorStyles::Right));
			this->uiName->Location = System::Drawing::Point(75, 35);
			this->uiName->Name = L"uiName";
			this->uiName->Size = System::Drawing::Size(202, 20);
			this->uiName->TabIndex = 7;
			this->uiName->Text = L"ui_1";
			// 
			// MainForm
			// 
			this->AutoScaleDimensions = System::Drawing::SizeF(6, 13);
			this->AutoScaleMode = System::Windows::Forms::AutoScaleMode::Font;
			this->ClientSize = System::Drawing::Size(1179, 675);
			this->Controls->Add(this->splitContainer1);
			this->Controls->Add(this->menuStrip1);
			this->Icon = (cli::safe_cast<System::Drawing::Icon^>(resources->GetObject(L"$this.Icon")));
			this->MainMenuStrip = this->menuStrip1;
			this->Name = L"MainForm";
			this->Text = L"Ui Designer";
			this->Load += gcnew System::EventHandler(this, &MainForm::MainForm_Load);
			this->menuStrip1->ResumeLayout(false);
			this->menuStrip1->PerformLayout();
			this->MainTab->ResumeLayout(false);
			this->Design->ResumeLayout(false);
			this->Code->ResumeLayout(false);
			this->Code->PerformLayout();
			this->splitContainer1->Panel1->ResumeLayout(false);
			this->splitContainer1->Panel2->ResumeLayout(false);
			this->splitContainer1->Panel2->PerformLayout();
			(cli::safe_cast<System::ComponentModel::ISupportInitialize^>(this->splitContainer1))->EndInit();
			this->splitContainer1->ResumeLayout(false);
			this->ResumeLayout(false);
			this->PerformLayout();

		}
#pragma endregion
	public:
		virtual void OnNameChanged(String^ old_name, String^ new_name) sealed {
			TreeNode^ node = GetNode(treeView->Nodes, old_name);
			if (node != nullptr) {
				node->Text = new_name;
			}
		}

	private:
		System::Void MainForm_Load(System::Object^ sender, System::EventArgs^ e) {
		}

		Widget* GetWidget(System::String^ name) {
			for each (IntPtr p in widgets) {
				Widget* w = (Widget*)p.ToPointer();
				if (String::Compare(gcnew System::String(w->props["name"]->GetValue<std::string>().c_str()), name) == 0) {
					return w;
				}
			}
			return nullptr;
		}

		System::Void labelToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
			Widget* widget = new ::Label(this);
			char name[32];
			for (int n = widgets->Count + 1;; ++n) {
				snprintf(name, 32, "label %d", n);
				if (GetWidget(gcnew System::String(name)) == nullptr) {
					break;
				}
			}
			widget->props["name"]->SetValue<std::string>(name);
			IntPtr ptr(widget);
			widgets->Add(ptr);
			AddTreeNode(treeView->Nodes, widget);
			UpdateEditor();
		}

		System::Void progressBarToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
			Widget* widget = new ::ProgressBar(this);
			char name[32];
			for (int n = widgets->Count + 1;; ++n) {
				snprintf(name, 32, "progress_bar %d", n);
				if (GetWidget(gcnew System::String(name)) == nullptr) {
					break;
				}
			}
			widget->props["name"]->SetValue<std::string>(name);
			IntPtr ptr(widget);
			widgets->Add(ptr);
			AddTreeNode(treeView->Nodes, widget);
			UpdateEditor();
		}

		System::Void buttonToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
			Widget* widget = new ::Button(this);
			char name[32];
			for (int n = widgets->Count + 1;; ++n) {
				snprintf(name, 32, "button %d", n);
				if (GetWidget(gcnew System::String(name)) == nullptr) {
					break;
				}
			}
			widget->props["name"]->SetValue<std::string>(name);
			IntPtr ptr(widget);
			widgets->Add(ptr);
			AddTreeNode(treeView->Nodes, widget);
			UpdateEditor();
		}

		System::Void widgetToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
			Widget* widget = new Widget(this);
			char name[32];
			for (int n = widgets->Count + 1;; ++n) {
				snprintf(name, 32, "widget %d", n);
				if (GetWidget(gcnew System::String(name)) == nullptr) {
					break;
				}
			}
			widget->props["name"]->SetValue<std::string>(name);
			IntPtr ptr(widget);
			widgets->Add(ptr);
			AddTreeNode(treeView->Nodes, widget);
			UpdateEditor();
		}

		json GetJson() {
			json js;
			js["ui"]["name"] = msclr::interop::marshal_as<std::string>(uiName->Text);
			js["ui"]["root"] = msclr::interop::marshal_as<std::string>(rootFolder->Text);
			std::vector<json> widgets_list;
			for each (IntPtr ptr in widgets) {
				Widget* widget = (Widget*)ptr.ToPointer();
				widgets_list.push_back(widget->ToJson());
			}
			js["ui"]["widgets"] = widgets_list;
			auto ui_text = gcnew System::String(js.dump(4).c_str());
			return js;
		}

		void AddTreeNode(TreeNodeCollection^ nodes, Widget* widget) {
			TreeNode^ n = nodes->Add(gcnew System::String(widget->props["name"]->GetValue<std::string>().c_str()));
			for (const auto& w2 : widget->widgets) {
				Widget* pw = GetWidget(gcnew System::String(w2.c_str()));
				if (pw != nullptr) {
					AddTreeNode(n->Nodes, pw);
				}
			}
		}

		void ClearParents() {
			for each (IntPtr p in widgets) {
				Widget* w = (Widget*)p.ToPointer();
				w->props["parent"]->SetValue<std::string>("");
				w->widgets.clear();
			}
		}

		TreeNode^ GetNode(TreeNodeCollection^ nodes, String^ name) {
			for each (TreeNode ^ n in nodes) {
				if (String::Compare(n->Text, name) == 0) {
					return n;
				}
				else {
					return GetNode(n->Nodes, name);
				}
			}
			return nullptr;
		}

		void UpdateEditor() {
			this->Text = gcnew System::String("Ui Designer - ");
			if (fileName != nullptr) {
				this->Text += fileName;
			}
			else {
				this->Text += "New file";
			}
			ClearParents();
			widgets_tree->Clear();
			if (treeView->Nodes->Count == 0) {
				for each (IntPtr ptr in widgets) {
					Widget* widget = (Widget*)ptr.ToPointer();
					auto parent = widget->GetString("parent");
					if (parent.has_value()) {
						AddTreeNode(treeView->Nodes, widget);
					}
				}
			}
			for each (TreeNode ^ n in treeView->Nodes) {
				Widget* w = GetWidget(n->Text);
				if (w != nullptr) {
					widgets_tree->Add(IntPtr(w));
					TraverseTree(n);
				}
			}
			treeView->Nodes->Clear();
			for each (IntPtr ptr in widgets_tree) {
				Widget* widget = (Widget*)ptr.ToPointer();
				AddTreeNode(treeView->Nodes, widget);
			}
			json js = GetJson();
			auto ui_text = gcnew System::String(js.dump(4).c_str());
			textEditor->Text = ui_text->Replace("\n", "\r\n");
			HotBiteTool::ToolUi::LoadUI(js);
		}

		System::Void NodeClick(System::Object^ sender, System::Windows::Forms::TreeNodeMouseClickEventArgs^ e) {
			Widget* widget = nullptr;
			for each (IntPtr ptr in widgets) {
				Widget* widget = (Widget*)ptr.ToPointer();
				if (String::Compare(e->Node->Text, gcnew String(widget->props["name"]->GetValue<std::string>().c_str())) == 0) {
					std::string type = widget->props["type"]->GetValue<std::string>();
					WidgetProp^ prop;
					if (type == "widget") {
						prop = gcnew WidgetProp(ptr, rootFolder->Text);
					}
					else if (type == "label") {
						prop = gcnew LabelProp(ptr, rootFolder->Text);
					}
					else if (type == "button") {
						prop = gcnew ButtonProp(ptr, rootFolder->Text);
					}
					else if (type == "progress") {
						prop = gcnew ProgressBarProp(ptr, rootFolder->Text);
					}
					propertyGrid->SelectedObject = prop;
					break;
				}
			}

		}
		System::Void propertyGrid_PropertyValueChanged(System::Object^ s, System::Windows::Forms::PropertyValueChangedEventArgs^ e) {
			UpdateEditor();
		}
		System::Void textEditor_TextChanged(System::Object^ sender, System::EventArgs^ e) {
		}
		System::Void propertyGrid_Click(System::Object^ sender, System::EventArgs^ e) {
		}
		System::Void propertyGrid_MouseClick(System::Object^ sender, System::Windows::Forms::MouseEventArgs^ e) {
		}
		System::Void propertyGrid_Paint(System::Object^ sender, System::Windows::Forms::PaintEventArgs^ e) {
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

		void Reset() {
			propertyGrid->SelectedObject = nullptr;
			for each (IntPtr ptr in widgets) {
				Widget* widget = (Widget*)ptr.ToPointer();
				delete widget;
			}
			treeView->Nodes->Clear();
			widgets->Clear();
			fileName = nullptr;
			UpdateEditor();
		}

		System::String^ fileName = nullptr;

		System::Void openToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
			OpenFileDialog^ openFileDialog = gcnew OpenFileDialog();

			openFileDialog->Filter = "UI Files (*.ui)|*.ui";
			openFileDialog->FilterIndex = 1;
			openFileDialog->RestoreDirectory = true;
			isLoading = true;
			if (openFileDialog->ShowDialog() == System::Windows::Forms::DialogResult::OK) {
				try
				{
					Reset();
					std::string strFileName = msclr::interop::marshal_as<std::string>(openFileDialog->FileName);
					std::ifstream inputFile(strFileName);
					if (inputFile.is_open()) {
						std::string content((std::istreambuf_iterator<char>(inputFile)),
							std::istreambuf_iterator<char>());
						json js = json::parse(content);
						auto& ui = js["ui"];
						uiName->Text = gcnew System::String(((std::string)ui["name"]).c_str());
						rootFolder->Text = gcnew System::String(((std::string)ui["root"]).c_str());
						for (const auto& widget : ui["widgets"]) {
							Widget* w = nullptr;
							if (widget["type"] == "widget") {
								w = new Widget(this);
							}
							else if (widget["type"] == "label") {
								w = new ::Label(this);
							}
							else if (widget["type"] == "button") {
								w = new ::Button(this);
							}
							else if (widget["type"] == "progress") {
								w = new ::ProgressBar(this);
							}
							if (w != nullptr) {
								if (!w->FromJson(widget)) {
									delete w;
									w = nullptr;
								}
								IntPtr ptr(w);
								widgets->Add(ptr);
							}
						}
						for each (IntPtr p in widgets) {
							Widget* w = (Widget*)p.ToPointer();
							auto parent = w->GetString("parent");
							if (!parent.has_value() || parent.value().empty()) {
								AddTreeNode(treeView->Nodes, w);
							}
						}
						inputFile.close();
						fileName = openFileDialog->FileName;
						UpdateEditor();
					}
				}
				catch (...) {
					MessageBox::Show("Error loading file");
				}
			}
			isLoading = false;
		}

		System::Void newToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
			if (MessageBox::Show("Discard current UI?", "Confirmation", MessageBoxButtons::YesNo, MessageBoxIcon::Question) == System::Windows::Forms::DialogResult::Yes) {
				// User clicked Yes, perform the action
				Reset();
			}
			else {
				// User clicked No, do nothing or handle accordingly
			}
		}
		System::Void saveAsToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
			SaveFileDialog^ saveFileDialog = gcnew SaveFileDialog();

			saveFileDialog->Filter = "UI Files (*.ui)|*.ui";
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
		System::Void treeView_KeyPress(System::Object^ sender, System::Windows::Forms::KeyPressEventArgs^ e) {

		}
		System::Void treeView_KeyDown(System::Object^ sender, System::Windows::Forms::KeyEventArgs^ e) {
			if (e->KeyCode == Keys::Delete) {
				TreeView^ treeView = dynamic_cast<TreeView^>(sender);
				if (treeView != nullptr) {
					TreeNode^ selectedNode = treeView->SelectedNode;
					if (selectedNode != nullptr) {
						for each (IntPtr ptr in widgets) {
							Widget* widget = (Widget*)ptr.ToPointer();
							if (String::Compare(selectedNode->Text, gcnew String(widget->props["name"]->GetValue<std::string>().c_str())) == 0) {
								widgets->Remove(ptr);
								propertyGrid->SelectedObject = nullptr;
								UpdateEditor();
								break;
							}
						}
					}
				}
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
		System::Void rootFolder_TextChanged(System::Object^ sender, System::EventArgs^ e) {
			UpdateEditor();
		}
		System::Void exitToolStripMenuItem_Click(System::Object^ sender, System::EventArgs^ e) {
			Application::Exit();
		}
		System::Void treeView_DragEnter(System::Object^ sender, System::Windows::Forms::DragEventArgs^ e) {
			if (e->Data->GetDataPresent(TreeNode::typeid)) {
				e->Effect = DragDropEffects::Move;
			}
			else {
				e->Effect = DragDropEffects::None;
			}
		}


		void TraverseTree(TreeNode^ node)
		{
			Widget* w = GetWidget(node->Text);
			if (w != nullptr) {
				if (!isLoading) {
					w->widgets.clear();
				}
				for each (TreeNode ^ child in node->Nodes)
				{
					Widget* w2 = GetWidget(child->Text);
					if (w2 != nullptr) {
						w->widgets.push_back(w2->GetString("name").value());
						w2->props["parent"]->SetValue<std::string>(w->GetString("name").value());
						TraverseTree(child);
					}
				}
			}
		}

		System::Void treeView_ItemDrag(System::Object^ sender, System::Windows::Forms::ItemDragEventArgs^ e) {
			// Move the dragged node when the left mouse button is used.  
			if (e->Button == System::Windows::Forms::MouseButtons::Left)
			{
				DoDragDrop(e->Item, DragDropEffects::Move);
				UpdateEditor();
				Refresh();
			}
		}

		System::Void treeView_DragDrop(System::Object^ sender, System::Windows::Forms::DragEventArgs^ e) {
			TreeNode^ draggedNode = dynamic_cast<TreeNode^>(e->Data->GetData(TreeNode::typeid));

			if (draggedNode != nullptr) {
				Point targetPoint = treeView->PointToClient(Point(e->X, e->Y));
				TreeNode^ targetNode = treeView->GetNodeAt(targetPoint);
				draggedNode->Remove();
				if (targetNode != nullptr) {
					targetNode->Nodes->Add(draggedNode);
				}
				else {
					treeView->Nodes->Add(draggedNode);
				}
				UpdateEditor();
			}
		}
	};
}
