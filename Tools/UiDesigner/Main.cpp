#include "MainForm.h"
#include "HotBiteTool.h"

using namespace System;
using namespace System::Windows::Forms;

void main() {
	Application::EnableVisualStyles();
	Application::SetCompatibleTextRenderingDefault(false);

	auto form = gcnew UiDesigner::MainForm();	
	form->Show();

	HotBiteTool::ToolUi::Run();
}