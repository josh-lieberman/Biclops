// Biclops 0.1 by Emerson - 6/2012
// Based heavily on Azorbix's D3D Starter Kit for interface implementations,
// I've left his original headers below for credits.


/************************************************\
**	Project : TatniumD3D (d3d9 hook) v2.1		**
**	Author	: Matthew L (Azorbix)				**
**	Date	: September 29, 2004				**
\************************************************/
/*******************************************************\
*	Credits:											*
*		Tetsuo:			Help with hooking the device	*
*		LanceVorgin:	His name is funny				*
*		p47r!ck:		Hes German						*
*		Hybrid:			Knows DirectX9					*
*		Dom1n1k:		Helped with detourfunc			*
*		Da_BuzZ:		Helped alot with d3d8			*
*														*
*	Tools used:											*
*		MSVC++ 6.0 (w/out CORE-SDK)						*
*		DirectX9 SDK Update (summer 2003)				*
\*******************************************************/
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1
#include <windows.h>
#include <fstream>
#include <ostream>
#include <iostream>
#include <stdio.h>

#include "main.h"
#include "d3d9.h"

//Globals
using namespace std;
std::ofstream ofile;	
char dlldir[320];

bool WINAPI DllMain(HMODULE hDll, DWORD dwReason, PVOID pvReserved)
{
	if(dwReason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hDll);
		GetModuleFileName(hDll, dlldir, 512);
		for(int i = strlen(dlldir); i > 0; i--) { if(dlldir[i] == '\\') { dlldir[i+1] = 0; break; } }
		ofile.open(GetDirectoryFile("d3dlog.txt"), ios::app);

		char sysd3d[320];
		GetSystemDirectory(sysd3d, 320);
		strcat(sysd3d, "\\d3d9.dll");
	
		HMODULE hMod = LoadLibrary(sysd3d);		
		oDirect3DCreate9 = (tDirect3DCreate9)GetProcAddress(hMod, "Direct3DCreate9");

		return true;
	}

	else if(dwReason == DLL_PROCESS_DETACH)
	{
		if(ofile) { ofile.close(); }
	}

    return false;
}

char *GetDirectoryFile(char *filename)
{
	static char path[320];
	strcpy(path, dlldir);
	strcat(path, filename);
	return path;
}

void __cdecl add_log (const char *fmt, ...)
{
	if(ofile != NULL)
	{
		if(!fmt) { return; }

		va_list va_alist;
		char logbuf[9999] = {0};

		va_start (va_alist, fmt);
		_vsnprintf_s(logbuf+strlen(logbuf), sizeof(logbuf) - strlen(logbuf), _TRUNCATE, fmt, va_alist);
		va_end (va_alist);

		ofile << logbuf << std::endl;
	}
}
