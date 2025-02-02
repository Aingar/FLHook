/**
 * @date Unknown
 * @author ||KOS||Acid (Ported by Raikkonen)
 * @defgroup KillTracker Kill Tracker
 * @brief
 * This plugin is used to count pvp kills and save them in the player file. Vanilla doesn't do this by default.
 * Also keeps track of damage taken between players, prints greatest damage contributor.
 *
 * @paragraph cmds Player Commands
 * All commands are prefixed with '/' unless explicitly specified.
 * - kills {client} - Shows the pvp kills for a player if a client id is specified, or if not, the player who typed it.
 *
 * @paragraph adminCmds Admin Commands
 * There are no admin commands in this plugin.
 *
 * @paragraph configuration Configuration
 * No configuration file is needed.
 *
 * @paragraph ipc IPC Interfaces Exposed
 * This plugin does not expose any functionality.
 *
 * @paragraph optional Optional Plugin Dependencies
 * This plugin has no dependencies.
 */

#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include <array>
#include <unordered_map>
#include <set>


PLUGIN_RETURNCODE returncode;

struct DamageDoneStruct
{
	float currDamage = 0.0f;
	float lastUndockDamage = 0.0f;
	bool hasAttacked = false;
};

static array<array<DamageDoneStruct, MAX_CLIENT_ID + 1>, MAX_CLIENT_ID + 1> damageArray;

//! Message broadcasted systemwide upon ship's death
//! {0} is replaced with victim's name, {1} with player who dealt the most damage to them,
//! {2} with percentage of hull damage taken byt that player.
wstring defaultDeathDamageTemplate = L"%victim died to %killer";
wstring killMsgStyle = L"0xCC303001"; // default: blue, bold
unordered_map<uint, vector<wstring>> shipClassToDeathMsgListMap;

uint numberOfKillers = 3;
float deathBroadcastRange = 15000;
uint minimumAssistPercentage = 5;

FILE* damageDeathLog;

void LogDeathDamage(const char* szString, ...)
{
	char szBufString[1024];
	va_list marker;
	va_start(marker, szString);
	_vsnprintf(szBufString, sizeof(szBufString) - 1, szString, marker);

	if (damageDeathLog) {
		char szBuf[64];
		time_t tNow = time(0);
		struct tm* t = localtime(&tNow);
		strftime(szBuf, sizeof(szBuf), "%Y.%m.%d %H:%M:%S", t);
		fprintf(damageDeathLog, "[%s] %s\n", szBuf, szBufString);
		fflush(damageDeathLog);
	}
	else {
		ConPrint(L"Failed to write damage log!\n");
	}
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	// If we're being loaded from the command line while FLHook is running then
	// set_scCfgFile will not be empty so load the settings as FLHook only
	// calls load settings on FLHook startup and .rehash.
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		damageDeathLog = fopen("./flhook_logs/damage_death.log", "at");

		HkLoadStringDLLs();
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
		HkUnloadStringDLLs();
		fclose(damageDeathLog);
	}
	return true;
}

// Load Settings
void __stdcall LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	srand((uint)time(nullptr));

	for (auto& subArray : damageArray)
	{
		subArray.fill({ 0.0f, 0.0f });
	}
	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + R"(\flhook_plugins\kill_tracker.cfg)";

	INI_Reader ini;
	if (ini.open(scPluginCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("General"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("DeathBroadcastRange"))
					{
						deathBroadcastRange = ini.get_value_float(0);
					}
					else if (ini.is_value("NumberOfKillers"))
					{
						numberOfKillers = ini.get_value_int(0);
					}
					else if (ini.is_value("KillMsgStyle"))
					{
						killMsgStyle = stows(ini.get_value_string());
					}
				}
			}
			else if (ini.is_header("KillMessage"))
			{
				vector<wstring> deathMsgList;
				uint shipClass;
				while (ini.read_value())
				{
					if (ini.is_value("ShipType"))
					{
						string typeStr = ToLower(ini.get_value_string(0));
						if (typeStr == "fighter")
							shipClass = Fighter;
						else if (typeStr == "freighter")
							shipClass = Freighter;
						else if (typeStr == "transport")
							shipClass = Transport;
						else if (typeStr == "gunboat")
							shipClass = Gunboat;
						else if (typeStr == "cruiser")
							shipClass = Cruiser;
						else if (typeStr == "capital")
							shipClass = Capital;
						else
							ConPrint(L"KillTracker: Error reading config for Death Messages, value %ls not recognized\n", stows(typeStr).c_str());
					}
					else if (ini.is_value("DeathMsg"))
					{
						deathMsgList.push_back(stows(ini.get_value_string()));
					}
				}

				shipClassToDeathMsgListMap[shipClass] = deathMsgList;
			}
		}
	}
}

