// Autobuy for FLHookPlugin
// December 2015 by BestDiscoveryHookDevs2015
//
// This is based on the original autobuy available in FLHook. However this one was hardly extensible and lacking features.
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <FLHook.h>
#include <hookext_exports.h>
#include <PluginUtilities.h>

using st6_malloc_t = void* (*)(size_t);
using st6_free_t = void(*)(void*);
IMPORT st6_malloc_t st6_malloc;
IMPORT st6_free_t st6_free;

static int set_iPluginDebug = 0;
static float hullRepairFactor = 0.33f;
static float equipmentRepairFactor = 0.3f;

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

void LoadSettings();

struct ForceEquipData
{
	unordered_set<uint> includedShips;
	unordered_set<uint> excludedShips;
	uint smallItem;
	uint mediumItem;
	uint largeItem;
};

unordered_map<uint, ForceEquipData> equipData;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	srand((uint)time(0));
	// If we're being loaded from the command line while FLHook is running then
	// set_scCfgFile will not be empty so load the settings as FLHook only
	// calls load settings on FLHook startup and .rehash.
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		if (set_scCfgFile.length() > 0)
			LoadSettings();
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
	}
	return true;
}

void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	INI_Reader ini;
	string File_FLHook = "flhook_plugins\\forced_equip.cfg";
	if (!ini.open(File_FLHook.c_str(), false))
	{
		return;
	}
	while (ini.read_header())
	{
		if (!ini.is_header("forced_equip"))
		{
			continue;
		}
		ForceEquipData data;
		uint id;
		while (ini.read_value())
		{
			if (ini.is_value("id"))
			{
				id = CreateID(ini.get_value_string());
			}
			else if (ini.is_value("ship"))
			{
				data.includedShips.insert(CreateID(ini.get_value_string()));
			}
			else if (ini.is_value("excluded_ship"))
			{
				data.excludedShips.insert(CreateID(ini.get_value_string()));
			}
			else if (ini.is_value("small"))
			{
				data.smallItem = CreateID(ini.get_value_string(0));
			}
			else if (ini.is_value("medium"))
			{
				data.mediumItem = CreateID(ini.get_value_string(0));
			}
			else if (ini.is_value("large"))
			{
				data.largeItem = CreateID(ini.get_value_string(0));
			}
		}
		equipData[id] = data;
	}
}

bool CheckHardpoints(CShip* cship, uint item, const string& fmtString)
{
	uint client = cship->ownerPlayer;
	char buf[15];
	int i = 0;
	
	bool beamback = false;
	while (true)
	{
		snprintf(buf, 15, fmtString.c_str(), ++i);
		long dummy;
		static long dummy2[100];
		if (!FindHardpoint_OS((long)cship->index, buf, dummy, *(HardpointInfo*)dummy2))
		{
			break;
		}

		CacheString cacheString{ buf };
		strcpy(cacheString.value, buf);
		auto equip = cship->equip_manager.FindByHardpoint(cacheString);

		if (equip && equip->archetype->iArchID == item)
		{
			continue;
		}

		if (!item)
		{
			if (equip)
			{
				Server.ReqRemoveItem(equip->iSubObjId, 1, client);
			}
			continue;
		}

		Players[client].enteredBase = 1;
		if (equip)
		{
			Server.ReqRemoveItem(equip->iSubObjId, 1, client);
		}
		Server.ReqAddItem(item, buf, 1, 1.0f, true, client);
		Players[client].enteredBase = 0;

		EquipDesc ed;
		ed.sID = Players[client].lastEquipId;
		ed.iCount = 1;
		ed.iArchID = item;
		Players[client].lShadowEquipDescList.add_equipment_item(ed, false);

		beamback = true;
	}

	return beamback;
}

void __stdcall PlayerLaunch_AFTER(uint ship, uint client)
{
	returncode = DEFAULT_RETURNCODE;

	auto cship = ClientInfo[client].cship;
	if (!cship)
	{
		return;
	}

	auto& data = equipData.find(ClientInfo[client].playerID);
	if (data == equipData.end() 
		|| (!data->second.includedShips.empty() && !data->second.includedShips.count(cship->archetype->iArchID))
		|| data->second.excludedShips.count(cship->archetype->iArchID))
	{
		return;
	}

	bool beamback = false;
	if (data->second.smallItem)
	{
		beamback |= CheckHardpoints(cship, data->second.smallItem, "HpDecor%02d");
	}
	if (data->second.mediumItem)
	{
		beamback |= CheckHardpoints(cship, data->second.mediumItem, "HpDecorMed%02d");
	}
	if (data->second.largeItem)
	{
		beamback |= CheckHardpoints(cship, data->second.largeItem, "HpDecorLarge%02d");
	}

	if (beamback)
	{
		PrintUserCmdText(client, L"Your forced equipment is being updated, you can now launch again");
		pub::Player::ForceLand(client, Players[client].iLastBaseID);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
	}
}

/// Hook will call this function after calling a plugin function to see if we the
/// processing to continue
EXPORT PLUGIN_RETURNCODE Get_PluginReturnCode()
{
	return returncode;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Loading Settings
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions to hook
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Force Equipment";
	p_PI->sShortName = "force_equipment";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.emplace_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch_AFTER, PLUGIN_HkIServerImpl_PlayerLaunch_AFTER, 0));

	return p_PI;
}
