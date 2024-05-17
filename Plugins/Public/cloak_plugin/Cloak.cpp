/**
 Cloak (Yet another) Docking Plugin for FLHook-Plugin
 by Cannon.

0.1:
 Initial release
*/

// includes 

#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <list>
#include <map>
#include <algorithm>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include "Cloak.h"
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include "minijson_writer.hpp"

#include <thread>
#include <atomic>
#include <mutex>

static int set_iPluginDebug = 0;
static bool cloakStateChanged = true;
static string filePath = "c:/stats/player_cloak_status.json";
bool set_enableCloakSystemOverride = false;

uint jumpingPlayers[MAX_CLIENT_ID + 1];

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

enum INFO_STATE
{
	STATE_CLOAK_INVALID = 0,
	STATE_CLOAK_OFF = 1,
	STATE_CLOAK_CHARGING = 2,
	STATE_CLOAK_ON = 3,
};

struct CLOAK_FUEL_USAGE
{
	float usageStatic = 0;
	float usageLinear = 0;
	float usageSquare = 0;
};

struct CLOAK_ARCH
{
	string scNickName;
	int iWarmupTime;
	int activationPeriod;
	uint availableShipClasses;
	unordered_map<uint, CLOAK_FUEL_USAGE> mapFuelToUsage;
	bool bDropShieldsOnUncloak;
	bool bBreakOnProximity;
	float fRange;
};

struct CLOAK_INFO
{
	CLOAK_INFO()
	{

		iCloakSlot = 0;
		tmCloakTime = 0;
		iState = STATE_CLOAK_INVALID;
		bAdmin = false;

		arch = nullptr;
	}

	ushort iCloakSlot;
	mstime tmCloakTime;
	uint iState;
	float fuelUsageCounter = 0;
	bool bAdmin;
	uint DisruptTime;

	CLOAK_ARCH* arch;
};

struct CDSTRUCT
{
	float range = 0.0f;
	int cooldown = 5;
	uint ammotype = 0;
	uint ammoamount = 0;
	uint effect = 0;
	float effectlifetime = 0.0f;
};

struct CLIENTCDSTRUCT
{
	int cdwn;
	CDSTRUCT cd;
};

static unordered_map<uint, CLOAK_INFO> mapClientsCloak;
static unordered_map<uint, CLIENTCDSTRUCT> mapClientsCD;

static map<uint, CLOAK_ARCH> mapCloakingDevices;
static map<uint, CDSTRUCT> mapCloakDisruptors;

static uint cloakAlertSound = CreateID("cloak_osiris");

struct OBSCURED_SYSTEM
{
	string nickname;
	uint hash;
};
static unordered_map<uint, OBSCURED_SYSTEM> mapObscuredSystems;

