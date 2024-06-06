// Player Control plugin for FLHookPlugin
// Feb 2010 by Cannon
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <float.h>
#include <FLHook.h>
#include <plugin.h>
#include <math.h>
#include <list>
#include <set>

#include <PluginUtilities.h>
#include "Main.h"

#include <FLCoreServer.h>
#include <FLCoreCommon.h>

namespace RepFixer
{

	struct FactionRep
	{
		// The reputation group nickname to adjust.
		string scRepGroup;

		// The adjustment mode. If the player's reputation for scRepGroup
		// is greater than fRep then make the reputation equal to fRep
		static const int MODE_REP_LESSTHAN = 0;

		// The adjustment mode. If the player's reputation for scRepGroup
		// is less than fRep then make the reputation equal to fRep
		static const int MODE_REP_GREATERTHAN = 1;

		// Don't change anything/ignore this reputation group.
		static const int MODE_REP_NO_CHANGE = 2;

		// Fix the rep group to this level.
		static const int MODE_REP_STATIC = 3;

		// The adjustment mode.
		int iMode;

		// The reputation limit.
		float fRep;
	};

	struct TagHack
	{
		string scRepGroup;
		float fRep;
	};

	/// Map of faction equipment IDs to reputations list.
	static unordered_map<unsigned int, vector<FactionRep>> set_mapFactionReps;

	/// Tag rephacks, (regex, rephacks associated)
	static unordered_map<wstring, vector<TagHack>> set_mapTagHacks;

	/// If true updates are logged to flhook.log
	static bool set_bLogUpdates = false;

	/// If true then the ID item must be mounted
	static bool set_bItemMustBeMounted = true;

	/// If true then do updates.
	static bool set_bEnableRepFixUpdates = true;

	/// Load the reputations for the specified equipment faction ID nickname.
	static void LoadFactionReps()
	{
		set_mapFactionReps.clear();

		char szCurDir[MAX_PATH];
		GetCurrentDirectory(sizeof(szCurDir), szCurDir);
		string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\playercntl_rephacks.cfg";


		INI_Reader ini;
		if (!ini.open(scPluginCfgFile.c_str(), false))
		{
			return;
		}

		unordered_map<uint, FactionRep> defaultRepMap;

		while (ini.read_header())
		{
			if (ini.is_header("default_reps"))
			{
				while (ini.read_value())
				{
					FactionRep factionRep;
					factionRep.scRepGroup = ini.get_name_ptr();

					factionRep.fRep = ini.get_value_float(0);
					if (factionRep.fRep > 1.0f)
						factionRep.fRep = 1.0f;
					else if (factionRep.fRep < -1.0f)
						factionRep.fRep = -1.0f;

					factionRep.iMode = ini.get_value_int(1);
					if (factionRep.iMode == FactionRep::MODE_REP_LESSTHAN
						|| factionRep.iMode == FactionRep::MODE_REP_GREATERTHAN
						|| factionRep.iMode == FactionRep::MODE_REP_STATIC)
					{
						uint repHash = CreateID(factionRep.scRepGroup.c_str());
						defaultRepMap[repHash] = factionRep;
					}
				}
				continue;
			}
			if (!ini.is_header("rephack"))
			{
				continue;
			}
			vector<uint> idList;
			unordered_map<uint, FactionRep> factionReps = defaultRepMap;

			while (ini.read_value())
			{
				if (ini.is_value("id"))
				{
					uint counter = 0;
					string currId = ini.get_value_string(counter++);
					while (!currId.empty())
					{
						idList.emplace_back(CreateID(currId.c_str()));
						currId = ini.get_value_string(counter++);
					}
				}
				else if (ini.is_value("inherits"))
				{
					uint inheritedGrp = CreateID(ini.get_value_string(0));
					if (set_mapFactionReps.count(inheritedGrp))
					{
						for (auto& rep : set_mapFactionReps.at(inheritedGrp))
						{
							uint repHash = CreateID(rep.scRepGroup.c_str());
							factionReps[repHash] = rep;
						}
					}
					else
					{
						ConPrint(L"ERROR: Could not inherit reps from %ls, can only inherit from IDs above the entry.\n", stows(ini.get_value_string(0)).c_str());
					}
				}
				else
				{
					FactionRep factionRep;
					factionRep.scRepGroup = ini.get_name_ptr();

					factionRep.fRep = ini.get_value_float(0);
					if (factionRep.fRep > 1.0f)
						factionRep.fRep = 1.0f;
					else if (factionRep.fRep < -1.0f)
						factionRep.fRep = -1.0f;

					factionRep.iMode = ini.get_value_int(1);
					if (factionRep.iMode == FactionRep::MODE_REP_LESSTHAN
						|| factionRep.iMode == FactionRep::MODE_REP_GREATERTHAN
						|| factionRep.iMode == FactionRep::MODE_REP_STATIC)
					{
						uint repHash = CreateID(factionRep.scRepGroup.c_str());
						factionReps[repHash] = factionRep;
					}
				}
			}

			vector<FactionRep> factionRepVector;
			for (auto& rep : factionReps)
			{
				factionRepVector.emplace_back(rep.second);
			}
			for (uint id : idList)
			{
				set_mapFactionReps[id] = factionRepVector;
			}
		}

	}