void UserCmd_SetDeathMsg(const uint client, const wstring& wscParam)
{
	wstring param = ToLower(GetParam(wscParam, ' ', 2));
	if (param.empty())
	{
		PrintUserCmdText(client, L"Usage: /set diemsg All|AllNoConn|System|Self|None");
	}

	DIEMSGTYPE dieMsg;
	if (param == L"all")
	{
		dieMsg = DIEMSG_ALL;
	}
	else if (param == L"allnoconn")
	{
		dieMsg = DIEMSG_ALL_NOCONN;
	}
	else if (param == L"self")
	{
		dieMsg = DIEMSG_SELF;
	}
	else if (param == L"system")
	{
		dieMsg = DIEMSG_SYSTEM;
	}
	else if (param == L"none")
	{
		dieMsg = DIEMSG_NONE;
	}
	else
	{
		PrintUserCmdText(client, L"Usage: /set diemsg All|AllNoConn|System|Self|None");
		return;
	}
	
	ClientInfo[client].dieMsg = dieMsg;
	string scUserFile;
	CAccount* acc = Players.FindAccountFromClientID(client);
	wstring wscDir; HkGetAccountDirName(acc, wscDir);
	scUserFile = scAcctPath + wstos(wscDir) + "\\flhookuser.ini";

	IniWrite(scUserFile, "settings", "DieMsg", itos(dieMsg));

	PrintUserCmdText(client, L"OK");
}

void __stdcall ShipHullDamage(IObjRW* iobj, float& incDmg, DamageList* dmg)
{
	returncode = DEFAULT_RETURNCODE;

	uint targetClient = reinterpret_cast<CShip*>(iobj->cobj)->ownerPlayer;
	if (!targetClient || !dmg->iInflictorPlayerID || targetClient == dmg->iInflictorPlayerID || incDmg <= 0.0f)
	{
		return;
	}
	
	if (!damageArray[targetClient][dmg->iInflictorPlayerID].hasAttacked
		&& dmg->damageCause != DamageCause::CruiseDisrupter
		&& dmg->damageCause != DamageCause::Collision
		&& (!Players[targetClient].PlayerGroup || Players[targetClient].PlayerGroup != Players[dmg->iInflictorPlayerID].PlayerGroup))
	{
		damageArray[targetClient][dmg->iInflictorPlayerID].hasAttacked = true;
		string inflictor = wstos((const wchar_t*)Players.GetActiveCharacterName(dmg->iInflictorPlayerID));
		string victim = wstos((const wchar_t*)Players.GetActiveCharacterName(targetClient));
		
		LogDeathDamage("%s has attacked %s in %s", inflictor.c_str(), victim.c_str(), Universe::get_system(iobj->cobj->system)->nickname);
	}
	damageArray[targetClient][dmg->iInflictorPlayerID].currDamage += incDmg;
}
void __stdcall ShipShieldDamage(IObjRW* iobj, CEShield* shield, float& incDmg, DamageList* dmg)
{
	returncode = DEFAULT_RETURNCODE;

	uint targetClient = reinterpret_cast<CShip*>(iobj->cobj)->ownerPlayer;
	if (!targetClient || !dmg->iInflictorPlayerID || targetClient == dmg->iInflictorPlayerID || incDmg <= 0.0f)
	{
		return;
	}

	if (!damageArray[targetClient][dmg->iInflictorPlayerID].hasAttacked
		&& dmg->damageCause != DamageCause::CruiseDisrupter
		&& dmg->damageCause != DamageCause::Collision
		&& (!Players[targetClient].PlayerGroup || Players[targetClient].PlayerGroup != Players[dmg->iInflictorPlayerID].PlayerGroup))
	{
		damageArray[targetClient][dmg->iInflictorPlayerID].hasAttacked = true;
		string inflictor = wstos((const wchar_t*)Players.GetActiveCharacterName(dmg->iInflictorPlayerID));
		string victim = wstos((const wchar_t*)Players.GetActiveCharacterName(targetClient));

		LogDeathDamage("%s has attacked %s in %s", inflictor.c_str(), victim.c_str(), Universe::get_system(iobj->cobj->system)->nickname);

		bool firstEntry = true;
		for (auto& equip : Players[targetClient].equipDescList.equip)
		{
			if (equip.bMounted)
			{
				continue;
			}

			bool isCommodity = false;
			pub::IsCommodity(equip.iArchID, isCommodity);
			if (isCommodity)
			{
				if (firstEntry)
				{
					firstEntry = false;
					LogDeathDamage("cargo onboard:");
				}
				auto equipArch = Archetype::GetEquipment(equip.iArchID);
				LogDeathDamage("- %dx %s", equip.iCount, wstos(HkGetWStringFromIDS(equipArch->iIdsName)).c_str());
			}
		}
	}
}

