#include "MainForm.h"
#include "HotBiteTool.h"

using namespace System;
using namespace System::Windows::Forms;

[STAThread]
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	auto form = gcnew UiDesigner::MainForm();	
	form->Show();

	HotBiteTool::ToolUi::Run();
	return 0;
}