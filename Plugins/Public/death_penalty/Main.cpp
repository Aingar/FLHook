/**
 * @date August, 2022
 * @author Ported from 88Flak by Raikkonen
 * @defgroup DeathPenalty Death Penalty
 * @brief
 * This plugin charges players credits for dying based on their ship worth. If the killer was a player it also rewards them.
 *
 * @paragraph cmds Player Commands
 * All commands are prefixed with '/' unless explicitly specified.
 * - dp - Shows the credits you would be charged if you died.
 *
 * @paragraph adminCmds Admin Commands
 * There are no admin commands in this plugin.
 *
 * @paragraph configuration Configuration
 * @code
 * {
 *     "DeathPenaltyFraction": 1.0,
 *     "DeathPenaltyFractionKiller": 1.0,
 *     "ExcludedSystems": ["li01"],
 *     "FractionOverridesByShip": {{"ge_fighter",1.0}}
 * }
 * @endcode
 *
 * @paragraph ipc IPC Interfaces Exposed
 * This plugin does not expose any functionality.
 *
 * @paragraph optional Optional Plugin Dependencies
 * This plugin has no dependencies.
 */

#include "Main.h"

float DeathPenaltyFraction = 0;
float DeathPenaltyFractionKiller = 0;
map<string, float> FractionOverridesByShip = {};
map<uint, CLIENT_DATA> MapClients;
unordered_set<uint> ExcludedSystemsIds;
unordered_map<uint, float> FractionOverridesByShipIds;

PLUGIN_RETURNCODE returncode;

// Load configuration file
void LoadSettings()
{
	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);

	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\death_penalty.cfg";

	INI_Reader ini;
	if (ini.open(scPluginCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("General"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("death_penalty_fraction"))
					{
						DeathPenaltyFraction = ini.get_value_float(0);
					}
					else if (ini.is_value("death_penalty_fraction_killer"))
					{
						DeathPenaltyFractionKiller = ini.get_value_float(0);
					}
					else if (ini.is_value("excluded_system"))
					{
						ExcludedSystemsIds.insert(CreateID(ini.get_value_string(0)));
					}
					else if (ini.is_value("ship_override"))
					{
						FractionOverridesByShipIds[CreateID(ini.get_value_string(0))] = ini.get_value_float(1);
					}
				}
			}
		}
	}
}

void ClearClientInfo(uint client) { MapClients.erase(client); }

/** @ingroup DeathPenalty
 * @brief Is the player is a system that is excluded from death penalty?
 */
bool bExcludedSystem(uint client)
{
	// Get System Id
	uint iSystemId;
	pub::Player::GetSystem(client, iSystemId);
	// Search list for system
	return (std::find(ExcludedSystemsIds.begin(), ExcludedSystemsIds.end(), iSystemId) != ExcludedSystemsIds.end());
}


/** @ingroup DeathPenalty
 * @brief This returns the override for the specific ship as defined in the json file.
 * If there is not override it returns the default value defined as
 * "DeathPenaltyFraction" in the json file
 */
float fShipFractionOverride(uint client)
{
	// Get ShipArchId
	uint shipArchId;
	pub::Player::GetShipID(client, shipArchId);

	// Default return value is the default death penalty fraction
	float fOverrideValue = DeathPenaltyFraction;

	// See if the ship has an override fraction
	if (FractionOverridesByShipIds.find(shipArchId) != FractionOverridesByShipIds.end())
		fOverrideValue = FractionOverridesByShipIds[shipArchId];

	return fOverrideValue;
}

/** @ingroup DeathPenalty
 * @brief Hook on Player Launch. Used to work out the death penalty and display a message to the player warning them of such
 */
void __stdcall PlayerLaunch(uint ship, uint client)
{
	// No point in processing anything if there is no death penalty
	if (DeathPenaltyFraction > 0.00001f)
	{
		// Check to see if the player is in a system that doesn't have death
		// penalty
		if (!bExcludedSystem(client))
		{
			// Get the players net worth
			float fValue;
			pub::Player::GetAssetValue(client, fValue);

			int cash;
			pub::Player::InspectCash(client, cash);

			int dpCredits = static_cast<int>(fValue * fShipFractionOverride(client));
			if (cash < dpCredits)
				dpCredits = cash;

			// Calculate what the death penalty would be upon death
			MapClients[client].DeathPenaltyCredits = dpCredits;

			// Should we print a death penalty notice?
			if (MapClients[client].bDisplayDPOnLaunch)
				PrintUserCmdText(client, L"Notice: the death penalty for your ship will be " + ToMoneyStr(MapClients[client].DeathPenaltyCredits) +
				        L" credits.  Type /dp for more information.");
		}
		else
			MapClients[client].DeathPenaltyCredits = 0;
	}
}

/** @ingroup DeathPenalty
 * @brief Load settings directly from the player's save directory
 */
void LoadUserCharSettings(uint client)
{
	// Get Account directory then flhookuser.ini file
	CAccount* acc = Players.FindAccountFromClientID(client);
	std::wstring dir;
	HkGetAccountDirName(acc, dir);
	std::string scUserFile = scAcctPath + wstos(dir) + "\\flhookuser.ini";

	wstring charName = (const wchar_t*)Players.GetActiveCharacterName(client);

	// Get char filename and save setting to flhookuser.ini
	wstring fileName;
	HkGetCharFileName(charName, fileName);
	std::string scFilename = wstos(fileName);
	std::string scSection = "general_" + scFilename;

	// read death penalty settings
	CLIENT_DATA cd;
	cd.bDisplayDPOnLaunch = IniGetB(scUserFile, scSection, "DPnotice", true);
	MapClients[client] = cd;
}