void ClearDamageTaken(const uint victim)
{
	damageArray[victim].fill({ 0.0f, 0.0f, false });
}

void ClearDamageDone(const uint inflictor, const bool isFullReset)
{
	for (int i = 1; i <= MAX_CLIENT_ID; i++)
	{
		auto& damageData = damageArray[i][inflictor];
		if (isFullReset)
		{
			damageData.lastUndockDamage = 0.0f;
		}
		else
		{
			damageData.lastUndockDamage = damageData.currDamage;
		}
		damageData.currDamage = 0.0f;
		damageData.hasAttacked = false;
	}
}

enum MessageType {
	MSGNONE,
	MSGBLUE,
	MSGRED,
	MSGDARKRED
};

MessageType inline getMessageType(const uint victimId, const PlayerData* pd, const uint system, bool isGroupInvolved)
{
	static const uint connID = CreateID("Li06");
	DIEMSGTYPE dieMsg = ClientInfo[pd->iOnlineID].dieMsg;
	if (dieMsg == DIEMSG_NONE)
	{
		return MSGNONE;
	}
	else if (isGroupInvolved)
	{
		return MSGBLUE;
	}
	else if (dieMsg == DIEMSG_SELF)
	{
		if (victimId == pd->iOnlineID)
		{
			return MSGBLUE;
		}
	}
	else if (dieMsg == DIEMSG_SYSTEM)
	{
		if (victimId == pd->iOnlineID)
		{
			return MSGBLUE;
		}
		if (system == pd->iSystemID)
		{
			return MSGRED;
		}
	}
	else if (dieMsg == DIEMSG_ALL_NOCONN)
	{
		if (victimId == pd->iOnlineID)
		{
			return MSGBLUE;
		}
		if (system == pd->iSystemID)
		{
			return MSGRED;
		}
		if (system != connID)
		{
			return MSGDARKRED;
		}
	}
	else if (dieMsg == DIEMSG_ALL)
	{
		if (victimId == pd->iOnlineID)
		{
			return MSGBLUE;
		}
		if (pd->iSystemID == system)
		{
			return MSGRED;
		}
		return MSGDARKRED;
	}
	return MSGNONE;
}