void LoadSettings();

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
	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\cloak.cfg";

	int cloakamt = 0;
	int cdamt = 0;
	INI_Reader ini;
	if (ini.open(scPluginCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("Cloak"))
			{
				CLOAK_ARCH device;
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						device.scNickName = ini.get_value_string(0);
					}
					else if (ini.is_value("warmup_time"))
					{
						device.iWarmupTime = ini.get_value_int(0);
					}
					else if (ini.is_value("ship_classes"))
					{
						uint types = 0;
						string typeStr = ToLower(ini.get_value_string(0));
						if (typeStr.find("fighter") != string::npos)
							types |= Fighter;
						if (typeStr.find("freighter") != string::npos)
							types |= Freighter;
						if (typeStr.find("transport") != string::npos)
							types |= Transport;
						if (typeStr.find("gunboat") != string::npos)
							types |= Gunboat;
						if (typeStr.find("cruiser") != string::npos)
							types |= Cruiser;
						if (typeStr.find("capital") != string::npos)
							types |= Capital;

						device.availableShipClasses = types;
					}
					else if (ini.is_value("fuel"))
					{
						CLOAK_FUEL_USAGE fuelUsage;
						string scNickName = ini.get_value_string(0);
						fuelUsage.usageStatic = ini.get_value_float(1);
						fuelUsage.usageLinear = ini.get_value_float(2);
						fuelUsage.usageSquare = ini.get_value_float(3);
						device.mapFuelToUsage[CreateID(scNickName.c_str())] = fuelUsage;
					}
					else if (ini.is_value("drop_shields_on_uncloak"))
					{
						device.bDropShieldsOnUncloak = ini.get_value_bool(0);
					}
					else if (ini.is_value("break_cloak_on_proximity"))
					{
						device.bBreakOnProximity = ini.get_value_bool(0);
					}
					else if (ini.is_value("detection_range")) {
						device.fRange = ini.get_value_float(0);
					}
				}
				auto cloakArch = reinterpret_cast<Archetype::CloakingDevice*>(Archetype::GetEquipment(CreateID(device.scNickName.c_str())));
				if (!cloakArch)
				{
					ConPrint(L"Problem loading config for cloak %ls", stows(device.scNickName).c_str());
					continue;
				}
				device.activationPeriod = static_cast<int>(ceil(cloakArch->fCloakinTime * 1000));
				mapCloakingDevices[CreateID(device.scNickName.c_str())] = device;
				++cloakamt;
			}
			else if (ini.is_header("Disruptor"))
			{
				CDSTRUCT disruptor;
				uint hash;
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						hash = CreateID(ini.get_value_string());
					}
					else if (ini.is_value("cooldown_time"))
					{
						disruptor.cooldown = ini.get_value_int(0);
					}
					else if (ini.is_value("range"))
					{
						disruptor.range = ini.get_value_float(0);
					}
					else if (ini.is_value("ammo"))
					{
						disruptor.ammotype = CreateID(ini.get_value_string(0));
						disruptor.ammoamount = ini.get_value_int(1);
					}
					else if (ini.is_value("fx"))
					{
						disruptor.effect = CreateID(ini.get_value_string());
					}
					else if (ini.is_value("effectlifetime"))
					{
						disruptor.effectlifetime = ini.get_value_float(0);
					}
				}
				mapCloakDisruptors[hash] = disruptor;
				++cdamt;
			}
			else if (ini.is_header("General")) {
				while (ini.read_value())
				{
					if (ini.is_value("cloak_alert_sound"))
					{
						cloakAlertSound = CreateID(ini.get_value_string());
					}
					else if (ini.is_value("enable_system_override_and_logging"))
					{
						set_enableCloakSystemOverride = ini.get_value_bool(0);
					}
				}
			}
			else if (ini.is_header("PlayerListCloak"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("mask"))
					{
						mapObscuredSystems[CreateID(ini.get_value_string(0))] = { ini.get_value_string(1), CreateID(ini.get_value_string(1)) };
					}
				}
			}
		}
		ini.close();
	}

	ConPrint(L"CLOAK: Loaded %u cloaking devices \n", cloakamt);
	ConPrint(L"CLOAK: Loaded %u cloak disruptors \n", cdamt);

	struct PlayerData *pd = 0;
	while (pd = Players.traverse_active(pd))
	{
	}
}

void ClearClientInfo(uint iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	mapClientsCloak.erase(iClientID);
}

void SetCloak(uint iClientID, uint iShipID, bool bOn)
{
	XActivateEquip ActivateEq;
	ActivateEq.bActivate = bOn;
	ActivateEq.iSpaceID = iShipID;
	ActivateEq.sID = mapClientsCloak[iClientID].iCloakSlot;
	Server.ActivateEquip(iClientID, ActivateEq);
}

void ObscureSystemList(uint clientId)
{
	if (!set_enableCloakSystemOverride)
	{
		return;
	}
	uint system = Players[clientId].iSystemID;
	if (mapObscuredSystems.count(system))
	{
		Players.SendSystemID(clientId, mapObscuredSystems.at(system).hash);
	}
	else
	{
		PrintUserCmdText(clientId, L"ERR unable to mask your system (%08x) on playerlist, contact admins", system);
	}
}

void RestoreSystemList(uint clientId)
{
	if (!set_enableCloakSystemOverride)
	{
		return;
	}
	Players.SendSystemID(clientId, Players[clientId].iSystemID);
}

