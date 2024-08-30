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

vector<wstring> welcomeInfo;

struct PlayerInfo
{
	uint lastPopupPage = 0;
};

PlayerInfo clientData[MAX_CLIENT_ID + 1];

enum buttons
{
	BUTTONS_LEFT_YES = 1,
	BUTTONS_CENTER_NO = 2,
	BUTTONS_RIGHT_LATER = 4,
	BUTTONS_CENTER_OK = 8,
	BUTTONS_FULL_SET = BUTTONS_LEFT_YES | BUTTONS_CENTER_NO | BUTTONS_RIGHT_LATER,
};

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

void DisplayWelcomeWindow(uint client, uint page)
{
	uint renderedButtons = buttons::BUTTONS_CENTER_NO;
	
	HkChangeIDSString(client, 1245, L"CLOSE");
	if (page > 1)
	{
		HkChangeIDSString(client, 1244, L"PREV PAGE");
		renderedButtons |= buttons::BUTTONS_LEFT_YES;
	}
	if (page < welcomeInfo.size())
	{
		HkChangeIDSString(client, 1570, L"NEXT PAGE");
		renderedButtons |= buttons::BUTTONS_RIGHT_LATER;
	}

	wchar_t buf[100];
	_snwprintf(buf, sizeof(buf), L"Welcome! : Page %d/%d", page, welcomeInfo.size());
	wstring title = buf;

	HkChangeIDSString(client, 500000, title);
	HkChangeIDSString(client, 500001, welcomeInfo[page-1]);

	FmtStr caption(0, 0);
	caption.begin_mad_lib(500000);
	caption.end_mad_lib();

	FmtStr message(0, 0);
	message.begin_mad_lib(500001);
	message.end_mad_lib();

	clientData[client].lastPopupPage = page;

	pub::Player::PopUpDialog(client, caption, message, renderedButtons);
}

bool UserCmd_Rules(uint client, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
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
	{ L"/rules", UserCmd_Rules, L"Usage: /rules [page]" }
};

bool UserCmd_Process(uint client, const wstring& wscCmd)
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

			if (UserCmds[i].proc(client, wscCmd, wscParam, UserCmds[i].usage))
			{
				returncode = SKIPPLUGINS_NOFUNCTIONCALL;
				return true;
			}
		}
	}
	return false;
}

void LoadSettings()
{
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string currDir = szCurDir;
	string newPlayerConfig = currDir + R"(\flhook_plugins\newplayer.cfg)";

	INI_Reader ini;
	if (ini.open(newPlayerConfig.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("NewPlayer")) {
				while (ini.read_value())
				{
					if (ini.is_value("WindowInfo"))
					{
						welcomeInfo.emplace_back(stows(ini.get_value_string()));
					}
				}
			}
		}

		ini.close();
	}
}

void PluginComm(PLUGIN_MESSAGE msg, void* data)
{
	returncode = DEFAULT_RETURNCODE;
	if (msg == CUSTOM_POPUP_INIT)
	{
		uint* clientId = reinterpret_cast<uint*>(data);
		clientData[*clientId].lastPopupPage = 0;
	}
}

void PopUpDialogue(uint client, uint buttonPressed)
{
	returncode = DEFAULT_RETURNCODE;
	ConPrint(L"%u\n", buttonPressed);
	if (!clientData[client].lastPopupPage)
	{
		return;
	}

	if (buttonPressed == buttons::BUTTONS_CENTER_NO)
	{
		HkChangeIDSString(client, 1244, L"YES");
		HkChangeIDSString(client, 1245, L"NO");
	}
	else if (buttonPressed == buttons::BUTTONS_LEFT_YES)
	{
		clientData[client].lastPopupPage--;
		DisplayWelcomeWindow(client, clientData[client].lastPopupPage);
	}
	else if (buttonPressed == buttons::BUTTONS_RIGHT_LATER)
	{
		clientData[client].lastPopupPage++;
		DisplayWelcomeWindow(client, clientData[client].lastPopupPage);
	}
}

void __stdcall CharacterSelect(struct CHARACTER_ID const& cId, unsigned int client)
{
	if (Players[client].characterMap.size() > 1)
	{
		return;
	}

	auto mdata = mdataPlayerMap->find(client);
	float timePlayed = (*mdata.value())->totalTimePlayed;

	if (timePlayed > 5)
	{
		return;
	}

	Plugin_Communication(PLUGIN_MESSAGE::CUSTOM_POPUP_INIT, &client);
	DisplayWelcomeWindow(client, 1);
}

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "New Player Plugin by Aingar";
	p_PI->sShortName = "newplayer";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect, PLUGIN_HkIServerImpl_CharacterSelect_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PopUpDialogue, PLUGIN_HKIServerImpl_PopUpDialog, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PluginComm, PLUGIN_Plugin_Communication, 0));


	return p_PI;
}