/** @ingroup DeathPenalty
 * @brief Apply the death penalty on a player death
 */
void PenalizeDeath(uint client, uint iKillerId)
{
	if (DeathPenaltyFraction < 0.00001f)
		return;

	// Valid client and the ShipArch or System isnt in the excluded list?
	if (client != -1 && !bExcludedSystem(client))
	{
		// Get the players cash
		int cash;
		pub::Player::InspectCash(client, cash);

		// Get how much the player owes
		int uOwed = MapClients[client].DeathPenaltyCredits;

		// If the amount the player owes is more than they have, set the
		// amount to their total cash
		if (uOwed > cash)
			uOwed = cash;

		// If another player has killed the player
		if (iKillerId != client && DeathPenaltyFractionKiller)
		{
			int uGive = static_cast<int>(uOwed * DeathPenaltyFractionKiller);
			if (uGive)
			{
				// Reward the killer, print message to them
				pub::Player::AdjustCash(client, uGive);
				PrintUserCmdText(iKillerId, L"Death penalty: given " + ToMoneyStr(uGive) + L" credits from %s's death penalty.",
				    Players.GetActiveCharacterName(client));
			}
		}

		if (uOwed)
		{
			// Print message to the player and remove cash
			PrintUserCmdText(client, L"Death penalty: charged " + ToMoneyStr(uOwed) + L" credits.");
			pub::Player::AdjustCash(client, -uOwed);
		}
	}
}

/** @ingroup DeathPenalty
 * @brief Hook on ShipDestroyed to kick off PenalizeDeath
 */
void __stdcall ShipDestroyed(DamageList* dmg, const DWORD* ecx, uint iKill)
{
	if (iKill)
	{
		// Get client
		const CShip* cShip = reinterpret_cast<CShip*>(ecx[4]);
		uint client = cShip->GetOwnerPlayer();

		// Get Killer Id if there is one
		uint iKillerId = 0;
		if (client)
		{
			uint lastInflictorId = dmg->get_cause() == 0 ? ClientInfo[client].dmgLast.get_inflictor_id() : dmg->get_inflictor_id();
			if (lastInflictorId)
			{
				iKillerId = lastInflictorId;
			}
		}

		// Call function to penalize player and reward killer
		PenalizeDeath(client, iKillerId);
	}
}

/** @ingroup DeathPenalty
 * @brief This will save whether the player wants to receieve the /dp notice or not to the flhookuser.ini file
 */
void SaveDPNoticeToCharFile(uint client, std::string value)
{
	CAccount* acc = Players.FindAccountFromClientID(client);
	std::wstring dir;
	HkGetAccountDirName(acc, dir);
	wstring charName = (const wchar_t*)Players.GetActiveCharacterName(client);
	if (!charName.empty())
	{
		std::string scUserFile = scAcctPath + wstos(dir) + "\\flhookuser.ini";
		std::string scSection = "general_" + wstos(charName);
		IniWrite(scUserFile, scSection, "DPnotice", value);
	}
}

/** @ingroup DeathPenalty
 * @brief /dp command. Shows information about death penalty
 */
void UserCmd_DP(uint client, const std::wstring& wscParam)
{
	// If there is no death penalty, no point in having death penalty commands
	if (std::abs(DeathPenaltyFraction) < 0.0001f)
	{
		ConPrint(L"DP Plugin active, but no/too low death penalty fraction is set.");
		return;
	}

	if (wscParam.length()) // Arguments passed
	{
		auto param = ToLower(Trim(GetParam(wscParam, ' ', 0)));
		if (param == L"off")
		{
			MapClients[client].bDisplayDPOnLaunch = false;
			SaveDPNoticeToCharFile(client, "no");
			PrintUserCmdText(client, L"Death penalty notices disabled.");
		}
		else if (param == L"on")
		{
			MapClients[client].bDisplayDPOnLaunch = true;
			SaveDPNoticeToCharFile(client, "yes");
			PrintUserCmdText(client, L"Death penalty notices enabled.");
		}
		else
		{
			PrintUserCmdText(client, L"ERR Invalid parameters");
			PrintUserCmdText(client, L"/dp on | /dp off");
		}
	}
	else
	{
		PrintUserCmdText(client, L"The death penalty is charged immediately when you die.");
		if (!bExcludedSystem(client))
		{
			float fValue;
			pub::Player::GetAssetValue(client, fValue);
			int uOwed = static_cast<int>(fValue * fShipFractionOverride(client));
			PrintUserCmdText(client, L"The death penalty for your ship will be " + ToMoneyStr(uOwed) + L" credits.");
			PrintUserCmdText(client,
			    L"If you would like to turn off the death penalty notices, run "
			    L"this command with the argument \"off\".");
		}
		else
		{
			PrintUserCmdText(client,
			    L"You don't have to pay the death penalty "
			    L"because you are in a specific system.");
		}
	}
}

bool __stdcall UserCmd_Process(uint client, const std::wstring& wscParam)
{
	if (wscParam.find(L"/dp") == 0)
	{
		UserCmd_DP(client, GetParamToEnd(wscParam, ' ', 1));
	}
	return true;
}

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Death Penalty";
	p_PI->sShortName = "death_penalty";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipDestroyed, PLUGIN_ShipDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch, PLUGIN_HkIServerImpl_PlayerLaunch, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadUserCharSettings, PLUGIN_LoadUserCharSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ClearClientInfo, PLUGIN_ClearClientInfo, 0));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));

	return p_PI;
}