void SetState(uint iClientID, uint iShipID, int iNewState)
{
	auto& cloakInfo = mapClientsCloak.at(iClientID);
	if (cloakInfo.iState != iNewState)
	{
		if (cloakInfo.iState == STATE_CLOAK_ON || iNewState == STATE_CLOAK_ON)
		{
			cloakStateChanged = true;
		}

		cloakInfo.iState = iNewState;
		cloakInfo.tmCloakTime = timeInMS();
		cloakInfo.fuelUsageCounter = 0.0f;
		CLIENT_CLOAK_STRUCT communicationInfo;
		switch (iNewState)
		{
		case STATE_CLOAK_CHARGING:
		{
			communicationInfo.iClientID = iClientID;
			communicationInfo.isChargingCloak = true;
			communicationInfo.isCloaked = false;
			Plugin_Communication(CLIENT_CLOAK_INFO, &communicationInfo);

			PrintUserCmdText(iClientID, L"Preparing to cloak...");
			break;
		}

		case STATE_CLOAK_ON:
		{
			communicationInfo.iClientID = iClientID;
			communicationInfo.isChargingCloak = false;
			communicationInfo.isCloaked = true;
			Plugin_Communication(CLIENT_CLOAK_INFO, &communicationInfo);

			PrintUserCmdText(iClientID, L" Cloaking device on");
			SetCloak(iClientID, iShipID, true);
			PrintUserCmdText(iClientID, L"Cloaking device on");
			ClientInfo[iClientID].bCloaked = true;
			ObscureSystemList(iClientID);
			break;
		}
		case STATE_CLOAK_OFF:
		default:
		{
			communicationInfo.iClientID = iClientID;
			communicationInfo.isChargingCloak = false;
			communicationInfo.isCloaked = false;
			Plugin_Communication(CLIENT_CLOAK_INFO, &communicationInfo);

			PrintUserCmdText(iClientID, L" Cloaking device off");
			SetCloak(iClientID, iShipID, false);
			PrintUserCmdText(iClientID, L"Cloaking device off");
			ClientInfo[iClientID].bCloaked = false;
			RestoreSystemList(iClientID);
			break;
		}
		}
	}
}

// Returns false if the ship has no fuel to operate its cloaking device.
static bool ProcessFuel(uint iClientID, CLOAK_INFO &info, uint iShipID)
{
	if (info.bAdmin)
		return true;

	CShip* cship = ClientInfo[iClientID].cship;
	if (!cship)
	{
		return true;
	}
	if (jumpingPlayers[iClientID])
	{
		--jumpingPlayers[iClientID];
		return true;
	}
	CEquipTraverser tr(EquipmentClass::Cargo);
	CECargo* cargo;
	while (cargo = reinterpret_cast<CECargo*>(cship->equip_manager.Traverse(tr)))
	{
		CLOAK_FUEL_USAGE* fuelUsage;
		if (!info.arch->mapFuelToUsage.count(cargo->archetype->iArchID))
		{
			continue;
		}
		else
		{
			fuelUsage = &info.arch->mapFuelToUsage.at(cargo->archetype->iArchID);
		}

		float currFuelUsage = fuelUsage->usageStatic;
		if (fuelUsage->usageLinear != 0.0f || fuelUsage->usageSquare != 0.0f)
		{
			Vector dir1 = cship->get_velocity();
			float vecLength = sqrtf(dir1.x * dir1.x + dir1.y * dir1.y + dir1.z * dir1.z);

			currFuelUsage += fuelUsage->usageLinear * vecLength;
			currFuelUsage += fuelUsage->usageSquare * vecLength * vecLength;
		}
		info.fuelUsageCounter += currFuelUsage;
		uint totalFuelUsage = static_cast<uint>(max(info.fuelUsageCounter, 0.0f));
		if (cargo->count >= totalFuelUsage)
		{
			if (totalFuelUsage >= 25) // Wait until the fuel usage reaches 25 to actually call RemoveCargo, as it's an expensive operation.
			{
				info.fuelUsageCounter -= static_cast<float>(totalFuelUsage);
				pub::Player::RemoveCargo(iClientID, cargo->iSubObjId, totalFuelUsage);
			}
			return true;
		}
	}

	return false;
}

