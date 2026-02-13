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
		uint hash;
		float fRep;
	};

	struct RepLimit
	{
		float minRep = -0.9f;
		float maxRep = 1.0f;
	};

	/// Map of faction equipment IDs to reputations list.
	static unordered_map<unsigned int, unordered_map<uint, RepLimit>> set_mapFactionReps;

	/// Tag rephacks, (regex, rephacks associated)
	static unordered_map<wstring, vector<TagHack>> set_mapTagHacks;
	static unordered_map<wstring, vector<TagHack>> set_mapNameHacks;

	static unordered_map<uint, unordered_map<uint, RepLimit>> playerRepLimits;

	static unordered_map<uint, wstring> factionNameMap;

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

		unordered_map<uint, RepLimit> defaultRepMap;

		while (ini.read_header())
		{
			if (ini.is_header("default_reps"))
			{
				while (ini.read_value())
				{
					uint repHash = MakeId(ini.get_name_ptr());

					int mode = ini.get_value_int(1);
					if(mode == FactionRep::MODE_REP_LESSTHAN)
					{
						defaultRepMap[repHash] = { -0.9f, ini.get_value_float(0) };
					}
					else if (mode == FactionRep::MODE_REP_GREATERTHAN)
					{
						defaultRepMap[repHash] = { ini.get_value_float(0), 1.0f };
					}
					else if (mode == FactionRep::MODE_REP_STATIC)
					{
						defaultRepMap[repHash] = { ini.get_value_float(0), ini.get_value_float(0) };
					}
				}
				continue;
			}
			if (!ini.is_header("rephack"))
			{
				continue;
			}
			vector<uint> idList;
			unordered_map<uint, RepLimit> factionReps = defaultRepMap;

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
							factionReps[rep.first] = rep.second;
						}
					}
					else
					{
						ConPrint(L"ERROR: Could not inherit reps from %ls, can only inherit from IDs above the entry.\n", stows(ini.get_value_string(0)).c_str());
					}
				}
				else
				{
					uint repHash = MakeId(ini.get_name_ptr());

					int mode = ini.get_value_int(1);
					if (mode == FactionRep::MODE_REP_LESSTHAN)
					{
						factionReps[repHash] = { -0.9f, ini.get_value_float(0) };
					}
					else if (mode == FactionRep::MODE_REP_GREATERTHAN)
					{
						factionReps[repHash] = { ini.get_value_float(0), 1.0f };
					}
					else if (mode == FactionRep::MODE_REP_STATIC)
					{
						factionReps[repHash] = { ini.get_value_float(0), ini.get_value_float(0) };
					}
				}
			}

			for (uint id : idList)
			{
				set_mapFactionReps[id] = factionReps;
			}
		}
		ini.close();
	}

	void LoadTagRephacks()
	{

		set_mapTagHacks.clear();
		// The path to the configuration file.
		char szCurDir[MAX_PATH];
		GetCurrentDirectory(sizeof(szCurDir), szCurDir);
		string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\playercntl_tagrephacks.cfg";

		INI_Reader ini;
		if (!ini.open(scPluginCfgFile.c_str(), false))
		{
			return;
		}

		ChangeINITerminator('\x8');
		while (ini.read_header())
		{
			if (ini.is_header("tag"))
			{
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
						th.hash = MakeId(ini.get_value_string(0));
						th.fRep = ini.get_value_float(1);
						replist.push_back(th);
					}
				}
				set_mapTagHacks[tagname] = replist;
			}
			else if (ini.is_header("name"))
			{
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
						th.hash = MakeId(ini.get_value_string(0));
						th.fRep = ini.get_value_float(1);
						replist.push_back(th);
					}
				}
				set_mapNameHacks[tagname] = replist;
			}
		}
		ini.close();

		ChangeINITerminator(';');
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

		INI_Reader ini;

		string factionpropfile = R"(..\data\initialworld.ini)";
		if (ini.open(factionpropfile.c_str(), false))
		{
			while (ini.read_header())
			{
				if (ini.is_header("Group"))
				{
					uint nickname;
					while (ini.read_value())
					{
						if (ini.is_value("nickname"))
						{
							nickname = MakeId(ini.get_value_string());
						}
						else if (ini.is_value("ids_name"))
						{
							factionNameMap[nickname] = HkGetWStringFromIDS(ini.get_value_int(0));
						}

					}
				}
			}
			ini.close();
		}

		LoadFactionReps();

		LoadTagRephacks();
	}

	/// For the specified client ID check and reset any factions that have reputations
	/// that are greater than the allowed value.
	void CheckReps(unsigned int iClientID)
	{
		auto playerVibe = Players[iClientID].iReputation;

		auto& playerRepLimitEntry = playerRepLimits[iClientID];
		playerRepLimitEntry.clear();

		auto iterIDs = set_mapFactionReps.find(ClientInfo[iClientID].playerID);
		if (iterIDs != set_mapFactionReps.end())
		{
			playerRepLimitEntry = iterIDs->second;
			for (auto& factionRepItem : iterIDs->second)
			{
				float currRep;
				Reputation::Vibe::GetGroupFeelingsTowards(playerVibe, factionRepItem.first, currRep);
				pub::Reputation::SetReputation(playerVibe, factionRepItem.first, currRep);
			}
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
			for (TagHack& tag : tagReps->second)
			{
				playerRepLimitEntry[tag.hash] = { tag.fRep, tag.fRep };
				pub::Reputation::SetReputation(playerVibe, tag.hash, tag.fRep);
			}
			break;
		}

		auto nameHackIter = set_mapNameHacks.find(charName);
		if (nameHackIter != set_mapNameHacks.end())
		{
			for (TagHack& tag : nameHackIter->second)
			{
				playerRepLimitEntry[tag.hash] = { tag.fRep, tag.fRep };
				pub::Reputation::SetReputation(playerVibe, tag.hash, tag.fRep);
			}
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

	float ClampRep(float rep, float minRep, float maxRep)
	{
		if (rep < minRep)
		{
			return minRep;
		}

		if (rep > maxRep)
		{
			return maxRep;
		}

		return rep;
	}

	void RepFixer::SetReputation(uint& repVibe, const uint& affiliation, float& newRep)
	{
		float currRep;
		Reputation::Vibe::GetGroupFeelingsTowards(repVibe, affiliation, currRep);
		uint clientId = Reputation::Vibe::GetClientID(repVibe);

		auto clientData = playerRepLimits.find(clientId);
		if (clientData == playerRepLimits.end())
		{
			if (currRep < newRep)
			{
				newRep = ClampRep(newRep, -1.0f, 0.7f);
			}
			return;
		}
		auto repLimits = clientData->second.find(affiliation);
		if(repLimits == clientData->second.end())
		{
			if (currRep < newRep)
			{
				newRep = ClampRep(newRep, -1.0f, 0.7f);
			}
			return;
		}

		newRep = ClampRep(newRep, repLimits->second.minRep, repLimits->second.maxRep);
	}

	bool RepFixer::UserCmd_SetIFF(uint client, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
	{
		if (!Players[client].iBaseID)
		{
			PrintUserCmdText(client, L"You must be docked on a base!");
			return false;
		}

		auto loweredCaseName = ToLower(GetParamToEnd(wscParam, ' ', 0));
		uint searchedAffil = 0;
		for (auto& faction : factionNameMap)
		{
			if (ToLower(faction.second).find(loweredCaseName) != wstring::npos)
			{
				searchedAffil = faction.first;
				break;
			}
		}

		if (!searchedAffil)
		{
			PrintUserCmdText(client, L"Faction of this name not found!");
			return false;
		}

		static const auto friendly = MakeId("fc_friendly");
		if (searchedAffil == friendly)
		{
			PrintUserCmdText(client, L"This Faction IFF is not available");
			return false;
		}

		uint playerRep = Players[client].iReputation;
		
		uint currAff;
		Reputation::Vibe::GetAffiliation(playerRep, currAff, false);

		if (currAff == searchedAffil)
		{
			PrintUserCmdText(client, L"You already are of selected IFF!");
			return false;
		}

		float currRep;
		Reputation::Vibe::GetGroupFeelingsTowards(playerRep, searchedAffil, currRep);

		const auto& targetFactionName = factionNameMap.find(searchedAffil)->second;

		if (currRep < 0.8)
		{
			PrintUserCmdText(client, L"Insufficient reputation with %s", targetFactionName.c_str());
			return false;
		}

		pub::Reputation::SetAffiliation(playerRep, searchedAffil);

		if (currAff != -1)
		{
			auto currFactionName = factionNameMap.find(currAff)->second;
			PrintUserCmdText(client, L"Your current IFF: %s", currFactionName.c_str());
		}
		else
		{
			PrintUserCmdText(client, L"Your current IFF: None");
		}
		PrintUserCmdText(client, L"Your new IFF: %s, launch to apply the change.", targetFactionName.c_str());
		
		return true;
	}
}