void ProcessDeath(uint victimId, const wstring* message1, const wstring* message2, const uint system, bool isPvP, set<CPlayerGroup*>& involvedGroups, set<uint>& involvedPlayers)
{
	wstring deathMessageBlue1 = L"<TRA data=\"0xFF000001" // Blue, Bold
		L"\" mask=\"-1\"/><TEXT>" + XMLText(*message1) + L"</TEXT>";
	wstring deathMessageRed1 = L"<TRA data=\"0x0000CC01" // Red, Bold
		L"\" mask=\"-1\"/><TEXT>" + XMLText(*message1) + L"</TEXT>";
	wstring deathMessageDarkRed = L"<TRA data=\"0x18188c01" // Dark Red, Bold
		L"\" mask=\"-1\"/><TEXT>" + XMLText(*message1) + L"</TEXT>";

	wstring deathMessageRed2;
	wstring deathMessageBlue2;
	if (message2)
	{
		deathMessageRed2 = L"<TRA data=\"0x0000CC01" // Red, Bold
			L"\" mask=\"-1\"/><TEXT>" + XMLText(*message2) + L"</TEXT>";
		
		deathMessageBlue2 = L"<TRA data=\"0xFF000001" // Blue, Bold
			L"\" mask=\"-1\"/><TEXT>" + XMLText(*message2) + L"</TEXT>";
	}
	
	PlayerData* pd = nullptr;
	while (pd = Players.traverse_active(pd))
	{
		bool isInvolved = involvedGroups.count(pd->PlayerGroup) || involvedPlayers.count(pd->iOnlineID);
		MessageType msgType = getMessageType(victimId, pd, system, isInvolved);

		if (msgType == MSGBLUE)
		{
			if (isPvP)
			{
				HkFMsg(pd->iOnlineID, deathMessageBlue1);
				if (message2)
				{
					HkFMsg(pd->iOnlineID, deathMessageBlue2);
				}
			}
			else
			{
				HkFMsg(pd->iOnlineID, deathMessageRed1);
				if (message2)
				{
					HkFMsg(pd->iOnlineID, deathMessageRed2);
				}
			}
		}
		else if (msgType == MSGRED)
		{
			HkFMsg(pd->iOnlineID, deathMessageRed1);
			if (message2)
			{
				HkFMsg(pd->iOnlineID, deathMessageRed2);
			}
		}
		else if (msgType == MSGDARKRED)
		{
			HkFMsg(pd->iOnlineID, deathMessageDarkRed);
		}
	}
}

wstring SelectRandomDeathMessage(const uint iClientID)
{
	uint shipType = Archetype::GetShip(Players[iClientID].iShipArchetype)->iArchType;
	const auto& deathMsgList = shipClassToDeathMsgListMap[shipType];
	if (deathMsgList.empty())
	{
		return defaultDeathDamageTemplate;
	}
	else
	{
		return deathMsgList.at(rand() % deathMsgList.size());
	}
}

inline float GetDamageDone(const DamageDoneStruct& damageDone)
{
	if (damageDone.currDamage != 0.0f)
	{
		return damageDone.currDamage;
	}
	if(damageDone.lastUndockDamage != 0.0f)
	{
		return damageDone.lastUndockDamage;
	}
	return 0.0f;
}