void InitCloakInfo(uint client, uint distance)
{
	wchar_t buf[50];
	_snwprintf(buf, sizeof(buf), L" InitCloakInfo %u", distance);
	HkFMsg(client, L"<TEXT>" + XMLText(buf) + L"</TEXT>");
}

void __stdcall PlayerLaunch_AFTER(unsigned int iShip, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;

	CShip* cship = ClientInfo[iClientID].cship;
	if (!cship)
	{
		return;
	}

	jumpingPlayers[iClientID] = 0;
	CEquipTraverser disrTrav(EquipmentClass::CM);

	CEquip* disruptorEq;
	while (disruptorEq = cship->equip_manager.Traverse(disrTrav))
	{
		if (mapCloakDisruptors.count(disruptorEq->archetype->iArchID))
		{
			CDSTRUCT cd;
			CLIENTCDSTRUCT cdclient;

			cdclient.cdwn = 0;
			cdclient.cd = mapCloakDisruptors[disruptorEq->archetype->iArchID];

			mapClientsCD[iClientID] = cdclient;
			break;
		}
	}

	//Legacy code for the cloaks, needs to be rewritten at some point. - Alley


	CEquip* cloakEq = cship->equip_manager.FindFirst(EquipmentClass::CloakingDevice);
	if (!cloakEq)
	{
		InitCloakInfo(iClientID, 0);
		return;
	}
	if (!mapCloakingDevices.count(cloakEq->EquipArch()->iArchID))
	{
		InitCloakInfo(iClientID, 0);
		return;
	}

	auto& cloakInfo = mapClientsCloak[iClientID];
	cloakInfo.iCloakSlot = cloakEq->GetID();
	cloakInfo.arch = &mapCloakingDevices.at(cloakEq->EquipArch()->iArchID);
	cloakInfo.DisruptTime = 0;
	cloakInfo.iState = STATE_CLOAK_INVALID;
	SetState(iClientID, iShip, STATE_CLOAK_OFF);
	if (cloakInfo.arch && cloakInfo.arch->bBreakOnProximity && !cloakInfo.bAdmin)
	{
		InitCloakInfo(iClientID, static_cast<uint>(cloakInfo.arch->fRange));
	}
	else
	{
		InitCloakInfo(iClientID, 0);
	}
}

void BaseEnter(unsigned int iBaseID, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	if (mapClientsCloak.count(iClientID) && mapClientsCloak.at(iClientID).iState == STATE_CLOAK_ON)
	{
		RestoreSystemList(iClientID);
	}
	mapClientsCloak.erase(iClientID);
	mapClientsCD.erase(iClientID);
}

