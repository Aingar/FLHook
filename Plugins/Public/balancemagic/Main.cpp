// Balance Magic for Discovery FLHook
// September 2018 by Kazinsal etc.
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

// includes 

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#ifndef byte
typedef unsigned char byte;
#endif

#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <list>
#include <map>
#include <algorithm>
#include <FLHook.h>
#include <plugin.h>
#include <math.h>
#include <unordered_map>

bool UserCmd_SnacClassic(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage);

typedef void(*wprintf_fp)(std::wstring format, ...);
typedef bool(*_UserCmdProc)(uint, const wstring &, const wstring &, const wchar_t*);

struct DamageMultiplier {
	float projectileDamage;
	float classMultipliers[20];
	float solarMultiplier;
};

struct USERCMD
{
	wchar_t* wszCmd;
	_UserCmdProc proc;
	wchar_t* usage;
};

USERCMD UserCmds[] =
{
	{ L"/snacclassic", UserCmd_SnacClassic, L"Usage: /snacclassic" },
};

int iLoadedDamageAdjusts = 0;

unordered_map<uint, DamageMultiplier> mapDamageAdjust;

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

/// Load the configuration
void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	mapDamageAdjust.clear();
	iLoadedDamageAdjusts = 0;

	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\balancemagic.cfg";

	INI_Reader ini;
	if (ini.open(scPluginCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("DamageAdjust"))
			{
				while (ini.read_value())
				{
					uint projNameHash = CreateID(ini.get_name_ptr());
					auto projectileInfo = reinterpret_cast<Archetype::Munition*>(Archetype::GetEquipment(projNameHash));
					DamageMultiplier stEntry = { 0.0f };
					stEntry.projectileDamage = projectileInfo->fHullDamage;
					float fighterMultiplier = ini.get_value_float(0);
					float freighterMultiplier = ini.get_value_float(1) ? ini.get_value_float(1) : fighterMultiplier;
					float transportMultiplier = ini.get_value_float(2) ? ini.get_value_float(2) : freighterMultiplier;
					float gunboatMultiplier = ini.get_value_float(3) ? ini.get_value_float(3) : transportMultiplier;
					float cruiserMultiplier = ini.get_value_float(4) ? ini.get_value_float(4) : gunboatMultiplier;
					float battlecruiserMultiplier = ini.get_value_float(5) ? ini.get_value_float(5) : cruiserMultiplier;
					float battleshipMultiplier = ini.get_value_float(6) ? ini.get_value_float(6) : battlecruiserMultiplier;
					float solarMultiplier = ini.get_value_float(7) ? ini.get_value_float(7) : battleshipMultiplier;


					stEntry.classMultipliers[0] = fighterMultiplier;
					stEntry.classMultipliers[1] = fighterMultiplier;
					stEntry.classMultipliers[3] = fighterMultiplier;

					stEntry.classMultipliers[2] = freighterMultiplier;
					stEntry.classMultipliers[4] = freighterMultiplier;
					stEntry.classMultipliers[5] = freighterMultiplier;
					stEntry.classMultipliers[19] = freighterMultiplier;

					stEntry.classMultipliers[6] = transportMultiplier;
					stEntry.classMultipliers[7] = transportMultiplier;
					stEntry.classMultipliers[8] = transportMultiplier;
					stEntry.classMultipliers[9] = transportMultiplier;
					stEntry.classMultipliers[10] = transportMultiplier;

					stEntry.classMultipliers[11] = gunboatMultiplier;
					stEntry.classMultipliers[12] = gunboatMultiplier;

					stEntry.classMultipliers[13] = cruiserMultiplier;
					stEntry.classMultipliers[14] = cruiserMultiplier;

					stEntry.classMultipliers[15] = battlecruiserMultiplier;

					stEntry.classMultipliers[16] = battleshipMultiplier;
					stEntry.classMultipliers[17] = battleshipMultiplier;
					stEntry.classMultipliers[18] = battleshipMultiplier;

					stEntry.solarMultiplier = solarMultiplier;

					mapDamageAdjust[projNameHash] = stEntry;
					++iLoadedDamageAdjusts;
				}
			}
		}
		ini.close();
	}

	ConPrint(L"BALANCEMAGIC: Loaded %u damage adjusts.\n", iLoadedDamageAdjusts);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	// If we're being loaded from the command line while FLHook is running then
	// set_scCfgFile will not be empty so load the settings as FLHook only
	// calls load settings on FLHook startup and .rehash.
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		if (set_scCfgFile.length() > 0)
			LoadSettings();
	}
	return true;
}


// Command-Option-X-O
bool UserCmd_SnacClassic(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	uint baseID = 0;
	pub::Player::GetBase(iClientID, baseID);
	if (!baseID)
	{
		PrintUserCmdText(iClientID, L"ERR cannot engage time machine while undocked");
		return true;
	}

	int iSNACs = 0;
	int iRemHoldSize;
	list<CARGO_INFO> lstCargo;
	HkEnumCargo(ARG_CLIENTID(iClientID), lstCargo, iRemHoldSize);

	foreach(lstCargo, CARGO_INFO, it)
	{
		if ((*it).bMounted)
			continue;

		if (it->iArchID == CreateID("dsy_snova_civ"))
		{
			iSNACs += it->iCount;
			pub::Player::RemoveCargo(iClientID, it->iID, it->iCount);
		}
	}

	if (iSNACs)
	{
		unsigned int good = CreateID("dsy_snova_classic");
		pub::Player::AddCargo(iClientID, good, iSNACs, 1.0, false);
		PrintUserCmdText(iClientID, L"The time machine ate %i modern-day SNACs and gave back old rusty ones from a bygone era.", iSNACs);
	}
	else
	{
		PrintUserCmdText(iClientID, L"The time machine was disappointed to find you had no unmounted SNACs to relinquish unto it");
	}
	return true;
}

