// Stubs Switch — símbolos Free winuser / NewApp / touch V2.

#include <switch.h>
#include <cstdio>

// Aplicação (título com R) recebe ~3.2 GB; applet do Álbum só ~440 MB. Só manter
// texturas/malhas residentes quando a memória do processo comporta o client inteiro.
extern "C" int WYD_Switch_KeepAssetsResident(void)
{
	static int s_resident = -1;
	if (s_resident < 0)
	{
		u64 total = 0;
		svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
		s_resident = total >= (1536ull << 20) ? 1 : 0;
		if (FILE* f = fopen("sdmc:/switch/client748/wyd748_diag.txt", "ab"))
		{
			fprintf(f, "[MEM] processo=%llu MB applet=%d residente=%d\n",
				(unsigned long long)(total >> 20), (int)appletGetAppletType(), s_resident);
			fclose(f);
		}
	}
	return s_resident;
}

// 1 = rodando pelo Álbum / applet (pouca RAM); 0 = Application (title override).
extern "C" int WYD_Switch_LowMemApplet(void)
{
	return appletGetAppletType() != AppletType_Application ? 1 : 0;
}

extern "C" void WYD_Linux_SetBackbuffer(int, int) {}

extern "C" void WYD_Linux_SetCompositionText(const char*) {}

extern "C" int WYD_TouchV2_Enabled() { return 0; }

extern "C" int WYD_TouchV2_HasCapture() { return 0; }

extern "C" int WYD_TouchV2_WantMouseSync() { return 0; }

extern "C" int WYD_TouchV2_Pointer(int, int, int, int, int, int) { return 0; }

extern "C" int WYD_TouchV2_Finger(int, float, float, int, int, int, int, int)
{
	return 0;
}

extern "C" void WYD_TouchV2_ForceEnable(int) {}

extern "C" void WYD_TouchV2_Pad(float, float, float, float, int, int, int) {}

extern "C" int WYD_TouchV2_PointerWindow(int, int, int, int, int, int, int, int)
{
	return 0;
}
