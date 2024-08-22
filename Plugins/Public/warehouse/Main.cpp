// Warehouse Plugin by Aingar. Lets you store items on designated bases for pickup.
// 
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

#include <windows.h>
#include <list>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>

PLUGIN_RETURNCODE returncode;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		HkLoadStringDLLs();
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
		HkUnloadStringDLLs();
	}
	return true;
}

EXPORT PLUGIN_RETURNCODE Get_PluginReturnCode()
{
	return returncode;
}

bool bPluginEnabled = true;


bool UserCmd_Login(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	return true;
}
bool UserCmd_Deposit(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	return true;
}
bool UserCmd_Withdraw(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	return true;
}
bool UserCmd_List(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	return true;
}

typedef bool(*_UserCmdProc)(uint, const wstring&, const wstring&, const wchar_t*);

struct USERCMD
{
	wchar_t* wszCmd;
	_UserCmdProc proc;
	wchar_t* usage;
};

USERCMD UserCmds[] =
{
	{ L"/wl", UserCmd_Login, L"Usage: /warehouselogin or /wl <login> <password>" },
	{ L"/warehouselogin", UserCmd_Login, L"Usage: /warehouselogin or /wl <login> <password>" },
	{ L"/wlist", UserCmd_List, L"Usage: /wlist or /warehouselist" },
	{ L"/warehouselist", UserCmd_List, L"Usage: /wlist or /warehouselist" },
	{ L"/deposit", UserCmd_Deposit, L"Usage: /deposit <itemNr> [amount]" },
	{ L"/withdraw", UserCmd_Withdraw, L"Usage: /withdraw <itemNr> [amount]" },
};

bool UserCmd_Process(uint iClientID, const wstring& wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	wstring wscCmdLineLower = ToLower(wscCmd);

	for (uint i = 0; (i < sizeof(UserCmds) / sizeof(USERCMD)); i++)
	{

		if (wscCmdLineLower.find(UserCmds[i].wszCmd) == 0)
		{
			wstring wscParam = L"";
			if (wscCmd.length() > wcslen(UserCmds[i].wszCmd))
			{
				if (wscCmd[wcslen(UserCmds[i].wszCmd)] != ' ')
					continue;
				wscParam = wscCmd.substr(wcslen(UserCmds[i].wszCmd) + 1);
			}

			if (UserCmds[i].proc(iClientID, wscCmd, wscParam, UserCmds[i].usage))
			{
				returncode = SKIPPLUGINS_NOFUNCTIONCALL;
				return true;
			}
		}
	}
	return false;
}

void PluginComm(PLUGIN_MESSAGE msg, void* data)
{

}

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Warehouse by Aingar";
	p_PI->sShortName = "warehouse";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PluginComm, PLUGIN_Plugin_Communication, 0));


	return p_PI;
}
