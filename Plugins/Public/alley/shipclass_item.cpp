// AlleyPlugin for FLHookPlugin
// April 2015 by Alley
//
// 
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <float.h>
#include <FLHook.h>
#include <plugin.h>
#include <list>
#include <set>

#include <PluginUtilities.h>
#include "PlayerRestrictions.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Structures and shit yo
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

unordered_map <uint, wstring> shipclassnames;
unordered_map <uint, scistruct> shipclassitems;
unordered_map <uint, wstring> owned;

unordered_map<uint, uint> mapIDs;

//we store these here as it's more efficient than constantly requested what id the player is flying with.
struct pinfo
{
	uint playerid;
	float maxholdsize;
	uint shipclass;
	uint shiparch;
};

unordered_map <uint, pinfo> clientplayerid;

//the struct
struct iddockinfo
{
	int type;
	uint cargo;
	unordered_set<uint> systems;
	unordered_set<uint> shipclasses;
	unordered_set<uint> exempt;
	unordered_set<uint> iff;
	unordered_set<uint> bases;
	wstring dockMessage;
};

//first uint will be the ID hash
unordered_map<uint, iddockinfo> iddock;

unordered_map<uint, uint> player_last_base;

void SCI::LoadSettings()
{
	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\alley_shipclassitems.cfg";
	string idfile = "..\\data\\equipment\\misc_equip.ini";

	INI_Reader ini;
	if (ini.open(scPluginCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("shipclasses"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("class"))
					{
						shipclassnames[ini.get_value_int(0)] = stows(ini.get_value_string(1));
					}
				}
			}
			else if (ini.is_header("itemrestrict"))
			{
				scistruct sci;
				uint itemarchid;
				while (ini.read_value())
				{
					if (ini.is_value("item"))
					{
						itemarchid = CreateID(ini.get_value_string(0));
					}
					else if (ini.is_value("canmount"))
					{
						sci.canmount.insert(ini.get_value_int(0));
					}
					else if (ini.is_value("nomount"))
					{
						sci.nomount.insert(CreateID(ini.get_value_string(0)));
					}
				}
				shipclassitems[itemarchid] = sci;
			}
			else if (ini.is_header("idrestrict"))
			{
				iddockinfo info;
				uint id;
				while (ini.read_value())
				{
					if (ini.is_value("name"))
					{
						id = CreateID(ini.get_value_string(0));
					}
					else if (ini.is_value("type"))
					{
						info.type = ini.get_value_int(0);
					}
					else if (ini.is_value("cargo"))
					{
						info.cargo = ini.get_value_int(0);
					}
					else if (ini.is_value("shipclass"))
					{
						info.shipclasses.insert(ini.get_value_int(0));
					}
					else if (ini.is_value("system"))
					{
						info.systems.insert(CreateID(ini.get_value_string(0)));
					}
					else if (ini.is_value("exempt"))
					{
						info.exempt.insert(CreateID(ini.get_value_string(0)));
					}
					else if (ini.is_value("iff"))
					{
						info.iff.insert(MakeId(ini.get_value_string(0)));
					}
					else if (ini.is_value("base"))
					{
						info.bases.insert(CreateID(ini.get_value_string(0)));
					}
					else if (ini.is_value("message"))
					{
						info.dockMessage = stows(ini.get_value_string());
					}
				}
				iddock[id] = info;
			}
		}
		ini.close();
	}
	if (ini.open(idfile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("Tractor"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						mapIDs[CreateID(ini.get_value_string(0))] = 0;
					}
				}
			}
		}
		ini.close();
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Dependencies
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SCI::CheckItems(unsigned int iClientID)
{
	bool foundamountedid = false;


	// Check all items on ship, see if one isn't meant for this ship class.
	for (list<EquipDesc>::iterator item = Players[iClientID].equipDescList.equip.begin(); item != Players[iClientID].equipDescList.equip.end(); item++)
	{
		if (!item->bMounted)
		{
			continue;
		}

		//we stick in a small check to see if there's an ID mounted
		if (mapIDs.count(item->iArchID))
		{
			foundamountedid = true;
		}

		if (!shipclassitems.count(item->iArchID))
		{
			continue;
		}

		auto& shipClassItem = shipclassitems.at(item->iArchID);

		Archetype::Ship* TheShipArch = Archetype::GetShip(Players[iClientID].iShipArchetype);
		wstring classname = shipclassnames.find(TheShipArch->iShipClass)->second;

		// find if the ship class match
		if (!shipClassItem.canmount.count(TheShipArch->iShipClass))
		{
			const GoodInfo* gi = GoodList::find_by_id(item->iArchID);
			wstring wscMsg = L"ERR you can't undock with %item mounted. This item can't be mounted on a %shipclass.";
			wscMsg = ReplaceStr(wscMsg, L"%shipclass", classname.c_str());

			if (gi)
			{
				wscMsg = ReplaceStr(wscMsg, L"%item", HkGetWStringFromIDS(gi->iIDSName).c_str());
			}
			else
			{
				wscMsg = ReplaceStr(wscMsg, L"%item", L"<errorStaffNotified>");
				ConPrint(L"Alley: Error, unable to process item 0x%x\n", item->iArchID);
			}

			owned[iClientID] = wscMsg;
			StoreReturnPointForClient(iClientID);
			return;
		}
		// check for non-stackable items
		
		for (list<EquipDesc>::iterator itemstack = Players[iClientID].equipDescList.equip.begin(); itemstack != Players[iClientID].equipDescList.equip.end(); itemstack++)
		{
			if (!itemstack->bMounted)
			{
				continue;
			}
			// find if a mounted item match the non-stack list
			if (shipClassItem.nomount.count(TheShipArch->iShipClass))
			{
				const GoodInfo* gi1 = GoodList::find_by_id(item->iArchID);
				const GoodInfo* gi2 = GoodList::find_by_id(itemstack->iArchID);
				wstring wscMsg = L"ERR You are not allowed to have %item and %second mounted at the same time.";
				wscMsg = ReplaceStr(wscMsg, L"%item", HkGetWStringFromIDS(gi1->iIDSName).c_str());
				wscMsg = ReplaceStr(wscMsg, L"%second", HkGetWStringFromIDS(gi2->iIDSName).c_str());
				owned[iClientID] = wscMsg;
				StoreReturnPointForClient(iClientID);
				return;
			}
		}
	}

	if (foundamountedid == false)
	{
		wstring wscMsg = L"ERR You have no ID on your ship. You must have one.";
		owned[iClientID] = wscMsg;
		StoreReturnPointForClient(iClientID);
	}


	return;
}