void HkTimerCheckKick()
{
	returncode = DEFAULT_RETURNCODE;
	mstime now = timeInMS();
	uint timeNow = time(0);

	for (auto& ci = mapClientsCloak.begin(); ci != mapClientsCloak.end(); ++ci)
	{
		uint iClientID = ci->first;
		uint iShipID = Players[iClientID].iShipID;
		CLOAK_INFO &info = ci->second;

		//code to check if the player is disrupted. We run this separately to not cause issues with the bugfix
		//first we check if it's 0. If it is, it's useless to process the other conditions, even if it means an additional check to begin with.
		if (info.DisruptTime)
		{
			if (!--info.DisruptTime)
			{
				PrintUserCmdText(iClientID, L"Cloaking Device rebooted.");
			}
		}

		if (iShipID)
		{
			switch (info.iState)
			{
			case STATE_CLOAK_OFF:
				if (timeNow % 3 == 0)
				{
					// Send cloak state for uncloaked cloak-able players (only for them in space)  
					// this is the code to fix the bug where players wouldnt always see uncloaked players  
					XActivateEquip ActivateEq;
					ActivateEq.bActivate = false;
					ActivateEq.iSpaceID = iShipID;
					ActivateEq.sID = info.iCloakSlot;
					Server.ActivateEquip(iClientID, ActivateEq);
				}
				break;
			case STATE_CLOAK_CHARGING:
				if (!ProcessFuel(iClientID, info, iShipID))
				{
					PrintUserCmdText(iClientID, L"Cloaking device shutdown, no fuel");
					SetState(iClientID, iShipID, STATE_CLOAK_OFF);
				}
				else if ((info.tmCloakTime + info.arch->iWarmupTime) < now)
				{
					SetState(iClientID, iShipID, STATE_CLOAK_ON);
				}
				else if (info.arch->bDropShieldsOnUncloak && !info.bAdmin)
				{
					pub::SpaceObj::DrainShields(iShipID);
				}
				break;

			case STATE_CLOAK_ON:
				if (!ProcessFuel(iClientID, info, iShipID) && info.tmCloakTime + info.arch->activationPeriod < now)
				{
					PrintUserCmdText(iClientID, L"Cloaking device shutdown, no fuel");
					SetState(iClientID, iShipID, STATE_CLOAK_OFF);
				}
				else if (info.arch->bDropShieldsOnUncloak && !info.bAdmin)
				{
					pub::SpaceObj::DrainShields(iShipID);
				}
				break;
			}
		}
	}
	for (auto& cd = mapClientsCD.begin(); cd != mapClientsCD.end(); ++cd)
	{
		if (cd->second.cdwn >= 2)
		{
			--cd->second.cdwn;
		}
		else if (cd->second.cdwn == 1)
		{
			PrintUserCmdText(cd->first, L"Cloak Disruptor cooldown complete.");
			IObjInspectImpl *obj = HkGetInspect(cd->first);
			if (obj)
			{
				HkUnLightFuse((IObjRW*)obj, cd->second.cd.effect, 0.0f);
			}
			cd->second.cdwn = 0;
		}
	}

	if (set_enableCloakSystemOverride && cloakStateChanged && timeNow%5 == 0)
	{
		stringstream stream;
		minijson::object_writer writer(stream);
		string sPlayer = "players";
		minijson::object_writer pwc = writer.nested_object(sPlayer.c_str());

		for (auto& iter : mapClientsCloak)
		{
			if (iter.second.iState != STATE_CLOAK_ON)
			{
				continue;
			}
			uint playerSystem = Players[iter.first].iSystemID;
			if (!mapObscuredSystems.count(playerSystem))
			{
				continue;
			}
			string playerName = wstos((const wchar_t*)Players.GetActiveCharacterName(iter.first));
			minijson::object_writer pw = pwc.nested_object(playerName.c_str());
			pw.write("system", mapObscuredSystems.at(playerSystem).nickname.c_str());
			pw.close();
		}
		pwc.close();
		writer.close();

		FILE* file = fopen(filePath.c_str(), "w");
		if (file)
		{
			fprintf(file, "%s", stream.str().c_str());
			fclose(file);
		}
		cloakStateChanged = false;
	}

}

void CloakDisruptor(uint iClientID)
{
	auto cship = ClientInfo[iClientID].cship;
	if (!cship)
	{
		return;
	}
	Vector& pos = cship->vPos;
	
	uint iSystem = Players[iClientID].iSystemID;

	// For all players in system...
	struct PlayerData *pPD = nullptr;
	while (pPD = Players.traverse_active(pPD))
	{
		// Get the this player's current system and location in the system.
		uint client2 = pPD->iOnlineID;
		if (iSystem != pPD->iSystemID)
			continue;

		CShip* cShip = ClientInfo[pPD->iOnlineID].cship;
		if (!cShip)
			continue;

		// Is player within the specified range of the sending char.
		if (HkDistance3D(pos, cShip->vPos) > mapClientsCD[iClientID].cd.range)
			continue;

		//we check if that character has a cloaking device.
		auto& cloakInfoIter = mapClientsCloak.find(client2);
		if (cloakInfoIter == mapClientsCloak.end())
		{
			continue;
		}
		
		auto& cloakInfo = cloakInfoIter->second;

		//if it's an admin, do nothing. Doing it this way fixes the ghost bug.
		if (!cloakInfo.bAdmin)
		{
			mstime now = timeInMS();
			//check if the cloak is charging or is already on
			if (cloakInfo.iState == STATE_CLOAK_CHARGING 
				|| (cloakInfo.iState == STATE_CLOAK_ON && (cloakInfo.tmCloakTime + cloakInfo.arch->activationPeriod) < now))
			{
				SetState(client2, cShip->id, STATE_CLOAK_OFF);
				pub::Audio::PlaySoundEffect(client2, CreateID("cloak_osiris"));
				cloakInfo.DisruptTime = mapClientsCD[iClientID].cd.cooldown;
				PrintUserCmdText(client2, L"Alert: Cloaking device disruption field detected.");
				PrintUserCmdText(client2, L"Cloak rebooting... %u seconds remaining.", cloakInfo.DisruptTime);
			}
		}
	}
}

