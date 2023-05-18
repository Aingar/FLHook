/**
 * @date July, 2022
 * @author Nekura Mew
 * @defgroup Tax Tax
 * @brief
 * The Tax plugin allows players to issue 'formally' make credit demands and declare hostilities.
 * Ported to DiscoHook by Aingar
 */

#include "Main.h"

uint maxTax = 1'000'000'000;
map<uint, Tax> taxMap;
boolean killDisconnectingPlayers = true;

wstring cannotPayMsg = L"You can't pay, they're out to kill you!";
wstring killMessage = L"%player decided to kill you, run!";
wstring ransomMessage = L"%player issued a demand for %pay credits";

PLUGIN_RETURNCODE returncode;

// Load Settings
void LoadSettings()
{
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scCfgFile = string(szCurDir) + "\\flhook_plugins\\tax.cfg";

	INI_Reader ini;
	if (ini.open(scCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("general"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("maxTax"))
					{
						maxTax = ini.get_value_int(0);
					}
					else if (ini.is_value("killDisconnectingPlayers"))
					{
						killDisconnectingPlayers = ini.get_value_bool(0);
					}
					else if (ini.is_value("cannotPayMessage"))
					{
						cannotPayMsg = stows(ini.get_value_string());
					}
					else if (ini.is_value("killMessage"))
					{
						killMessage = stows(ini.get_value_string());
					}
					else if (ini.is_value("ransomMessage"))
					{
						ransomMessage = stows(ini.get_value_string());
					}
				}
			}
		}
		ini.close();
	}
}

// Functions

bool UserCmdTax(uint client, const wstring& cmd, const wstring &wscParam, const wchar_t* usage)
{
	uint ship = 0;
	pub::Player::GetShip(client, ship);
	if (!ship)
	{
		PrintUserCmdText(client, L"Error: You cannot tax in a No-PvP system.");
		return true;
	}
	// no-pvp check

	uint system = Players[client].iSystemID;
	bool isNoPvpSystem = false;
	for (uint nopvpsys : set_lstNoPVPSystems)
	{
		if (system == nopvpsys)
		{
			isNoPvpSystem = true;
			break;
		}
	}
	if (isNoPvpSystem)
	{
		PrintUserCmdText(client, L"Error: You cannot tax in a No-PvP system.");
		return true;
	}

	const uint taxAmount = ToUInt(GetParam(wscParam, ' ', 0));

	if (taxAmount == 0)
	{
		PrintUserCmdText(client, L"Usage:");
		PrintUserCmdText(client, L"/tax <credits>");
	}

	if (taxAmount > maxTax)
	{
		PrintUserCmdText(client, L"ERR Maximum tax value is %u credits.", maxTax);
		return true;
	}

	uint targetShip = 0;

	pub::SpaceObj::GetTarget(ship, targetShip);

	uint targetPlayer = HkGetClientIDByShip(targetShip);

	if (targetPlayer == 0)
	{
		PrintUserCmdText(client, L"Error: You are not targeting a player.");
		return true;
	}

	if (taxMap.count(targetPlayer))
	{
		PrintUserCmdText(client, L"Error: There already is a tax request pending for this player.");
		return true;
	}

	const auto characterName = (const wchar_t*)Players.GetActiveCharacterName(client);
	const auto targetName = (const wchar_t*)Players.GetActiveCharacterName(targetPlayer);


	Tax tax;
	tax.initiatorId = client;
	tax.targetId = targetPlayer;
	tax.cash = taxAmount;
	taxMap[targetPlayer] = tax;

	// send confirmation msg
	if (taxAmount)
	{
		wstring message = ransomMessage;
		message = ReplaceStr(message, L"%player", characterName);
		message = ReplaceStr(message, L"%pay", stows(itos(tax.cash)).c_str());
		PrintUserCmdText(targetPlayer, message);
		PrintUserCmdText(client, L"Tax request of %u credits sent to %ls!", taxAmount, targetName);
	}
	else
	{
		wstring message = killMessage;
		message = ReplaceStr(message, L"%player", characterName);
		PrintUserCmdText(targetPlayer, message);
		PrintUserCmdText(client, L"The hunt is on");
	}

	return true;
}