void __stdcall SendDeathMessage(const wstring& message, uint& system, uint& clientVictim, uint& clientKiller, DamageCause& dmgCause)
{
	returncode = DEFAULT_RETURNCODE;
	if (!clientVictim)
	{
		return;
	}

	returncode = NOFUNCTIONCALL;

	map<float, uint> damageToInflictorMap; // damage is the key instead of value because keys are sorted, used to render top contributors in order
	set<CPlayerGroup*> involvedGroups;
	set<uint> involvedPlayers;

	float totalDamageTaken = 0.0f;
	PlayerData* pd = nullptr;
	while (pd = Players.traverse_active(pd))
	{
		auto& damageData = damageArray[clientVictim][pd->iOnlineID];
		float damageToAdd = GetDamageDone(damageData);
		
		if (pd->PlayerGroup && (pd->iOnlineID == clientVictim || damageToAdd > 0.0f))
		{
			involvedGroups.insert(pd->PlayerGroup);
		}
		else if (damageToAdd > 0.0f)
		{
			involvedPlayers.insert(pd->iOnlineID);
		}
		if (damageToAdd == 0.0f)
		{
			continue;
		}

		damageToInflictorMap[damageToAdd] = pd->iOnlineID;
		totalDamageTaken += damageToAdd;
	}

	if (clientKiller && clientKiller != clientVictim && totalDamageTaken == 0.0f)
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return;
	}

	const Archetype::Ship* shipArch = Archetype::GetShip(Players[clientVictim].iShipArchetype);

	if (totalDamageTaken < (shipArch->fHitPoints * 0.02))
	{
		ClearDamageTaken(clientVictim);
		ProcessDeath(clientVictim, &message, nullptr, system, false, involvedGroups, involvedPlayers);
		return;
	}

	wstring victimName = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(clientVictim));

	wstring deathMessage = SelectRandomDeathMessage(clientVictim);
	deathMessage = ReplaceStr(deathMessage, L"%victim", victimName);
	wstring assistMessage = L"";

	uint killerCounter = 0;

	for (auto& i = damageToInflictorMap.rbegin(); i != damageToInflictorMap.rend(); i++) // top values at the end
	{
		if (i == damageToInflictorMap.rend() || killerCounter >= numberOfKillers)
		{
			break;
		}
		uint contributionPercentage = static_cast<uint>(round((i->first / totalDamageTaken) * 100));
		if (contributionPercentage < minimumAssistPercentage)
		{
			break;
		}

		wstring inflictorName = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(i->second));
		if (killerCounter == 0)
		{
			clientKiller = i->second; // override the killer ID to the top damage contributor for other plugins.
			deathMessage = ReplaceStr(deathMessage, L"%killer", inflictorName);
			deathMessage += L"(" + stows(itos(contributionPercentage)) + L"%)";
		}
		else if (killerCounter == 1)
		{
			assistMessage = L"Assisted by: " + inflictorName + L"(" + stows(itos(contributionPercentage)) + L"%)";
		}
		else
		{
			assistMessage += L", " + inflictorName + L"(" + stows(itos(contributionPercentage)) + L"%)";
		}
		killerCounter++;
	}

	if (assistMessage.empty())
	{
		LogDeathDamage("Player Death: %s, total %0.0f", wstos(deathMessage).c_str(), totalDamageTaken);
		ProcessDeath(clientVictim, &deathMessage, nullptr, system, true, involvedGroups, involvedPlayers);
	}
	else
	{
		LogDeathDamage("Player Death: %s %s, total %0.0f", wstos(deathMessage).c_str(), wstos(assistMessage).c_str(), totalDamageTaken);
		ProcessDeath(clientVictim, &deathMessage, &assistMessage, system, true, involvedGroups, involvedPlayers);
	}
	ClearDamageTaken(clientVictim);
}

void __stdcall DelayedDisconnect(uint client)
{
	returncode = DEFAULT_RETURNCODE;
	ClearDamageTaken(client);
	ClearDamageDone(client, true);
}

void __stdcall Disconnect(uint client, enum EFLConnection conn)
{
	returncode = DEFAULT_RETURNCODE;
	ClearDamageTaken(client);
	ClearDamageDone(client, true);
}

void __stdcall PlayerLaunch(uint shipId, uint client)
{
	returncode = DEFAULT_RETURNCODE;
	ClearDamageTaken(client);
	ClearDamageDone(client, false);
}

void __stdcall CharacterSelect(CHARACTER_ID const& cid, uint client)
{
	returncode = DEFAULT_RETURNCODE;
	ClearDamageTaken(client);
	ClearDamageDone(client, true);
}


bool UserCmd_Process(uint client, const wstring &wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	if (wscCmd.find(L"/set diemsg") == 0)
	{
		UserCmd_SetDeathMsg(client, wscCmd);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
	}
	return true;
}


EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Kill Tracker";
	p_PI->sShortName = "killtracker";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipHullDamage, PLUGIN_ShipHullDmg, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipShieldDamage, PLUGIN_ShipShieldDmg, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&SendDeathMessage, PLUGIN_SendDeathMsg, 5));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&DelayedDisconnect, PLUGIN_DelayedDisconnect, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Disconnect, PLUGIN_HkIServerImpl_DisConnect, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch, PLUGIN_HkIServerImpl_PlayerLaunch, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect, PLUGIN_HkIServerImpl_CharacterSelect, 0));

	return p_PI;
}