void SetFuse(uint iClientID, uint fuse, float lifetime)
{
	CDSTRUCT &cd = mapClientsCD[iClientID].cd;

	IObjInspectImpl *obj = HkGetInspect(iClientID);
	if (obj)
	{
		HkLightFuse((IObjRW*)obj, fuse, 0.0f, lifetime, 0.0f);
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool UserCmd_Cloak(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	const CShip* cship = ClientInfo[iClientID].cship;
	if (!cship)
	{
		PrintUserCmdText(iClientID, L"Not in space");
		return true;
	}

	auto& infoIter = mapClientsCloak.find(iClientID);

	if (infoIter == mapClientsCloak.end() || !infoIter->second.arch)
	{
		PrintUserCmdText(iClientID, L"Cloaking device not available");
		return true;
	}

	auto& info = infoIter->second;

	if (info.DisruptTime)
	{
		PrintUserCmdText(iClientID, L"Cloaking Device Disrupted. Please wait %u seconds", info.DisruptTime);
		return true;
	}

	uint type =	cship->archetype->iArchType;
	
	if (!(info.arch->availableShipClasses & type))
	{
		PrintUserCmdText(iClientID, L"Cloaking device will not function on this ship type");
		info.iState = STATE_CLOAK_INVALID;
		SetState(iClientID, cship->id, STATE_CLOAK_OFF);
		return true;
	}

	info.bAdmin = false;

	switch (info.iState)
	{
	case STATE_CLOAK_OFF:
		SetState(iClientID, cship->id, STATE_CLOAK_CHARGING);
		break;
	case STATE_CLOAK_ON:
	{
		mstime now = timeInMS();
		if ((info.tmCloakTime + info.arch->activationPeriod) < now)
		{
			SetState(iClientID, cship->id, STATE_CLOAK_OFF);
		}
		else
		{
			PrintUserCmdText(iClientID, L"ERR Device must fully activate before deactivation.");
		}
		break;
	}
	case STATE_CLOAK_CHARGING:
		SetState(iClientID, cship->id, STATE_CLOAK_OFF);
		break;
	}
	
	return true;
}

bool UserCmd_Disruptor(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	//not found
	auto cd = mapClientsCD.find(iClientID);
	if (mapClientsCD.find(iClientID) == mapClientsCD.end())
	{
		PrintUserCmdText(iClientID, L"Cloak Disruptor not found.");
	}
	//found
	else
	{
		if (cd->second.cdwn != 0)
		{
			PrintUserCmdText(iClientID, L"Cloak Disruptor recharging. %d seconds left.", cd->second.cdwn);
		}
		else
		{
			//bugfix, initial status assumes every player don't have ammo.
			bool foundammo = false;

			CShip* cship = ClientInfo[iClientID].cship;
			if (!cship)
			{
				PrintUserCmdText(iClientID, L"Ship not in space!");
				return true;
			}
			CEquipTraverser tr(EquipmentClass::Cargo);
			CECargo* cargo;
			while (cargo = reinterpret_cast<CECargo*>(cship->equip_manager.Traverse(tr)))
			{
				if (cargo->archetype->iArchID == cd->second.cd.ammotype)
				{
					if (cargo->count >= cd->second.cd.ammoamount)
					{
						pub::Player::RemoveCargo(iClientID, cargo->iSubObjId, cd->second.cd.ammoamount);
						foundammo = true;
					}
					break;
				}
			}

			if (foundammo == false)
			{
				PrintUserCmdText(iClientID, L"ERR Not enough batteries.");
				return true;
			}

			IObjInspectImpl *obj = HkGetInspect(iClientID);
			if (obj)
			{
				HkLightFuse((IObjRW*)obj, cd->second.cd.effect, 0, cd->second.cd.effectlifetime, 0);
			}

			pub::Audio::PlaySoundEffect(iClientID, CreateID("cloak_osiris"));
			CloakDisruptor(iClientID);
			cd->second.cdwn = cd->second.cd.cooldown;
			PrintUserCmdText(iClientID, L"Cloak Disruptor engaged. Cooldown: %d seconds.", cd->second.cdwn);
		}
	}

	return true;
}

void CloakAlert(CUSTOM_CLOAK_ALERT_STRUCT* data)
{
	for (uint clientId : data->alertedGroupMembers)
	{
		pub::Audio::PlaySoundEffect(clientId, cloakAlertSound);
	}
}



typedef bool(*_UserCmdProc)(uint, const wstring &, const wstring &, const wchar_t*);

struct USERCMD
{
	wchar_t *wszCmd;
	_UserCmdProc proc;
	wchar_t *usage;
};

USERCMD UserCmds[] =
{
	{ L"/cloak", UserCmd_Cloak, L"Usage: /cloak"},
	{ L"/cloak*", UserCmd_Cloak, L"Usage: /cloak"},
	{ L"/disruptor", UserCmd_Disruptor, L"Usage: /disruptor"},
	{ L"/disruptor*", UserCmd_Disruptor, L"Usage: /disruptor"},

};

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

#define IS_CMD(a) !wscCmd.compare(L##a)

bool ExecuteCommandString_Callback(CCmds* cmds, const wstring &wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	if (IS_CMD("cloak"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;

		uint iClientID = HkGetClientIdFromCharname(cmds->GetAdminName());
		
		if (!(cmds->rights & RIGHT_CLOAK)) { cmds->Print(L"ERR No permission\n"); return true; }
		
		if (iClientID == -1)
		{
			cmds->Print(L"ERR On console");
			return true;
		}

		uint iShip;
		pub::Player::GetShip(iClientID, iShip);
		if (!iShip)
		{
			PrintUserCmdText(iClientID, L"ERR Not in space");
			return true;
		}

		auto& cloakData = mapClientsCloak.find(iClientID);
		if (cloakData == mapClientsCloak.end())
		{
			cmds->Print(L"ERR Cloaking device not available");
			return true;
		}

		switch (cloakData->second.iState)
		{
		case STATE_CLOAK_OFF:
			cloakData->second.bAdmin = true;
			InitCloakInfo(iClientID, 0);
			SetState(iClientID, iShip, STATE_CLOAK_ON);
			break;
		case STATE_CLOAK_CHARGING:
		case STATE_CLOAK_ON:
			cloakData->second.bAdmin = true;
			InitCloakInfo(iClientID, 0);
			SetState(iClientID, iShip, STATE_CLOAK_OFF);
			break;
		}
		return true;
	}
	return false;
}

void __stdcall ExplosionHit(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg)
{
	returncode = DEFAULT_RETURNCODE;

	if (dmg->get_cause() != DamageCause::CruiseDisrupter && dmg->get_cause() != DamageCause::UnkDisrupter)
		return;

	CShip* cShip = reinterpret_cast<CShip*>(iobj->cobj);
	uint client = cShip->ownerPlayer;
	if (!client)
		return;

	auto& cloakData = mapClientsCloak.find(client);
	if (cloakData == mapClientsCloak.end())
	{
		return;
	}

	if (!cloakData->second.bAdmin
		&& cloakData->second.iState == STATE_CLOAK_CHARGING)
	{
		SetState(client, cShip->id, STATE_CLOAK_OFF);
	}
}

void __stdcall JumpInComplete_AFTER(unsigned int iSystem, unsigned int iShip)
{
	returncode = DEFAULT_RETURNCODE;
	uint iClientID = HkGetClientIDByShip(iShip);

	if (!iClientID)
	{
		return;
	}

	jumpingPlayers[iClientID] = 5;

	auto& cloakData = mapClientsCloak.find(iClientID);
	if (cloakData == mapClientsCloak.end())
	{
		return;
	}

	if (cloakData->second.iState == STATE_CLOAK_CHARGING)
	{
		SetState(iClientID, iShip, STATE_CLOAK_OFF);
		pub::Audio::PlaySoundEffect(iClientID, CreateID("cloak_osiris"));
		PrintUserCmdText(iClientID, L"Alert: Cloaking device overheat detected. Shutting down.");

		wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);
		HkAddCheaterLog(wscCharname, L"Switched system while under cloak charging mode");
	}
}

int __cdecl Dock_Call(unsigned int const &iShip, unsigned int const &iDockTarget, int& iCancel, enum DOCK_HOST_RESPONSE& response)
{
	returncode = DEFAULT_RETURNCODE;

	if (((response == PROCEED_DOCK || response == DOCK) && iCancel != -1))
	{
		return 0;
	}

	uint client = HkGetClientIDByShip(iShip);
	if (!client)
	{
		return 0;
	}
	// If the last jump happened within 60 seconds then prohibit the docking
	// on a jump hole or gate.
	uint iTypeID;
	pub::SpaceObj::GetType(iDockTarget, iTypeID);
	if (!(iTypeID & (JumpGate | JumpHole)))
	{
		return 0;
	}
	
	auto& cloakInfo = mapClientsCloak.find(client);
	if (cloakInfo == mapClientsCloak.end())
	{
		return 0;
	}

	if (cloakInfo->second.iState == STATE_CLOAK_CHARGING)
	{
		SetState(client, iShip, STATE_CLOAK_OFF);
		pub::Audio::PlaySoundEffect(client, CreateID("cloak_osiris"));
		PrintUserCmdText(client, L"Alert: Cloaking device overheat detected. Shutting down.");

		wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(client);
		HkAddCheaterLog(wscCharname, L"About to enter a JG/JH while under cloak charging mode");
		return 0;
	}
	else if (cloakInfo->second.iState == STATE_CLOAK_ON)
	{
		jumpingPlayers[client] = 20;
	}

	return 0;
}

void Plugin_Communication_CallBack(PLUGIN_MESSAGE msg, void* data)
{
	returncode = DEFAULT_RETURNCODE;
	if (msg == CUSTOM_CLOAK_ALERT)
	{
		returncode = SKIPPLUGINS;
		CUSTOM_CLOAK_ALERT_STRUCT* info = reinterpret_cast<CUSTOM_CLOAK_ALERT_STRUCT*>(data);
		CloakAlert(info);
	}
	else if (msg == CUSTOM_CLOAK_CHECK)
	{
		returncode = SKIPPLUGINS;
		CUSTOM_CLOAK_CHECK_STRUCT* info = reinterpret_cast<CUSTOM_CLOAK_CHECK_STRUCT*>(data);
		auto& cloakData = mapClientsCloak.find(info->clientId);
		if (cloakData != mapClientsCloak.end())
		{
			auto cloakState = cloakData->second.iState;
			if (cloakState == STATE_CLOAK_CHARGING || cloakState == STATE_CLOAK_ON)
			{
				info->isCloaked = true;
			}
		}
	}
}

void SwitchOutComplete(uint ship, uint clientId)
{
	returncode = DEFAULT_RETURNCODE;

	auto& info = mapClientsCloak.find(clientId);
	if (info == mapClientsCloak.end() || info->second.iState == STATE_CLOAK_ON)
	{
		ObscureSystemList(clientId);
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Functions to hook */
EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Cloak Plugin by cannon";
	p_PI->sShortName = "cloak";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ClearClientInfo, PLUGIN_ClearClientInfo, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch_AFTER, PLUGIN_HkIServerImpl_PlayerLaunch_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseEnter, PLUGIN_HkIServerImpl_BaseEnter, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&HkTimerCheckKick, PLUGIN_HkTimerCheckKick, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExplosionHit, PLUGIN_ExplosionHit, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&JumpInComplete_AFTER, PLUGIN_HkIServerImpl_JumpInComplete_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Dock_Call, PLUGIN_HkCb_Dock_Call, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&SwitchOutComplete, PLUGIN_HkIServerImpl_SystemSwitchOutComplete_AFTER, 0));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Plugin_Communication_CallBack, PLUGIN_Plugin_Communication, 0));

	return p_PI;
}
