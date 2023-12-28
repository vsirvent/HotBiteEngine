#include <Windows.h>
#include "MaterialDesignerForm.h"
#include "HotBiteTool.h"

#pragma comment(lib, "Engine.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

using namespace System;
using namespace System::Windows::Forms;

[STAThread]
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{

	Application::EnableVisualStyles();
	Application::SetCompatibleTextRenderingDefault(false);

	auto form = gcnew MaterialDesigner::MaterialDesignerForm();
	form->Show();

	HotBiteTool::ToolUi::Run();
	return 0;
}
