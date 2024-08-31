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

enum class WindowID
{
	None = 0,
	NewPlayerWelcome = 1,
	FirstSteps = 2,
	ServerRules = 3,
};

struct WindowData
{
	wstring caption;
	vector<wstring> messages;
};
unordered_map<uint, WindowData> windowData;

struct PlayerInfo
{
	uint lastPopupPage = 0;
	WindowID currPlayerWindow = WindowID::None;
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

void DisplayWelcomeWindow(uint client, uint page, WindowID windowIDParam)
{
	uint windowID = (uint)windowIDParam;
	auto windowIter = windowData.find(windowID);
	if (windowIter == windowData.end())
	{
		PrintUserCmdText(client, L"You were supposed to get a popup window with ID of %u/%u, notify staff", windowID, page);
		return;
	}

	auto& windowInfo = windowIter->second;
	uint renderedButtons = buttons::BUTTONS_CENTER_NO;
	
	HkChangeIDSString(client, 1245, L"CLOSE");
	if (page > 1)
	{
		HkChangeIDSString(client, 1244, L"PREV PAGE");
		renderedButtons |= buttons::BUTTONS_LEFT_YES;
	}
	if (page < windowInfo.messages.size())
	{
		HkChangeIDSString(client, 1570, L"NEXT PAGE");
		renderedButtons |= buttons::BUTTONS_RIGHT_LATER;
	}

	wchar_t buf[100];
	if (windowInfo.messages.size() > 1)
	{
		_snwprintf(buf, sizeof(buf), L"%ls : Page %d/%d", windowInfo.caption.c_str(), page, windowInfo.messages.size());
	}
	else
	{
		_snwprintf(buf, sizeof(buf), L"%ls!", windowInfo.caption.c_str());
	}
	wstring title = buf;

	HkChangeIDSString(client, 500000, title);
	HkChangeIDSString(client, 500001, windowInfo.messages[page-1]);

	FmtStr caption(0, 0);
	caption.begin_mad_lib(500000);
	caption.end_mad_lib();

	FmtStr message(0, 0);
	message.begin_mad_lib(500001);
	message.end_mad_lib();

	clientData[client].lastPopupPage = page;
	clientData[client].currPlayerWindow = windowIDParam;

	pub::Player::PopUpDialog(client, caption, message, renderedButtons);
}

bool UserCmd_ServerRules(uint client, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	uint pageNr = 1;
	if (!wscParam.empty())
	{
		uint paramNr = ToUInt(wscParam);
		if (paramNr)
		{
			pageNr = paramNr;
		}
	}

	Plugin_Communication(PLUGIN_MESSAGE::CUSTOM_POPUP_INIT, &client);
	DisplayWelcomeWindow(client, pageNr, WindowID::ServerRules);
	return true;
}

bool UserCmd_FirstSteps(uint client, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	uint pageNr = 1;
	if (!wscParam.empty())
	{
		uint paramNr = ToUInt(wscParam);
		if (paramNr)
		{
			pageNr = paramNr;
		}
	}

	Plugin_Communication(PLUGIN_MESSAGE::CUSTOM_POPUP_INIT, &client);
	DisplayWelcomeWindow(client, pageNr, WindowID::FirstSteps);
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
	{ L"/server-rules", UserCmd_ServerRules, L"Usage: /rules [page]" },
	{ L"/firststeps", UserCmd_FirstSteps, L"Usage: /firststeps [page]" }
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
	windowData.clear();

	// don't skip semicolons when parsing config files
	//BYTE patch[] = { 0x90,0x90,0x90,0x90,0x90,0x90 };
	//WriteProcMem((char*)0x630F5E0, patch, sizeof(patch));
	BYTE patch[] = { 0x00 };
	WriteProcMem((char*)0x630F5E3, patch, sizeof(patch));

	INI_Reader ini;
	if (ini.open(newPlayerConfig.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("WindowInfo"))
			{
				WindowData* windowEntry;
				wstring infoLine;
				while (ini.read_value())
				{
					if (ini.is_value("id"))
					{
						windowEntry = &windowData[ini.get_value_int(0)];
					}
					if (ini.is_value("Caption"))
					{
						windowEntry->caption = stows(ini.get_value_string());
					}
					else if (ini.is_value("TextLine"))
					{
						wstring tempString = stows(ini.get_value_string());
						bool isNextLine = tempString.at(tempString.size() - 1) == L'X';
						if (isNextLine)
						{
							tempString = tempString.substr(0, tempString.size() - 1);
						}
						infoLine += tempString;
						if(!isNextLine)
						{
							windowEntry->messages.emplace_back(infoLine);
							infoLine.clear();
						}
					}
				}
			}
		}

		ini.close();
	}
	BYTE patch2[] = { 0x3B };
	WriteProcMem((char*)0x630F5E3, patch2, sizeof(patch2));
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
	if (!clientData[client].lastPopupPage)
	{
		return;
	}

	if (buttonPressed == buttons::BUTTONS_CENTER_NO)
	{
		clientData[client].lastPopupPage = 0;
		clientData[client].currPlayerWindow = WindowID::None;
		HkChangeIDSString(client, 1244, L"YES");
		HkChangeIDSString(client, 1245, L"NO");
	}
	else if (buttonPressed == buttons::BUTTONS_LEFT_YES)
	{
		clientData[client].lastPopupPage--;
		DisplayWelcomeWindow(client, clientData[client].lastPopupPage, clientData[client].currPlayerWindow);
	}
	else if (buttonPressed == buttons::BUTTONS_RIGHT_LATER)
	{
		clientData[client].lastPopupPage++;
		DisplayWelcomeWindow(client, clientData[client].lastPopupPage, clientData[client].currPlayerWindow);
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
	DisplayWelcomeWindow(client, 1, WindowID::NewPlayerWelcome);
}

#define IS_CMD(a) !args.compare(L##a)
#define RIGHT_CHECK(a) if(!(cmd->rights & a)) { cmd->Print(L"ERR No permission\n"); return true; }
bool ExecuteCommandString_Callback(CCmds* cmd, const wstring& args)
{
	returncode = DEFAULT_RETURNCODE;
	RIGHT_CHECK(RIGHT_SUPERADMIN)
	if (args.find(L"reloadnewplayer") == 0)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		LoadSettings();
		return true;
	}
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
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));



	return p_PI;
}
