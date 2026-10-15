#pragma once

#include <Windows.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <cstddef>

namespace HotBite {
	namespace Engine {
		namespace Core {

			// The DirectX Shader Compiler, loaded at run time from dxcompiler.dll (and dxil.dll next to it, which signs
			// what it compiles): shaders are SM6 DXIL, so reflection and the in-process compiler (hot reload, the
			// editors) both come from there. The build compiles the same sources with dxc.exe (Engine/Dxc.targets).
			//
			// Loaded on first use, so a host whose DLLs are missing fails where it asks for them with a clear log line
			// instead of at start-up.
			class DxcSupport {
			public:
				// True once dxcompiler.dll is loaded.
				static bool Available();
				static IDxcUtils* Utils();
				// A new compiler object per compile: they are cheap, and it keeps compiles on worker threads independent.
				static HRESULT CreateCompiler(IDxcCompiler3** out);

				// Reflection of a compiled shader: DXIL goes through DXC, anything older (a DXBC .cso left from before
				// the move, a test fixture) through D3DReflect.
				static HRESULT Reflect(const void* bytecode, size_t size, ID3D12ShaderReflection** out);
			};
		}
	}
}