bool SCI::CanBuyItem(uint iArchID, uint iClientID) {

	auto foundItem = shipclassitems.find(iArchID);
	if (foundItem != shipclassitems.end()) {
		Archetype::Ship* shipArch = Archetype::GetShip(Players[iClientID].iShipArchetype);
		for (uint allowedClass : foundItem->second.canmount) {
			if (allowedClass == shipArch->iShipClass) {
				return true;
			}
		}
		return false;
	}
	return true;
}

// based on conn plugin
void SCI::StoreReturnPointForClient(unsigned int client)
{
	// Use *LAST* player base rather than current one because base's BaseExit handler
	// could have run before us and killed the record of being docked on a POB before
	// we got to run CheckItems and StoreReturnPointForClient
	uint base = GetCustomLastBaseForClient(client);
	// It's not docked at a custom base, check for a regular base
	if (!base) {
		pub::Player::GetBase(client, base);
	}
	if (!base) {
		return;
	}

	player_last_base[client] = base;
}

// based on conn plugin
uint SCI::GetCustomLastBaseForClient(unsigned int client)
{
	// Pass to plugins incase this ship is docked at a custom base.
	CUSTOM_BASE_IS_DOCKED_STRUCT info;
	info.iClientID = client;
	info.iDockedBaseID = 0;
	Plugin_Communication(CUSTOM_BASE_IS_DOCKED, &info);
	return info.iDockedBaseID;
}