/**
This function is called by FLHook when a user types a chat string. We look at the
string they've typed and see if it starts with one of the above commands. If it
does we try to process it.
*/
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

void __stdcall HkCb_AddDmgEntry(DamageList *dmg, ushort subObjID, float& setHealth, DamageEntry::SubObjFate& fate)
{
	returncode = DEFAULT_RETURNCODE;
	if (!iDmgMunitionID || (!iDmgToSpaceID && !iDmgTo))
	{
		return;
	}
	const auto& iter = mapDamageAdjust.find(iDmgMunitionID);
	if (iter == mapDamageAdjust.end())
	{
		return;
	}

	const DamageMultiplier& dmgInfo = iter->second;
	float curr, max;
	bool bShieldsUp;

	if (subObjID == 1) // 1 is base (hull)
		pub::SpaceObj::GetHealth(iDmgToSpaceID, curr, max);
	else if (subObjID == 65521) // 65521 is shield (bubble, not equipment)
		pub::SpaceObj::GetShieldHealth(iDmgToSpaceID, curr, max, bShieldsUp);
	else if (subObjID < 34) // collision groups, external equipment starts at 34 onwards
		curr = setHealth + (dmgInfo.projectileDamage / PLAYER_COLLISION_GROUP_HIT_PTS_SCALE);
	else // external equipment (shield, thrusters, guns)
		curr = setHealth + (dmgInfo.projectileDamage / PLAYER_ATTACHED_EQUIP_HIT_PTS_SCALE);

	if (iDmgToSpaceID == 0) { // for external equipment, this value isn't populated and needs to be fetched
		pub::Player::GetShip(iDmgTo, iDmgToSpaceID);
	}
	uint iTargetType;
	pub::SpaceObj::GetType(iDmgToSpaceID, iTargetType);

	// Deduce: if not fighter nor freighter, then it's obviously solar object.
	if (!(iTargetType & (OBJ_FIGHTER | OBJ_FREIGHTER | OBJ_TRANSPORT | OBJ_GUNBOAT | OBJ_CRUISER | OBJ_CAPITAL)))
	{
		setHealth = curr - (curr - setHealth) * dmgInfo.solarMultiplier;
	}
	else
	{
		uint iArchID;
		pub::SpaceObj::GetSolarArchetypeID(iDmgToSpaceID, iArchID);
		uint targetShipClass = Archetype::GetShip(iArchID)->iShipClass;

		setHealth = curr - (curr - setHealth) * dmgInfo.classMultipliers[targetShipClass];
	}

	// Fix wrong shield rebuild time and shield disabling completely bugs.
	if (setHealth <= 0) {
		setHealth = 0;
		if (subObjID != 65521) // for shield bubble, setting fate to 2 permanently disables it. Do not want.
		{
			fate = static_cast<DamageEntry::SubObjFate>(2); // update fate to ensure destruction event of the element, fate 2 means destroyed
		}
	}
	
	// Collision Group Handling
	if (iDmgTo && subObjID > 1 && subObjID < 34) {
		ClientInfo[iDmgTo].dmgLast = *dmg;
		float currHullHP;
		pub::SpaceObj::GetHealth(iDmgToSpaceID, currHullHP, max);
		float newHullHP = currHullHP - (curr - setHealth - iter->second.projectileDamage);
		if (newHullHP < 0) {
			newHullHP = 0;
		}
		dmg->add_damage_entry(1, newHullHP, static_cast<DamageEntry::SubObjFate>(0)); // fate 0 means alive, but even at 0 hp, it still triggers death fuses correctly in case of ship death, possibly overridden in further FLServer processing?
	}
}

void Plugin_Communication_Callback(PLUGIN_MESSAGE msg, void* data)
{
	returncode = DEFAULT_RETURNCODE;

	if (msg == COMBAT_DAMAGE_OVERRIDE)
	{
		returncode = SKIPPLUGINS;
		COMBAT_DAMAGE_OVERRIDE_STRUCT* info = reinterpret_cast<COMBAT_DAMAGE_OVERRIDE_STRUCT*>(data);
		const auto& iter = mapDamageAdjust.find(info->iMunitionID);
		if (iter != mapDamageAdjust.end())
		{
			info->fDamageMultiplier = iter->second.solarMultiplier;
		}
	}
	return;
}

/** Functions to hook */
EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Balance Magic plugin by Kazinsal";
	p_PI->sShortName = "balancemagic";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&HkCb_AddDmgEntry, PLUGIN_HkCb_AddDmgEntry, 9));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Plugin_Communication_Callback, PLUGIN_Plugin_Communication, 10));
	return p_PI;
}