	void LoadTagRephacks()
	{

		set_mapTagHacks.clear();
		// The path to the configuration file.
		char szCurDir[MAX_PATH];
		GetCurrentDirectory(sizeof(szCurDir), szCurDir);
		string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\playercntl_tagrephacks.cfg";

		int iLoaded = 0;

		INI_Reader ini;
		if (!ini.open(scPluginCfgFile.c_str(), false))
		{
			return;
		}

		while (ini.read_header())
		{
			if (!ini.is_header("tag"))
			{
				continue;
			}
			wstring tagname;
			vector<TagHack> replist;

			while (ini.read_value())
			{
				if (ini.is_value("name"))
				{
					tagname = stows(ini.get_value_string());
				}
				else if (ini.is_value("rep"))
				{
					TagHack th;
					th.scRepGroup = ini.get_value_string(0);
					th.fRep = ini.get_value_float(1);
					replist.push_back(th);
				}
			}
			set_mapTagHacks[tagname] = replist;
			++iLoaded;
		}
		ini.close();

		ConPrint(L"Playercntl: Loaded %u tag rephacks\n", iLoaded);
	}

	void ReloadFactionReps()
	{
		LoadFactionReps();
		LoadTagRephacks();
	}

	/// Load the plugin settings.
	void RepFixer::LoadSettings(const string &scPluginCfgFile)
	{
		set_bEnableRepFixUpdates = IniGetB(scPluginCfgFile, "RepFixer", "EnableRepFixUpdates", false);
		set_bLogUpdates = IniGetB(scPluginCfgFile, "RepFixer", "LogRepFixUpdates", false);
		set_bItemMustBeMounted = IniGetB(scPluginCfgFile, "RepFixer", "ItemMustBeMounted", true);

		// For each "ID/License" equipment item load the faction reputation list.
		set_mapFactionReps.clear();

		LoadFactionReps();

		LoadTagRephacks();
	}

	/// For the specified client ID check and reset any factions that have reputations
	/// that are greater than the allowed value.
	static void CheckReps(unsigned int iClientID)
	{

		int playerRep = Players[iClientID].iReputation;
		for (auto& cargo : Players[iClientID].equipDescList.equip)
		{
			// If the item is not mounted and we are only checking mounted items
			// then skip to the next one.
			if (!cargo.bMounted && set_bItemMustBeMounted)
				continue;

			// If the item is not an 'ID' then skip to the next one. 
			unordered_map<unsigned int, vector<FactionRep> >::iterator iterIDs = set_mapFactionReps.find(cargo.iArchID);
			if (iterIDs == set_mapFactionReps.end())
				continue;


			// The item is an 'ID'; check and adjust the player reputations
			// if needed.
			for (vector<FactionRep>::iterator iterReps = iterIDs->second.begin(); iterReps != iterIDs->second.end(); iterReps++)
			{
				const FactionRep& rep = *iterReps;

				uint iRepGroupID;
				float fRep;
				pub::Reputation::GetReputationGroup(iRepGroupID, rep.scRepGroup.c_str());
				pub::Reputation::GetGroupFeelingsTowards(playerRep, iRepGroupID, fRep);
				if (((fRep > rep.fRep) && (rep.iMode == FactionRep::MODE_REP_LESSTHAN))
					|| ((fRep < rep.fRep) && (rep.iMode == FactionRep::MODE_REP_GREATERTHAN)))
				{
					pub::Reputation::SetReputation(playerRep, iRepGroupID, rep.fRep);
				}
				else if ((fRep != rep.fRep) && (rep.iMode == FactionRep::MODE_REP_STATIC))
				{
					pub::Reputation::SetReputation(playerRep, iRepGroupID, rep.fRep);
				}
			}

			// We've adjusted the reps, stop searching the cargo list.
			break;
		}

		wstring charName = (const wchar_t*)Players.GetActiveCharacterName(iClientID);

		//tag based rephacks
		for (unordered_map<wstring, vector<TagHack>>::iterator tagReps = set_mapTagHacks.begin(); tagReps != set_mapTagHacks.end(); tagReps++)
		{
			if (charName.find(tagReps->first) == string::npos)
			{
				continue;
			}
			//we have a match, apply reps
			for each (TagHack tag in tagReps->second)
			{
				uint iRepGroupID;
				pub::Reputation::GetReputationGroup(iRepGroupID, tag.scRepGroup.c_str());
				pub::Reputation::SetReputation(playerRep, iRepGroupID, tag.fRep);
			}
			break;
		}

		return;
	}

	void RepFixer::PlayerLaunch(unsigned int iShip, unsigned int iClientID)
	{
		if (set_bEnableRepFixUpdates)
			CheckReps(iClientID);
	}

	void RepFixer::BaseEnter(unsigned int iBaseID, unsigned int iClientID)
	{
		if (set_bEnableRepFixUpdates)
			CheckReps(iClientID);
	}
}