// based on conn plugin
void SCI::MoveClient(unsigned int client, unsigned int targetBase)
{
	// Ask that another plugin handle the beam.
	CUSTOM_BASE_BEAM_STRUCT info;
	info.iClientID = client;
	info.iTargetBaseID = targetBase;
	info.bBeamed = false;
	Plugin_Communication(CUSTOM_BASE_BEAM, &info);
	if (info.bBeamed)
		return;

	// No plugin handled it, do it ourselves.
	pub::Player::ForceLand(client, targetBase); // beam
}

void SCI::CheckOwned(unsigned int iClientID)
{
	//end = not found
	if (owned.find(iClientID) == owned.end())
	{
		//PrintUserCmdText(iClientID, L"DEBUG: Not found for ownage.");
	}
	//else = found
	else
	{
		PrintUserCmdText(iClientID, owned.find(iClientID)->second.c_str());
		MoveClient(iClientID, player_last_base[iClientID]);
		//PrintUserCmdText(iClientID, L"DEBUG: Owned. Sending you back to base.");
		owned.erase(iClientID);
		player_last_base.erase(iClientID);
	}
}

void SCI::ClearClientInfo(uint iClientID)
{
	owned.erase(iClientID);
	clientplayerid.erase(iClientID);
}

void SCI::UpdatePlayerID(unsigned int iClientID)
{
	auto clientId = ClientInfo[iClientID].playerID;

	if (!iddock.count(clientId))
	{
		return;
	}

	pinfo info;
	info.playerid = clientId;
	info.shiparch = Players[iClientID].iShipArchetype;

	Archetype::Ship *ship = Archetype::GetShip(info.shiparch);
	info.maxholdsize = ship->fHoldSize;
	info.shipclass = ship->iShipClass;

	clientplayerid[iClientID] = info;
	
}

bool SCI::CanDock(uint iDockTarget, uint iClientID)
{
	auto clientInfo = clientplayerid.find(iClientID);
	//First we check if the player is on our watchlist.
	if (clientInfo == clientplayerid.end())
	{
		return true;
	}
	
	//PrintUserCmdText(iClientID, L"DEBUG: Found you");
	//temporarily copy the id so we don't mapception
	uint id = clientInfo->second.playerid;
	uint currsystem = Players[iClientID].iSystemID;
	uint currship = Players[iClientID].iShipArchetype;

	auto idDataIter = iddock.find(id);
	if (idDataIter == iddock.end())
	{
		return true;
	}

	auto idData = idDataIter->second;

	uint iTypeID;
	pub::SpaceObj::GetType(iDockTarget, iTypeID);

	if (!(iTypeID & (DockingRing | Station)))
	{
		return true;
	}

	if (idData.type == 3) // cargo restriction
	{
		if (idData.exempt.count(currship))
		{
			return true;
		}

		if (!idData.systems.count(currsystem))
		{
			return true;
		}

		//we check the cargo restriction first as that should iron out a good chunk of them
		if (clientInfo->second.maxholdsize > idData.cargo)
		{
			PrintUserCmdText(iClientID, L"Cargo Hold is over the authorized capacity. Docking Denied.");
			return false;
		}

		uint currshipclass = clientInfo->second.shipclass;
		if (idData.shipclasses.count(currshipclass))
		{
			PrintUserCmdText(iClientID, L"This ship class is not allowed to dock in this system. Docking Denied.");

			return false;
		}
	}

	if (idData.type == 2) // iffRestriction
	{
		auto iobj = HkGetInspectObj(iDockTarget);
		if (!iobj)
		{
			return true;
		}

		CSolar* csolar = (CSolar*)iobj->cobj;
		uint affiliation;
		Reputation::Vibe::GetAffiliation(csolar->repVibe, affiliation, false);
		if (csolar->isDynamic)
		{
			if (!idData.iff.count(affiliation))
			{
				PrintUserCmdText(iClientID, L"Your ID cannot dock on Modular Bases of this IFF");
				return false;
			}
		}

		if (!idData.iff.count(affiliation) && !idData.bases.count(csolar->dockTargetId))
		{
			PrintUserCmdText(iClientID, idData.dockMessage.c_str());
			return false;
		}
	}
	return true;
}