bool UserCmdPay(uint client, const wstring& cmd, const wstring &wscParam, const wchar_t* usage)
{
	for (auto& iter : taxMap)
	{
		Tax& it = iter.second;

		if (it.targetId == client)
		{
			if (it.cash == 0)
			{
				PrintUserCmdText(client, cannotPayMsg);
				return true;
			}

			const auto characterName = (const wchar_t*)Players.GetActiveCharacterName(client);
			int cash = 0;
			HkGetCash(characterName, cash);

			if (cash < it.cash)
			{
				PrintUserCmdText(client, L"You have not enough money to pay the tax.");
				PrintUserCmdText(it.initiatorId, L"The target does not have enough money to pay the tax.");
				taxMap.erase(iter.first);
				return true;
			}

			const auto initiatorName = (const wchar_t*)Players.GetActiveCharacterName(it.initiatorId);
			HkAddCash(initiatorName, it.cash);
			HkAddCash(characterName, -it.cash);

			PrintUserCmdText(client, L"You paid the tax.");
			PrintUserCmdText(it.initiatorId, L"%ls paid the %u credit tax!", characterName, it.cash);
			HkSaveChar(client);
			HkSaveChar(it.initiatorId);
			taxMap.erase(iter.first);
			return true;
		}
	}
	PrintUserCmdText(client, L"Error: No tax request was found that could be accepted!");
	return true;
}

void TimerF1Check()
{
	PlayerData* playerData = nullptr;
	while ((playerData = Players.traverse_active(playerData)))
	{
		const uint client = playerData->iOnlineID;

		if (ClientInfo[client].tmF1TimeDisconnect)
			continue;

		if (ClientInfo[client].tmF1Time && (timeInMS() >= ClientInfo[client].tmF1Time)) // f1
		{
			// tax
			for (const auto& iter : taxMap)
			{
				const Tax& it = iter.second;
				if (it.targetId == client)
				{
					if(killDisconnectingPlayers)
					{
						uint ship;
						pub::Player::GetShip(client, ship);
						// F1 -> Kill
						if(ship)
							pub::SpaceObj::SetRelativeHealth(ship, 0.0);
					}
					const auto characterName = (const wchar_t*)Players.GetActiveCharacterName(client);
					PrintUserCmdText(it.initiatorId, L"Tax request to %ls aborted.", characterName);
					taxMap.erase(iter.first);
					break;
				}
			}
		}
		else if (ClientInfo[client].tmF1TimeDisconnect && (timeInMS() >= ClientInfo[client].tmF1TimeDisconnect))
		{
			// tax
			for (const auto& iter : taxMap)
			{
				const Tax& it = iter.second;
				if (it.targetId == client)
				{
					uint ship;
					pub::Player::GetShip(client, ship);
					if(ship)
					{
						// F1 -> Kill
						pub::SpaceObj::SetRelativeHealth(ship, 0.0);
					}
					const auto characterName = (const wchar_t*)Players.GetActiveCharacterName(client);
					PrintUserCmdText(it.initiatorId, L"Tax request to %ls aborted.", characterName);
					taxMap.erase(iter.first);
					break;
				}
			}
		}
	}
}

// Hooks

void DisConnect(const uint& client, const enum EFLConnection& state)
{
	TimerF1Check();
}

typedef bool(*_UserCmdProc)(uint, const wstring &, const wstring &, const wchar_t*);

struct USERCMD
{
	wchar_t *wszCmd;
	_UserCmdProc proc;
	wchar_t *usage;
};

// Client command processing
USERCMD UserCmds[] = {
	{L"/tax", UserCmdTax, L"Demand listed amount from your current target."},
	{L"/pay", UserCmdPay, L"Pays a tax request that has been issued to you."},
};

bool UserCmd_Process(uint iClientID, const wstring &wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	wstring wscCmdLineLower = ToLower(wscCmd);

	// If the chat string does not match the USER_CMD then we do not handle the
	// command, so let other plugins or FLHook kick in. We require an exact match
	for (uint i = 0; (i < sizeof(UserCmds) / sizeof(USERCMD)); i++)
	{

		if (wscCmdLineLower.find(UserCmds[i].wszCmd) == 0)
		{
			// Extract the parameters string from the chat string. It should
			// be immediately after the command and a space.
			wstring wscParam = L"";
			if (wscCmd.length() > wcslen(UserCmds[i].wszCmd))
			{
				if (wscCmd[wcslen(UserCmds[i].wszCmd)] != ' ')
					continue;
				wscParam = wscCmd.substr(wcslen(UserCmds[i].wszCmd) + 1);
			}

			// Dispatch the command to the appropriate processing function.
			if (UserCmds[i].proc(iClientID, wscCmd, wscParam, UserCmds[i].usage))
			{
				// We handled the command tell FL hook to stop processing this
				// chat string.
				returncode = SKIPPLUGINS_NOFUNCTIONCALL; // we handled the command, return immediatly
				return true;
			}
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FLHOOK STUFF
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "tax";
	p_PI->sShortName = "tax";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&TimerF1Check, PLUGIN_HkTimerCheckKick, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&DisConnect, PLUGIN_HkIServerImpl_DisConnect, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));

	return p_PI;
}