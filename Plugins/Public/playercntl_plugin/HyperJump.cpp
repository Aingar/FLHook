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
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

#include <PluginUtilities.h>
#include "Main.h"
#include <hookext_exports.h>

#define RIGHT_CHECK(a) if(!(cmds->rights & a)) { cmds->Print(L"ERR No permission\n"); return; }

namespace HyperJump
{
	// Check that the item is a ship, cargo or equipment item is valid
	static uint CreateValidID(const char *nickname)
	{
		uint item = CreateID(nickname);

		if (!Archetype::GetEquipment(item)
			&& !Archetype::GetSimple(item)
			&& !Archetype::GetShip(item))
		{
			ConPrint(L"ERROR: item '%s' is not valid\n", stows(nickname).c_str());
		}

		return item;
	}

	// Ships restricted from jumping
	static set<uint> jumpRestrictedShipsList;

	static set<uint> setCloakingClients;

	void ClientCloakCallback(CLIENT_CLOAK_STRUCT* info)
	{
		if (info->isChargingCloak || info->isCloaked)
		{
			setCloakingClients.insert(info->iClientID);
		}
		else
		{
			setCloakingClients.erase(info->iClientID);
		}
	}

	static unordered_map<uint, unordered_map<uint, vector<uint>>> mapAvailableJumpSystems;

	static uint BeaconCommodity = 0;
	static float JumpCargoSizeRestriction = 7000;
	static uint set_blindJumpOverrideSystem = 0;
	static uint set_jumpInPvPInvulnerabilityPeriod = 5000;
	static uint set_exitJumpHoleLoadout = CreateID("wormhole_unstable");
	static uint set_exitJumpHoleArchetype = CreateID("jumphole_noentry");
	static uint set_entryJumpHoleLoadout = CreateID("wormhole_unstable");
	static uint set_entryJumpHoleArchetype = CreateID("flhook_jumphole");
	static uint set_IDSNamespaceStart = 267200;
	static uint set_maxJumpRange = 0;
	static unordered_set<uint> set_banJumpSystems;

	struct JD_JUMPHOLE
	{
		uint objId;
		uint targetSystem;
		time_t timeout;
		boolean isEntrance;
		uint pairedJH;
		set<uint> dockingQueue;
		uint remainingCapacity = UINT_MAX; // if undefined, unlimited jumps allowed
	};
	static unordered_map<uint, JD_JUMPHOLE> jumpObjMap;
	static unordered_map<uint, uint> shipToJumpObjMap;

	struct JUMPFUSE
	{
		uint jump_fuse = 0;
		float lifetime = 0.0f;
		float delay = 0.0f;
	};
	
	// map<shipclass, map<JH/JD type, JUMPFUSE>> 
	static unordered_map<uint, map<JUMP_TYPE, JUMPFUSE>> JumpInFuseMap;

	struct SYSTEMJUMPCOORDS
	{
		uint system;
		wstring sector;
		Vector pos;
		Matrix ornt;
	};
	static unordered_map<uint, vector<SYSTEMJUMPCOORDS>> mapSystemJumps;
	static unordered_map<uint, SYSTEMJUMPCOORDS> mapDeferredJumps;

	struct JUMPDRIVE_ARCH
	{
		uint nickname;
		float can_jump_charge;
		float charge_rate;
		vector<uint> charge_fuse;
		uint jump_fuse;
		uint jump_range;
		uint jump_hole_capacity;
		uint jump_hole_duration;
		map<uint, vector<uint>> mapFuelToUsagePerDistance;
		uint available_ship_classes;
		boolean cd_disrupts_charge;
	};
	static unordered_map<uint, JUMPDRIVE_ARCH> mapJumpDriveArch;

	struct JUMPDRIVE
	{
		JUMPDRIVE_ARCH* arch = nullptr;

		bool charging_on;
		float curr_charge;
		uint active_fuse;
		float active_fuse_delay;
		uint last_tick_charge;
		list<uint> active_charge_fuse;
		JUMP_TYPE jump_type = JUMPGATE_HOLE_JUMP;

		int jump_timer;
		uint jumpDistance;
		uint iTargetSystem;
		uint iCoordinateIndex;
		uint targetClient;
		Vector vTargetPosition;
		Matrix matTargetOrient;
	};
	static unordered_map<uint, JUMPDRIVE> mapJumpDrives;

	struct BEACONMATRIX
	{
		uint nickname;
		int inaccuracy;
		uint range;
		uint fuel;
		uint fuelAmount;
		uint beaconFuse = CreateID("fuse_jumpdrive_charge_5");
		float beaconLifetime;
	};

	//There is only one kind of Matrix right now, but this might change later on
	static unordered_map<uint, BEACONMATRIX> mapBeaconMatrix;
	//map the existing Matrix
	struct PLAYERBEACON
	{
		std::vector<uint> incomingClientIDs;
		BEACONMATRIX* arch;
	};
	static unordered_map<uint, PLAYERBEACON> mapPlayerBeaconMatrix;

	//key - requesting player, value - player being requested;
	static unordered_map<uint, uint> mapBeaconJumpRequests;

	void ClearJumpDriveInfo(uint iClientID, bool clearFuses)
	{
		auto& jd = mapJumpDrives[iClientID];
		jd.charging_on = false;
		jd.curr_charge = 0;

		jd.jump_timer = 0;
		jd.jumpDistance = 0;
		jd.iTargetSystem = 0;
		jd.vTargetPosition.x = 0;
		jd.vTargetPosition.y = 0;
		jd.vTargetPosition.z = 0;
		jd.targetClient = 0;
		jd.last_tick_charge = 0;
		jd.iCoordinateIndex = 0;
		jd.jump_type = JUMPGATE_HOLE_JUMP;

		if (clearFuses)
		{
			jd.active_fuse = 0;
			jd.active_charge_fuse.clear();
		}
	}

	void SwitchSystem(uint iClientID, uint system, Vector pos, Matrix ornt)
	{
		mapDeferredJumps[iClientID].system = system;
		mapDeferredJumps[iClientID].pos = pos;
		mapDeferredJumps[iClientID].ornt = ornt;

		// Force a launch to put the ship in the right location in the current system so that
		// when the change system command arrives (hopefully) a fraction of a second later
		// the ship will appear at the right location.
		HkRelocateClient(iClientID, pos, ornt);
		// Send the jump command to the client. The client will send a system switch out complete
		// event which we intercept to set the new starting positions.
		PrintUserCmdText(iClientID, L" ChangeSys %u", system);

		ClearJumpDriveInfo(iClientID, false);
	}

	void HyperJump::LoadSettings(const string &scPluginCfgFile)
	{
		// Patch Archetype::GetEquipment & Archetype::GetShip to suppress annoying warnings flserver-errors.log
		unsigned char patch1[] = { 0x90, 0x90 };
		WriteProcMem((char*)0x62F327E, &patch1, 2);
		WriteProcMem((char*)0x62F944E, &patch1, 2);
		WriteProcMem((char*)0x62F123E, &patch1, 2);

		char szCurDir[MAX_PATH];
		GetCurrentDirectory(sizeof(szCurDir), szCurDir);
		string scCfgFile = string(szCurDir) + "\\flhook_plugins\\jump.cfg";
		string scCfgFileSystemList = string(szCurDir) + "\\flhook_plugins\\jump_allowedsystems.cfg";

		INI_Reader ini;

		if (ini.open(scCfgFileSystemList.c_str(), false))
		{
			uint sysConnectionListSize = 0;
			uint coordListSize = 0;
			while (ini.read_header())
			{
				if (ini.is_header("system"))
				{
					uint originSystem;
					uint jumpRange;
					vector<uint> targetSystemsList;
					while (ini.read_value())
					{
						if (ini.is_value("target_system"))
						{
							targetSystemsList.push_back(CreateID(ini.get_value_string(0)));
							sysConnectionListSize++;
						}
						else if (ini.is_value("origin_system"))
						{
							originSystem = CreateID(ini.get_value_string(0));
						}
						else if (ini.is_value("depth"))
						{
							jumpRange = ini.get_value_int(0);
							if (jumpRange > set_maxJumpRange)
							{
								set_maxJumpRange = jumpRange;
							}
						}
					}
					mapAvailableJumpSystems[originSystem][jumpRange] = targetSystemsList;
				}
				if (ini.is_header("system_jump_positions"))
				{
					while (ini.read_value())
					{
						if (ini.is_value("jump_position"))
						{
							SYSTEMJUMPCOORDS coords;
							coords.system = CreateID(ini.get_value_string(0));
							coords.pos = { ini.get_value_float(1), ini.get_value_float(2), ini.get_value_float(3) };

							Vector erot = { ini.get_value_float(4), ini.get_value_float(5), ini.get_value_float(6) };
							coords.ornt = EulerMatrix(erot);
							coords.sector = VectorToSectorCoord(coords.system, coords.pos);

							mapSystemJumps[coords.system].push_back(coords);
							coordListSize++;
						}
					}
				}
			}
			ConPrint(L"Hyperspace: Loaded %u system coordinates and %u connections for %u systems\n", coordListSize, sysConnectionListSize, mapAvailableJumpSystems.size());
			ini.close();
		}

		if (ini.open(scCfgFile.c_str(), false))
		{
			while (ini.read_header())
			{
				if (ini.is_header("general"))
				{
					while (ini.read_value())
					{
						if (ini.is_value("BeaconCommodity"))
						{
							BeaconCommodity = CreateID(ini.get_value_string());
						}
						else if (ini.is_value("BlindJumpOverrideSystem"))
						{
							set_blindJumpOverrideSystem = CreateID(ini.get_value_string());
						}
						else if (ini.is_value("JumpInInvulnerabilityPeriod"))
						{
							set_jumpInPvPInvulnerabilityPeriod = ini.get_value_int(0);
						}
						else if (ini.is_value("JumpInFuse"))
						{
							uint shipType = ini.get_value_int(0);
							JUMP_TYPE jumpType = static_cast<JUMP_TYPE>(ini.get_value_int(1));
							JUMPFUSE jumpFuse;
							jumpFuse.jump_fuse = CreateID(ini.get_value_string(2));
							jumpFuse.lifetime = ini.get_value_float(3);
							jumpFuse.delay = ini.get_value_float(4);
							JumpInFuseMap[shipType][jumpType] = jumpFuse;
						}
						else if (ini.is_value("JumpDriveIDSStart"))
						{
							set_IDSNamespaceStart = ini.get_value_int(0);
						}
						else if (ini.is_value("BanJumpSystem"))
						{
							set_banJumpSystems.insert(CreateID(ini.get_value_string()));
						}
					}
				}
				else if (ini.is_header("shiprestrictions"))
				{
					while (ini.read_value())
					{
						if (ini.is_value("restrict"))
						{
							uint nicknameHash = CreateID(ini.get_value_string(0));
							jumpRestrictedShipsList.emplace(nicknameHash);
						}
						else if (ini.is_value("JumpCargoSizeRestriction"))
						{
							JumpCargoSizeRestriction = ini.get_value_float(0);
						}
					}
				}
				else if (ini.is_header("jumpdrive"))
				{
					JUMPDRIVE_ARCH jd;
					while (ini.read_value())
					{
						if (ini.is_value("nickname"))
						{
							jd.nickname = CreateValidID(ini.get_value_string(0));
						}
						else if (ini.is_value("can_jump_charge"))
						{
							jd.can_jump_charge = ini.get_value_float(0);
						}
						else if (ini.is_value("charge_rate"))
						{
							jd.charge_rate = ini.get_value_float(0);
						}
						else if (ini.is_value("charge_fuse"))
						{
							jd.charge_fuse.push_back(CreateID(ini.get_value_string(0)));
						}
						else if (ini.is_value("jump_fuse"))
						{
							jd.jump_fuse = CreateID(ini.get_value_string(0));
						}
						else if (ini.is_value("jump_hole_capacity"))
						{
							jd.jump_hole_capacity = ini.get_value_int(0);
						}
						else if (ini.is_value("jump_hole_duration"))
						{
							jd.jump_hole_duration = ini.get_value_int(0);
						}
						else if (ini.is_value("jump_range"))
						{
							jd.jump_range = ini.get_value_int(0);
						}
						else if (ini.is_value("fuel"))
						{
							uint fuel = CreateValidID(ini.get_value_string(0));
							int rate;
							for (uint i = 1; i <= set_maxJumpRange + 1; ++i)
							{
								rate = ini.get_value_int(i);
								jd.mapFuelToUsagePerDistance[fuel].push_back(rate);
							}
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

							jd.available_ship_classes = types;
						}
						else if (ini.is_value("cd_disrupts_charge"))
						{
							jd.cd_disrupts_charge = ini.get_value_bool(0);
						}
					}
					mapJumpDriveArch[jd.nickname] = jd;
				}
				else if (ini.is_header("beacon"))
				{
					BEACONMATRIX bm;
					while (ini.read_value())
					{
						if (ini.is_value("nickname"))
						{
							bm.nickname = CreateValidID(ini.get_value_string(0));
						}
						else if (ini.is_value("inaccuracy"))
						{
							bm.inaccuracy = ini.get_value_int(0);
						}
						else if (ini.is_value("fuel"))
						{
							bm.fuel = CreateValidID(ini.get_value_string(0));
							bm.fuelAmount = ini.get_value_int(1);
						}
						else if (ini.is_value("range"))
						{
							bm.range = ini.get_value_int(0);
						}
						else if (ini.is_value("beacon_time"))
						{
							bm.beaconLifetime = ini.get_value_float(0);
						}
						else if (ini.is_value("beacon_fuse"))
						{
							bm.beaconFuse = CreateID(ini.get_value_string());
						}
					}
					mapBeaconMatrix[bm.nickname] = bm;
				}
			}
			if (BeaconCommodity == 0)
			{
				BeaconCommodity = CreateID("commodity_event_04");
			}
			ini.close();
		}

		// Remove patch now that we've finished loading.
		unsigned char patch2[] = { 0xFF, 0x12 };
		WriteProcMem((char*)0x62F327E, &patch2, 2);
		WriteProcMem((char*)0x62F944E, &patch2, 2);
		WriteProcMem((char*)0x62F123E, &patch2, 2);

		ConPrint(L"Jumpdrive [%d]\n", mapJumpDriveArch.size());
		ConPrint(L"Beacon Matrix [%d]\n", mapBeaconMatrix.size());
	}

	void SetFuse(uint iClientID, uint fuse, float lifetime = 0.0f, float delay = 0.0f)
	{
		JUMPDRIVE &jd = mapJumpDrives[iClientID];
		if (jd.active_fuse)
		{
			IObjInspectImpl *obj = HkGetInspect(iClientID);
			if (obj)
			{
				HkUnLightFuse((IObjRW*)obj, jd.active_fuse, jd.active_fuse_delay);
			}
			jd.active_fuse = 0;
		}

		if (fuse)
		{
			IObjInspectImpl *obj = HkGetInspect(iClientID);
			if (obj)
			{
				jd.active_fuse = fuse;
				jd.active_fuse_delay = delay;
				HkLightFuse((IObjRW*)obj, jd.active_fuse, delay, lifetime, 0.0f);
			}
		}
	}

	void HyperJump::SetJumpInFuse(uint iClientID)
	{
		auto& jdData = mapJumpDrives[iClientID];

		JUMP_TYPE jumpType = jdData.jump_type;

		Archetype::Ship* playerShip = Archetype::GetShip(Players[iClientID].iShipArchetype);
		if (JumpInFuseMap.count(playerShip->iShipClass) && JumpInFuseMap[playerShip->iShipClass].count(jumpType))
		{
			const JUMPFUSE& jumpFuse = JumpInFuseMap[playerShip->iShipClass][jumpType];
			SetFuse(iClientID, jumpFuse.jump_fuse, jumpFuse.lifetime, jumpFuse.delay);
		}
		else
		{
			SetFuse(iClientID, 0);
		}
		jdData.jump_type = JUMPGATE_HOLE_JUMP;
	}

	void AddChargeFuse(uint iClientID, uint fuse)
	{
		IObjInspectImpl *obj = HkGetInspect(iClientID);
		if (obj)
		{
			mapJumpDrives[iClientID].active_charge_fuse.push_back(fuse);
			HkLightFuse((IObjRW*)obj, fuse, 0, 0, 0);
		}
	}

	void StopChargeFuses(uint iClientID)
	{
		IObjInspectImpl *obj = HkGetInspect(iClientID);
		if (obj)
		{
			foreach(mapJumpDrives[iClientID].active_charge_fuse, uint, fuse)
				HkUnLightFuse((IObjRW*)obj, *fuse, 0);
			mapJumpDrives[iClientID].active_charge_fuse.clear();
		}
	}

	bool CheckBeaconFuel(const uint clientId, bool consumeFuel)
	{
		if (!mapPlayerBeaconMatrix.count(clientId))
		{
			return false;
		}
		const BEACONMATRIX* beacon = mapPlayerBeaconMatrix.at(clientId).arch;
		if (!beacon->fuel)
		{
			return true;
		}
		for (const auto& eq : Players[clientId].equipDescList.equip)
		{
			if (eq.iArchID == beacon->fuel)
			{
				if (eq.iCount < beacon->fuelAmount)
				{
					return false;
				}

				if (consumeFuel)
				{
					pub::Player::RemoveCargo(clientId, eq.sID, beacon->fuelAmount);
				}
				return true;
			}
		}

		return false;
	}

	void ShutdownJumpDrive(uint iClientID)
	{
		SetFuse(iClientID, 0);
		StopChargeFuses(iClientID);
		pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jumpdrive_charging_failed"));

		JUMPDRIVE& jd = mapJumpDrives[iClientID];
		if (jd.targetClient)
		{
			auto& beaconData = mapPlayerBeaconMatrix[jd.targetClient].incomingClientIDs;
			for (auto& iter = beaconData.begin(); iter != beaconData.end(); iter++)
			{
				if (*iter == iClientID)
				{
					beaconData.erase(iter);
					auto incomingName = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(iClientID));
					PrintUserCmdText(jd.targetClient, L"%s jump drive charging was aborted", incomingName);
					break;
				}
			}
		}
		jd.jump_timer = 0;
		jd.charging_on = false;
		jd.curr_charge = 0;
		jd.targetClient = 0;
	}

	bool InitJumpDriveInfo(uint iClientID, bool fullCheck = false)
	{
		// Initialise the drive parameters for this ship
		if (mapJumpDrives.count(iClientID) && mapJumpDrives[iClientID].arch != nullptr && !fullCheck)
		{
			return true;
		}

		ClearJumpDriveInfo(iClientID, true);

		// Check that the player has a jump drive and initialise the infomation
		// about it - otherwise return false.
		for (list<EquipDesc>::iterator item = Players[iClientID].equipDescList.equip.begin(); item != Players[iClientID].equipDescList.equip.end(); item++)
		{
			if (mapJumpDriveArch.count(item->iArchID))
			{
				if (item->bMounted)
				{
					mapJumpDrives[iClientID].arch = &mapJumpDriveArch[item->iArchID];
					return true;
				}
			}
		}
		return false;
	}

	bool HyperJump::CheckForBeacon(uint iClientID)
	{
		if (mapPlayerBeaconMatrix.count(iClientID))
		{
			return true;
		}
		for (list<EquipDesc>::iterator item = Players[iClientID].equipDescList.equip.begin(); item != Players[iClientID].equipDescList.equip.end(); item++)
		{
			if (mapBeaconMatrix.count(item->iArchID))
			{
				mapPlayerBeaconMatrix[iClientID].arch = &mapBeaconMatrix[item->iArchID];
				return true;
			}
		}

		return false;
	}

	pair<bool, uint> IsSystemJumpable(uint systemFrom, uint systemTo, uint range)
	{
		if (mapAvailableJumpSystems.count(systemFrom) == 0)
		{
			return make_pair(false, 0);
		}

		if (systemFrom == systemTo)
		{
			return make_pair(true, 0);
		}

		auto& allowedSystemsByRange = mapAvailableJumpSystems[systemFrom];
		for (uint depth = 1; depth <= range; depth++)
		{
			if (allowedSystemsByRange.count(range) && find(allowedSystemsByRange[depth].begin(), allowedSystemsByRange[depth].end(), systemTo) != allowedSystemsByRange[depth].end())
				return make_pair(true, depth);
		}
		return make_pair(false, 0);
	}

	void CreateJumpHolePair(uint iClientID)
	{
		auto& jd = mapJumpDrives[iClientID];
		time_t timestamp = time(0);

		Vector pos;
		Matrix ori;
		pub::SpaceObj::GetLocation(Players[iClientID].iShipID, pos, ori);
		Rotate180(ori);
		TranslateZ(pos, ori, 1450);

		//create exit first so entry has an existing solar to point to
		auto systemInfo = Universe::get_system(jd.iTargetSystem);

		SPAWN_SOLAR_STRUCT entrySolar;

		SPAWN_SOLAR_STRUCT exitSolar;
		exitSolar.loadoutArchetypeId = set_exitJumpHoleLoadout;
		exitSolar.solarArchetypeId = set_exitJumpHoleArchetype;
		exitSolar.iSystemId = jd.iTargetSystem;
		exitSolar.pos = jd.vTargetPosition;
		exitSolar.ori = jd.matTargetOrient;
		exitSolar.solar_ids = 267199;
		exitSolar.nickname = "custom_jump_hole_exit_" + to_string(iClientID) + "_" + to_string(timestamp);

		// create the entrance
		Plugin_Communication(CUSTOM_SPAWN_SOLAR, &exitSolar);

		entrySolar.loadoutArchetypeId = set_entryJumpHoleLoadout;
		entrySolar.solarArchetypeId = set_entryJumpHoleArchetype;
		entrySolar.iSystemId = Players[iClientID].iSystemID;
		entrySolar.pos = pos;
		entrySolar.ori = ori;
		entrySolar.destSystem = exitSolar.iSystemId;
		entrySolar.destObj = exitSolar.iSpaceObjId;
		entrySolar.overwrittenName = L"Collapsing " + HkGetWStringFromIDS(systemInfo->strid_name) + L" Jump Hole";
		entrySolar.solar_ids = set_IDSNamespaceStart + iClientID;
		entrySolar.nickname = "custom_jump_hole_entry_" + to_string(iClientID) + "_" + to_string(timestamp);

		Plugin_Communication(CUSTOM_SPAWN_SOLAR, &entrySolar);

		JD_JUMPHOLE exitHole;
		exitHole.objId = exitSolar.iSpaceObjId;
		exitHole.isEntrance = false;
		exitHole.pairedJH = entrySolar.iSpaceObjId;
		exitHole.remainingCapacity = 0;

		JD_JUMPHOLE entryHole;
		entryHole.objId = entrySolar.iSpaceObjId;
		entryHole.targetSystem = entrySolar.destSystem;
		entryHole.timeout = time(nullptr) + jd.arch->jump_hole_duration;
		entryHole.isEntrance = true;
		entryHole.pairedJH = exitSolar.iSpaceObjId;
		entryHole.remainingCapacity = jd.arch->jump_hole_capacity;

		jumpObjMap[exitSolar.iSpaceObjId] = exitHole;
		jumpObjMap[entrySolar.iSpaceObjId] = entryHole;

		ClearJumpDriveInfo(iClientID, true);
	}

	bool HyperJump::Dock_Call(uint const& iShip, uint const& iDockTarget)
	{
		if (!jumpObjMap.count(iDockTarget))
		{
			return true;
		}

		uint client = HkGetClientIDByShip(iShip);
		if (!client)
		{
			return true;
		}

		auto& jumpObj = jumpObjMap.at(iDockTarget);

		if (!jumpObj.isEntrance)
		{
			pub::Player::SendNNMessage(client, pub::GetNicknameId("anomaly_detected"));
			return false;
		}

		if (jumpObj.remainingCapacity == 0)
		{
			PrintUserCmdText(client, L"ERR Jump Hole capacity reached, too unstable to proceed.");
			return false;
		}

		time_t now = time(0);
		if (now - 1 > jumpObj.timeout && jumpObj.dockingQueue.empty())
		{
			PrintUserCmdText(client, L"ERR Jump Hole timing out, too unstable to proceed.");
			return false;
		}

		if (jumpObj.remainingCapacity != UINT_MAX)
		{
			jumpObj.remainingCapacity--;
		}

		if (jumpObj.remainingCapacity == 0)
		{
			jumpObj.timeout = 0; // set the JH to collapse immediately upon the last person's arrival at the end system
		}

		jumpObj.dockingQueue.insert(iShip);
		shipToJumpObjMap[iShip] = iDockTarget;
		return true;
	}

	void HyperJump::JumpInComplete(uint ship)
	{
		auto iter = shipToJumpObjMap.find(ship);
		if (iter == shipToJumpObjMap.end())
		{
			return;
		}
		auto jumpObjIter = jumpObjMap.find(iter->second);
		if (jumpObjIter == jumpObjMap.end())
		{
			return;
		}
		jumpObjIter->second.dockingQueue.erase(ship);
		shipToJumpObjMap.erase(ship);
	}

	void HyperJump::RequestCancel(int iType, uint iShip, uint dockObjId)
	{
		if (iType != 0)
		{
			return;
		}

		auto iter = jumpObjMap.find(dockObjId);
		if (iter != jumpObjMap.end())
		{
			if (iter->second.dockingQueue.count(iShip))
			{
				jumpObjMap.at(dockObjId).dockingQueue.erase(iShip);
				jumpObjMap.at(dockObjId).remainingCapacity++;
				shipToJumpObjMap.erase(iShip);
			}
		}
	}

	pair<bool, uint> CanBeaconJumpToPlayer(const uint jumpingClientId, const uint beaconClientId)
	{

		if (!CheckForBeacon(beaconClientId))
		{
			return { false, 0 };
		}

		return IsSystemJumpable(Players[jumpingClientId].iSystemID, Players[beaconClientId].iSystemID, mapJumpDrives[jumpingClientId].arch->jump_range + mapPlayerBeaconMatrix[beaconClientId].arch->range);
	}

	bool HyperJump::UserCmd_CanBeaconJumpToPlayer(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
	{
		if (wscParam.empty())
		{
			PrintUserCmdText(iClientID, L"ERR Invalid parameters");
			PrintUserCmdText(iClientID, usage);
			return true;
		}
		if (!InitJumpDriveInfo(iClientID))
		{
			PrintUserCmdText(iClientID, L"ERR Jump Drive not equipped");
			return true;
		}
		wstring wscCharname = GetParam(wscParam, L' ', 0);
		uint iTargetClientID = HkGetClientIdFromCharname(wscCharname);
		if (iTargetClientID == UINT_MAX)
		{
			uint targetClientID = ToUInt(wscCharname);
			if (targetClientID && HkIsValidClientID(targetClientID))
			{
				iTargetClientID = targetClientID;
			}
			else 
			{
				PrintUserCmdText(iClientID, L"ERR Player not online");
				return true;
			}
		}

		if (CanBeaconJumpToPlayer(iClientID, iTargetClientID).first)
		{
			PrintUserCmdText(iClientID, L"Can beacon jump to player");
		}
		else
		{
			PrintUserCmdText(iClientID, L"Cannot beacon jump to player");
		}
		return true;
	}

	void ListJumpableSystems(uint iClientID)
	{
		uint iSystemID;
		pub::Player::GetSystem(iClientID, iSystemID);
		const auto& playerJumpDrive = mapJumpDrives[iClientID];
		if (!InitJumpDriveInfo(iClientID))
		{
			PrintUserCmdText(iClientID, L"ERR Jump Drive not equipped");
			return;
		}

		if (!mapAvailableJumpSystems.count(iSystemID))
		{
			PrintUserCmdText(iClientID, L"ERR Jumping from this system is not possible");
			return;
		}

		auto& systemRangeList = mapAvailableJumpSystems[iSystemID];
		for (uint depth = 1; depth <= playerJumpDrive.arch->jump_range; depth++)
		{
			wstring sysList = L"Systems " + stows(itos(depth)) + L" jumps away:";
			for (uint &allowed_sys : systemRangeList[depth])
			{
				const Universe::ISystem *systemData = Universe::get_system(allowed_sys);
				sysList += L" [" + HkGetWStringFromIDS(systemData->strid_name) + L"]";
			}
			PrintUserCmdText(iClientID, sysList.c_str());
		}
		
	}

	bool HyperJump::UserCmd_IsSystemJumpable(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
	{
		if (!InitJumpDriveInfo(iClientID))
		{
			PrintUserCmdText(iClientID, L"ERR No jump drive installed");
			return true;
		}
		wstring sysName = ToLower(GetParamToEnd(wscParam, ' ', 0));
		if (sysName.empty())
		{
			PrintUserCmdText(iClientID, L"ERR Invalid system name");
			return true;
		}
		for (struct Universe::ISystem *sysinfo = Universe::GetFirstSystem(); sysinfo; sysinfo = Universe::GetNextSystem())
		{
			const auto& fullSystemName = HkGetWStringFromIDS(sysinfo->strid_name);
			if (ToLower(fullSystemName) == sysName)
			{
				uint &iTargetSystemID = sysinfo->id;
				uint iClientSystem;
				pub::Player::GetSystem(iClientID, iClientSystem);

				auto canJump = IsSystemJumpable(iClientSystem, iTargetSystemID, mapJumpDrives[iClientID].arch->jump_range);
				if (canJump.first)
				{
					PrintUserCmdText(iClientID, L"%ls is %u systems away, within your jump range of %u systems", fullSystemName.c_str(), canJump.second, mapJumpDrives[iClientID].arch->jump_range);
				}
				else
				{
					PrintUserCmdText(iClientID, L"%ls is out of your jump range of %u systems", fullSystemName.c_str(), mapJumpDrives[iClientID].arch->jump_range);
				}
				return true;
			}
		}

		PrintUserCmdText(iClientID, L"ERR Invalid system name");
		return true;
	}
	
	bool SetJumpSystem(const uint iClientID, JUMPDRIVE& jd, wstring& targetSystem, const wchar_t* usage)
	{
		if (!InitJumpDriveInfo(iClientID))
		{
			PrintUserCmdText(iClientID, L"ERR Jump drive not equipped");
			pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jumpdrive_charging_failed"));
			return false;
		}

		targetSystem = ToLower(targetSystem);

		uint iPlayerSystem;
		pub::Player::GetSystem(iClientID, iPlayerSystem);

		if (set_banJumpSystems.count(iPlayerSystem))
		{
			PrintUserCmdText(iClientID, L"ERR Jumping not allowed in this system");
			return false;
		}

		if (targetSystem == L"blind")
		{
			if (set_blindJumpOverrideSystem)
			{
				if (!mapSystemJumps.count(set_blindJumpOverrideSystem))
				{
					PrintUserCmdText(iClientID, L"ERR Blind jumping misconfigured, contact staff!");
					return false;
				}
				auto& jumpCoordList = mapSystemJumps[set_blindJumpOverrideSystem];
				auto& jumpCoords = jumpCoordList.at(rand() % jumpCoordList.size());
				JUMPDRIVE& jd = mapJumpDrives[iClientID];
				jd.iTargetSystem = set_blindJumpOverrideSystem;
				jd.vTargetPosition = jumpCoords.pos;
				jd.matTargetOrient = jumpCoords.ornt;
				jd.jumpDistance = 0;
			}
			else
			{
				uint entrySystem = Players[iClientID].iSystemID;
				vector<uint> viableTargets;
				for (uint i = 1; i <= jd.arch->jump_range ; i++)
				{
					auto& systems = mapAvailableJumpSystems[entrySystem][i];
					viableTargets.insert(viableTargets.end(), systems.begin(), systems.end());
				}
				if (viableTargets.empty())
				{
					return false;
				}
				else
				{
					uint system = viableTargets.at(rand() % viableTargets.size());
					auto& jumpCoordList = mapSystemJumps[system];
					auto& coords = jumpCoordList.at(rand() % jumpCoordList.size());

					jd.iTargetSystem = system;
					jd.jumpDistance = IsSystemJumpable(entrySystem, system, set_maxJumpRange).second;
					jd.vTargetPosition = coords.pos;
					jd.matTargetOrient = coords.ornt;

					PrintUserCmdText(iClientID, L"Blind jump charging...");
					pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_blind_jump_charging"));
					return true;
				}
			}
			return true;
		}

		for (struct Universe::ISystem *sysinfo = Universe::GetFirstSystem(); sysinfo; sysinfo = Universe::GetNextSystem())
		{
			const auto& fullSystemName = HkGetWStringFromIDS(sysinfo->strid_name);
			const auto& fullSystemNameLowerCased = ToLower(fullSystemName);
			if (fullSystemNameLowerCased != targetSystem)
			{
				continue;
			}
			uint &iTargetSystemID = sysinfo->id;

			auto canJump = IsSystemJumpable(iPlayerSystem, iTargetSystemID, mapJumpDrives[iClientID].arch->jump_range);
			if (!canJump.first)
			{
				PrintUserCmdText(iClientID, L"System out of range, use '/jump list' for a list of valid destinations");
				return false;
			}
			if (mapSystemJumps.count(iTargetSystemID) == 0)
			{
				PrintUserCmdText(iClientID, L"ERR Unable to lock onto the target system, too much interference");
				return false;
			}
			auto& jumpCoordList = mapSystemJumps[iTargetSystemID];

			uint sectorSelectedIndex = rand() % jumpCoordList.size();
			auto& jumpCoords = jumpCoordList.at(sectorSelectedIndex);
			jd.iTargetSystem = iTargetSystemID;
			jd.vTargetPosition = jumpCoords.pos;
			jd.matTargetOrient = jumpCoords.ornt;

			jd.jumpDistance = canJump.second;

			PrintUserCmdText(iClientID, L"System locked in, jumping to %ls, sector %ls", fullSystemName.c_str(), jumpCoords.sector.c_str());
			if (mapSystemJumps[iTargetSystemID].size() > 1)
			{
				PrintUserCmdText(iClientID, L"Alternate jump coordinates available, use /setsector to switch");
				int iCount = 1;
				++sectorSelectedIndex;
				for (auto coord : mapSystemJumps[iTargetSystemID])
				{
					if (sectorSelectedIndex == iCount)
					{
						PrintUserCmdText(iClientID, L"%u. %ls - selected", iCount, coord.sector.c_str());
					}
					else
					{
						PrintUserCmdText(iClientID, L"%u. %ls", iCount, coord.sector.c_str());
					}
					iCount++;
				}
			}
			return true;
		}
		PrintUserCmdText(iClientID, usage);
		PrintUserCmdText(iClientID, L"ERR Command or system name unrecognized");
		return false;
	}

	bool UserCmd_SetSector(uint iClientID, const wstring & wscCmd, const wstring & wscParam, const wchar_t * usage)
	{
		if (!InitJumpDriveInfo(iClientID))
		{
			PrintUserCmdText(iClientID, L"ERR Jump drive not equipped");
			pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jumpdrive_charging_failed"));
			return true;
		}

		if (!mapJumpDrives[iClientID].iTargetSystem) 
		{
			PrintUserCmdText(iClientID, L"ERR Jump Drive not locked onto a system, use /jump <systemName> to initiate.");
			return true;
		}

		auto &jd = mapJumpDrives[iClientID];
		uint index = ToUInt(GetParam(wscParam, ' ', 0));
		if (!index || mapSystemJumps[jd.iTargetSystem].size() < index)
		{
			PrintUserCmdText(iClientID, L"ERR invalid selection");
			return true;
		}
		auto &coords = mapSystemJumps[jd.iTargetSystem].at(index - 1);
		jd.vTargetPosition = coords.pos;
		jd.matTargetOrient = coords.ornt;

		PrintUserCmdText(iClientID, L"Sector %s selected", coords.sector.c_str());
		return true;
	}

	bool UserCmd_ListSector(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
	{
		wstring targetSystem = ToLower(GetParamToEnd(wscParam, ' ', 0));
		for (struct Universe::ISystem* sysinfo = Universe::GetFirstSystem(); sysinfo; sysinfo = Universe::GetNextSystem())
		{
			const auto& fullSystemName = HkGetWStringFromIDS(sysinfo->strid_name);
			const auto& fullSystemNameLowerCased = ToLower(fullSystemName);
			if (fullSystemNameLowerCased != targetSystem)
			{
				continue;
			}
			if (!mapSystemJumps.count(sysinfo->id))
			{
				PrintUserCmdText(iClientID, L"ERR System not jumpable");
				return true;
			}

			PrintUserCmdText(iClientID, L"Available jump coordinates for %ls:", fullSystemName.c_str());
			uint count = 1;
			for (auto coord : mapSystemJumps.at(sysinfo->id))
			{
				PrintUserCmdText(iClientID, L"%u. %ls", count, coord.sector.c_str());
				++count;
			}
			return true;
		}
		PrintUserCmdText(iClientID, L"ERR Incorrect system name");
		return true;
	}

	void HyperJump::Timer()
	{
		vector<uint> lstOldClients;

		// Handle jump drive charging
		for (auto& iter = mapJumpDrives.begin(); iter != mapJumpDrives.end(); iter++)
		{
			uint iClientID = iter->first;

			if (Players[iClientID].iShipID == 0)
			{
				lstOldClients.emplace_back(iClientID);
				continue;
			}
			
			JUMPDRIVE &jd = iter->second;

			if (jd.arch == nullptr)
			{
				continue;
			}
			if (jd.jump_timer > 0)
			{
				if (setCloakingClients.find(iClientID) != setCloakingClients.end())
				{
					PrintUserCmdText(iClientID, L"ERR Ship is cloaked.");
					ShutdownJumpDrive(iClientID);
					continue;
				}

				jd.jump_timer--;
				// Turn on the jumpdrive flash
				if (jd.jump_timer == 5)
				{
					jd.curr_charge = 0.0;
					jd.charging_on = false;
					SetFuse(iClientID, jd.arch->jump_fuse);
					pub::Audio::PlaySoundEffect(iClientID, CreateID("dsy_jumpdrive_activate"));

					if (jd.targetClient)
					{
						if (!CheckBeaconFuel(jd.targetClient, true))
						{
							PrintUserCmdText(jd.targetClient, L"ERR Insufficient batteries to power the matrix!");
							PrintUserCmdText(iClientID, L"ERR Beacon ship has insufficient batteries!");
							ShutdownJumpDrive(iClientID);
							continue;
						}
						PlayerData* pd = nullptr;
						uint systemId = Players[jd.targetClient].iSystemID;
						wstring beaconPlayer = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(jd.targetClient));
						wstring matrixJumpMessage = L"Hyperspace breach is forming around %player!";
						matrixJumpMessage = ReplaceStr(matrixJumpMessage, L"%player", beaconPlayer);
						while (pd = Players.traverse_active(pd))
						{
							if (pd->iSystemID != systemId || !pd->iShipID)
							{
								continue;
							}
							if (15000.0f < HkDistance3D(ClientInfo[pd->iOnlineID].cship->vPos,
								ClientInfo[jd.targetClient].cship->vPos))
							{
								continue;
							}
							PrintUserCmdText(pd->iOnlineID, matrixJumpMessage);
							pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_beacon_jump_detected"));

						}
						SetFuse(jd.targetClient, mapPlayerBeaconMatrix[jd.targetClient].arch->beaconFuse, mapPlayerBeaconMatrix[jd.targetClient].arch->beaconLifetime, 0);
						
					}
					pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jumpdrive_charging_complete"));
				}
				// Execute the jump and do the pop sound
				else if (jd.jump_timer == 0)
				{

					CUSTOM_IN_WARP_CHECK_STRUCT info = { iClientID, false };
					Plugin_Communication(CUSTOM_IN_WARP_CHECK, &info);
					if (info.inWarp)
					{
						//bump up timer to execute the jump as soon as the player exits the jump tunnel.
						jd.jump_timer++;
						continue;
					}

					uint playerSystem;
					pub::Player::GetSystem(iClientID, playerSystem);
					auto canJump = IsSystemJumpable(playerSystem, jd.iTargetSystem, jd.jumpDistance);
					if (!jd.targetClient && jd.iTargetSystem != set_blindJumpOverrideSystem && (!canJump.first || canJump.second > jd.jumpDistance))
					{
						PrintUserCmdText(iClientID, L"ERR You moved out of jump range during the charging period.");
						ShutdownJumpDrive(iClientID);
						continue;
					}

					// Stop the charging fuses
					StopChargeFuses(iClientID);
					SetFuse(iClientID, 0);

					if (jd.targetClient)
					{
						//check if in transit
						if (!HkGetInspect(jd.targetClient))
						{
							PrintUserCmdText(iClientID, L"ERR Target Player is not in space, aborting jump.");
							ShutdownJumpDrive(iClientID);
							continue;
						}

						auto& canJump = CanBeaconJumpToPlayer(iClientID, jd.targetClient);
						if (!canJump.first || canJump.second > jd.jumpDistance)
						{
							PrintUserCmdText(iClientID, L"ERR Target Player is out of range, aborting jump.");
							ShutdownJumpDrive(iClientID);
							continue;
						}

						CUSTOM_IN_WARP_CHECK_STRUCT info = { jd.targetClient, false };
						Plugin_Communication(CUSTOM_IN_WARP_CHECK, &info);
						if (info.inWarp)
						{
							//bump up timer to execute the jump as soon as beacon player exits the jump tunnel.
							jd.jump_timer++;
							continue;
						}

						uint targetShip;
						pub::Player::GetShip(jd.targetClient, targetShip);
						pub::SpaceObj::GetLocation(targetShip, jd.vTargetPosition, jd.matTargetOrient);
						pub::Player::GetSystem(jd.targetClient, jd.iTargetSystem);
						int inaccuracy = mapPlayerBeaconMatrix[jd.targetClient].arch->inaccuracy;
						jd.vTargetPosition.x += (rand() % (inaccuracy * 2)) - inaccuracy;
						jd.vTargetPosition.y += (rand() % (inaccuracy * 2)) - inaccuracy;
						jd.vTargetPosition.z += (rand() % (inaccuracy * 2)) - inaccuracy;
					}

					CreateJumpHolePair(iClientID);
					pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jumpdrive_activated"));
				}

				// Proceed to the next ship.
				continue;
			}

			if (jd.charging_on && jd.jump_timer == 0)
			{
				// Use fuel to charge the jump drive's storage capacitors
				bool successfulCharge = false;

				if (setCloakingClients.find(iClientID) != setCloakingClients.end())
				{
					PrintUserCmdText(iClientID, L"ERR Ship is cloaked.");
					ShutdownJumpDrive(iClientID);
					continue;
				}

				for (list<EquipDesc>::iterator item = Players[iClientID].equipDescList.equip.begin(); item != Players[iClientID].equipDescList.equip.end(); item++)
				{
					if (jd.arch->mapFuelToUsagePerDistance.count(item->iArchID))
					{
						uint fuel_usage = jd.arch->mapFuelToUsagePerDistance[item->iArchID].at(jd.jumpDistance);
						if (item->iCount >= fuel_usage)
						{
							pub::Player::RemoveCargo(iClientID, item->sID, fuel_usage);
							if ((jd.curr_charge < jd.arch->can_jump_charge)
								&& (jd.curr_charge + jd.arch->charge_rate >= jd.arch->can_jump_charge))
							{
								jd.jump_timer = 6;
							}
							jd.curr_charge += jd.arch->charge_rate;
							successfulCharge = true;
							break;
						}
					}
				}

				// Turn off the charging effect if the charging has failed due to lack of fuel and
				// skip to the next player.
				if (!successfulCharge)
				{
					jd.charging_on = false;
					PrintUserCmdText(iClientID, L"Jump drive charging failed, out of batteries.");
					ShutdownJumpDrive(iClientID);
					continue;
				}

				pub::Audio::PlaySoundEffect(iClientID, CreateID("dsy_jumpdrive_charge"));

				uint expected_charge_status = (uint)(jd.curr_charge / jd.arch->can_jump_charge * 10);
				if (jd.last_tick_charge != expected_charge_status)
				{
					jd.last_tick_charge = expected_charge_status;
					PrintUserCmdText(iClientID, L"Jump drive charge %0.0f%%", (jd.curr_charge / jd.arch->can_jump_charge) * 100.0f);

					// Find the currently expected charge fuse
					uint charge_fuse_idx = (uint)((jd.curr_charge / jd.arch->can_jump_charge) * (jd.arch->charge_fuse.size() - 1));
					if (charge_fuse_idx >= jd.arch->charge_fuse.size())
						charge_fuse_idx = jd.arch->charge_fuse.size() - 1;

					// If the fuse is not present then activate it.
					uint charge_fuse = jd.arch->charge_fuse[charge_fuse_idx];
					if (find(jd.active_charge_fuse.begin(), jd.active_charge_fuse.end(), charge_fuse)
						== jd.active_charge_fuse.end())
					{
						AddChargeFuse(iClientID, charge_fuse);
					}
				}
			}
		}

		//handle timeout of dynamically spawned Temporary Jump Holes
		if (!jumpObjMap.empty())
		{
			time_t now = time(0);
			static vector<uint> JumpHoleExitsToClose;
			for (auto& iter : jumpObjMap)
			{
				JD_JUMPHOLE& jh = iter.second;
				if (jh.timeout >= now || !jh.isEntrance)
				{
					continue;
				}
				
				if (jh.dockingQueue.empty())
				{
					JumpHoleExitsToClose.emplace_back(jh.pairedJH);
					JumpHoleExitsToClose.emplace_back(jh.objId);
				}
				else
				{
					for (uint ship : jh.dockingQueue)
					{
						//check if a ship still exists (could be disconnected)
						if (pub::SpaceObj::ExistsAndAlive(ship) == -2)
						{
							jh.dockingQueue.erase(ship);
						}
					}
				}
			}
			if (!JumpHoleExitsToClose.empty())
			{
				for (uint jumpHoleID : JumpHoleExitsToClose)
				{
					jumpObjMap.erase(jumpHoleID);
					DESPAWN_SOLAR_STRUCT info;
					info.destroyType = FUSE;
					info.spaceObjId = jumpHoleID;
					Plugin_Communication(CUSTOM_DESPAWN_SOLAR, &info);
				}
				JumpHoleExitsToClose.clear();
			}
		}

		// If the ship has docked or died remove the client.	
		for(uint iClientID : lstOldClients)
		{
			mapJumpDrives.erase(iClientID);
		}
	}

	void HyperJump::SetJumpInPvPInvulnerability(uint clientID)
	{
		if (set_jumpInPvPInvulnerabilityPeriod)
		{
			ClientInfo[clientID].tmProtectedUntil = timeInMS() + set_jumpInPvPInvulnerabilityPeriod;
		}
	}

	void HyperJump::SystemSwitchOut(uint clientId, uint spaceObjId)
	{
		if (mapJumpDrives[clientId].charging_on)
		{
			PrintUserCmdText(clientId, L"Entering jump anomaly. Discharging jump drive.");
			ShutdownJumpDrive(clientId);
		}
	}

	bool HyperJump::SystemSwitchOutComplete(unsigned int iShip, unsigned int iClientID)
	{
		static PBYTE SwitchOut = 0;
		if (!SwitchOut)
		{
			SwitchOut = (PBYTE)hModServer + 0xf600;

			DWORD dummy;
			VirtualProtect(SwitchOut + 0xd7, 200, PAGE_EXECUTE_READWRITE, &dummy);
		}

		// Patch the system switch out routine to put the ship in a
		// system of our choosing.
		if (mapDeferredJumps.find(iClientID) != mapDeferredJumps.end())
		{
			uint iSystemID = mapDeferredJumps[iClientID].system;
			SwitchOut[0x0d7] = 0xeb;				// ignore exit object
			SwitchOut[0x0d8] = 0x40;
			SwitchOut[0x119] = 0xbb;				// set the destination system
			*(PDWORD)(SwitchOut + 0x11a) = iSystemID;
			SwitchOut[0x266] = 0x45;				// don't generate warning
			*(float*)(SwitchOut + 0x2b0) = mapDeferredJumps[iClientID].pos.z;		// set entry location
			*(float*)(SwitchOut + 0x2b8) = mapDeferredJumps[iClientID].pos.y;
			*(float*)(SwitchOut + 0x2c0) = mapDeferredJumps[iClientID].pos.x;
			*(float*)(SwitchOut + 0x2c8) = mapDeferredJumps[iClientID].ornt.data[2][2];
			*(float*)(SwitchOut + 0x2d0) = mapDeferredJumps[iClientID].ornt.data[1][1];
			*(float*)(SwitchOut + 0x2d8) = mapDeferredJumps[iClientID].ornt.data[0][0];
			*(float*)(SwitchOut + 0x2e0) = mapDeferredJumps[iClientID].ornt.data[2][1];
			*(float*)(SwitchOut + 0x2e8) = mapDeferredJumps[iClientID].ornt.data[2][0];
			*(float*)(SwitchOut + 0x2f0) = mapDeferredJumps[iClientID].ornt.data[1][2];
			*(float*)(SwitchOut + 0x2f8) = mapDeferredJumps[iClientID].ornt.data[1][0];
			*(float*)(SwitchOut + 0x300) = mapDeferredJumps[iClientID].ornt.data[0][2];
			*(float*)(SwitchOut + 0x308) = mapDeferredJumps[iClientID].ornt.data[0][1];
			*(PDWORD)(SwitchOut + 0x388) = 0x03ebc031;		// ignore entry object
			mapDeferredJumps.erase(iClientID);
			pub::SpaceObj::SetInvincible(iShip, false, false, 0);
			Server.SystemSwitchOutComplete(iShip, iClientID);
			SwitchOut[0x0d7] = 0x0f;
			SwitchOut[0x0d8] = 0x84;
			SwitchOut[0x119] = 0x87;
			*(PDWORD)(SwitchOut + 0x11a) = 0x1b8;
			*(PDWORD)(SwitchOut + 0x25d) = 0x1cf7f;
			SwitchOut[0x266] = 0x1a;
			*(float*)(SwitchOut + 0x2b0) =
				*(float*)(SwitchOut + 0x2b8) =
				*(float*)(SwitchOut + 0x2c0) = 0;
			*(float*)(SwitchOut + 0x2c8) =
				*(float*)(SwitchOut + 0x2d0) =
				*(float*)(SwitchOut + 0x2d8) = 1;
			*(float*)(SwitchOut + 0x2e0) =
				*(float*)(SwitchOut + 0x2e8) =
				*(float*)(SwitchOut + 0x2f0) =
				*(float*)(SwitchOut + 0x2f8) =
				*(float*)(SwitchOut + 0x300) =
				*(float*)(SwitchOut + 0x308) = 0;
			*(PDWORD)(SwitchOut + 0x388) = 0xcf8b178b;

			CUSTOM_JUMP_STRUCT info;
			info.iShipID = iShip;
			info.iSystemID = iSystemID;
			
			Plugin_Communication(CUSTOM_JUMP, &info);
			return true;
		}
		return false;
	}

	void HyperJump::ClearClientInfo(const uint iClientID)
	{
		const auto& beaconInfo = mapPlayerBeaconMatrix.find(iClientID);
		if (beaconInfo != mapPlayerBeaconMatrix.end())
		{
			for (uint queuedClientID : beaconInfo->second.incomingClientIDs)
			{
				PrintUserCmdText(queuedClientID, L"Lost lockon onto target ship. Shutting down jump drive.");
				ShutdownJumpDrive(queuedClientID);
			}
		}
		mapDeferredJumps.erase(iClientID);
		mapJumpDrives.erase(iClientID);
		mapPlayerBeaconMatrix.erase(iClientID);
		setCloakingClients.erase(iClientID);
	}

	/** Chase a player. Works across systems but needs improvement of the path selection algorithm */
	void HyperJump::AdminCmd_Chase(CCmds* cmds, const wstring &wscCharname)
	{
		RIGHT_CHECK(RIGHT_CHASEPULL)

		HKPLAYERINFO adminPlyr;
		if (HkGetPlayerInfo(cmds->GetAdminName(), adminPlyr, false) != HKE_OK)
		{
			cmds->Print(L"ERR Not in space\n");
			return;
		}

		HKPLAYERINFO targetPlyr;
		if (HkGetPlayerInfo(wscCharname, targetPlyr, false) != HKE_OK || targetPlyr.iShip == 0)
		{
			cmds->Print(L"ERR Player not found or not in space\n");
			return;
		}

		Vector pos;
		Matrix ornt;
		pub::SpaceObj::GetLocation(targetPlyr.iShip, pos, ornt);
		pos.y += 100;

		cmds->Print(L"Jump to system=%s x=%0.0f y=%0.0f z=%0.0f\n", targetPlyr.wscSystem.c_str(), pos.x, pos.y, pos.z);
		mapJumpDrives[adminPlyr.iClientID].jump_type = BEAM_JUMP;
		SwitchSystem(adminPlyr.iClientID, targetPlyr.iSystem, pos, ornt);
		return;
	}

	/** Beam admin to a base. Works across systems but needs improvement of the path selection algorithm */
	bool HyperJump::AdminCmd_Beam(CCmds* cmds, const wstring &wscCharname, const wstring &wscTargetBaseName)
	{
		if (!(cmds->rights & RIGHT_BEAMKILL))
		{
			cmds->Print(L"ERR No permission\n");
			return true;;
		}

		HKPLAYERINFO info;
		if (HkGetPlayerInfo(wscCharname, info, false) != HKE_OK)
		{
			cmds->Print(L"ERR Player not found\n");
			return true;
		}

		if (info.iShip == 0)
		{
			cmds->Print(L"ERR Player not in space\n");
			return true;
		}

		// Search for an exact match at the start of the name
		struct Universe::IBase *baseinfo = Universe::GetFirstBase();
		while (baseinfo)
		{
			wstring basename = HkGetWStringFromIDS(baseinfo->iBaseIDS);
			if (ToLower(basename).find(ToLower(wscTargetBaseName)) == 0)
			{
				HkBeamById(info.iClientID, baseinfo->iBaseID);
				return true;
			}
			baseinfo = Universe::GetNextBase();
		}

		// Exact match failed, try a for an partial match
		baseinfo = Universe::GetFirstBase();
		while (baseinfo)
		{
			wstring basename = HkGetWStringFromIDS(baseinfo->iBaseIDS);
			if (ToLower(basename).find(ToLower(wscTargetBaseName)) != -1)
			{
				HkBeamById(info.iClientID, baseinfo->iBaseID);
				return true;
			}
			baseinfo = Universe::GetNextBase();
		}

		// Fall back to default flhook .beam command
		return false;
	}

	/** Pull a player to you. Works across systems but needs improvement of the path selection algorithm */
	void HyperJump::AdminCmd_Pull(CCmds* cmds, const wstring &wscCharname)
	{
		RIGHT_CHECK(RIGHT_CHASEPULL)

		HKPLAYERINFO adminPlyr;
		if (HkGetPlayerInfo(cmds->GetAdminName(), adminPlyr, false) != HKE_OK || adminPlyr.iShip == 0)
		{
			cmds->Print(L"ERR Not in space\n");
			return;
		}

		HKPLAYERINFO targetPlyr;
		if (HkGetPlayerInfo(wscCharname, targetPlyr, false) != HKE_OK)
		{
			cmds->Print(L"ERR Player not found\n");
			return;
		}

		Vector pos;
		Matrix ornt;
		pub::SpaceObj::GetLocation(adminPlyr.iShip, pos, ornt);
		pos.y += 400;

		cmds->Print(L"Jump to system=%s x=%0.0f y=%0.0f z=%0.0f\n", adminPlyr.wscSystem.c_str(), pos.x, pos.y, pos.z);
		mapJumpDrives[targetPlyr.iClientID].jump_type = BEAM_JUMP;
		SwitchSystem(targetPlyr.iClientID, adminPlyr.iSystem, pos, ornt);
		return;
	}

	/** Move to location */
	void HyperJump::AdminCmd_Move(CCmds* cmds, float x, float y, float z)
	{
		if (cmds->ArgStrToEnd(1).length() == 0)
		{
			cmds->Print(L"ERR Usage: move x y z\n");
			return;
		}

		RIGHT_CHECK(RIGHT_CHASEPULL)

		HKPLAYERINFO adminPlyr;
		if (HkGetPlayerInfo(cmds->GetAdminName(), adminPlyr, false) != HKE_OK || adminPlyr.iShip == 0)
		{
			cmds->Print(L"ERR Not in space\n");
			return;
		}

		Vector pos;
		Matrix rot;
		pub::SpaceObj::GetLocation(adminPlyr.iShip, pos, rot);
		pos.x = x;
		pos.y = y;
		pos.z = z;
		cmds->Print(L"Moving to %0.0f %0.0f %0.0f\n", pos.x, pos.y, pos.z);
		HkRelocateClient(adminPlyr.iClientID, pos, rot);
		return;
	}

	bool HyperJump::UserCmd_Jump(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
	{
		// If no ship, report a warning
		uint ship;
		pub::Player::GetShip(iClientID, ship);
		if (!ship)
		{
			PrintUserCmdText(iClientID, L"ERR You need to be in space!");
			pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jumpdrive_charging_failed"));
			return true;
		}

		if (!InitJumpDriveInfo(iClientID))
		{
			PrintUserCmdText(iClientID, L"ERR Jump drive not equipped");
			pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jumpdrive_charging_failed"));
			return true;
		}

		JUMPDRIVE &jd = mapJumpDrives[iClientID];

		uint type;
		pub::SpaceObj::GetType(ship, type);

		if (!(jd.arch->available_ship_classes & type))
		{
			PrintUserCmdText(iClientID, L"ERR Jump Drive will not function on this ship type");
			pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jumpdrive_charging_failed"));
			return true;
		}

		wstring& input = ToLower(GetParamToEnd(wscParam, ' ', 0));

		if (input == L"list")
		{
			ListJumpableSystems(iClientID);
			return true;
		}

		if (jd.charging_on)
		{
			if (input == L"stop")
			{
				ShutdownJumpDrive(iClientID);
				PrintUserCmdText(iClientID, L"Jump Drive disabled");
				pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jump_drive_disrupted"));
			}
			else
			{
				PrintUserCmdText(iClientID, L"ERR Jump Drive charging, to stop, type /jump stop");
			}
			return true;
		}

		if (!SetJumpSystem(iClientID, jd, input, usage))
		{
			return true;
		}

		jd.charging_on = true;
		jd.curr_charge = 0.0f;
		jd.last_tick_charge = 0;

		pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jumpdrive_charging"));
		PrintUserCmdText(iClientID, L"Jump drive charging");
		// Print out a message within the iLocalChatRange when a player engages a JD.
		wstring wscMsg = L"%time WARNING: A hyperspace breach is being opened by %player";
		wscMsg = ReplaceStr(wscMsg, L"%time", GetTimeString(set_bLocalTime));
		wscMsg = ReplaceStr(wscMsg, L"%player", (const wchar_t*)Players.GetActiveCharacterName(iClientID));
		PrintLocalUserCmdText(iClientID, wscMsg, set_iLocalChatRange);
		
		return true;
	}

	time_t filetime_to_timet(const FILETIME& ft)
	{
		ULARGE_INTEGER ull;
		ull.LowPart = ft.dwLowDateTime;
		ull.HighPart = ft.dwHighDateTime;
		return ull.QuadPart / 10000000ULL - 11644473600ULL;
	}

	// Move the ship's starting position randomly if it has been logged out in space.
	void HyperJump::PlayerLaunch(unsigned int iShip, unsigned int iClientID)
	{
		static const uint MAX_DRIFT = 50000;

		// Find the time this ship was last online.
		wstring wscTimeStamp = L"";
		if (HkFLIniGet((const wchar_t*)Players.GetActiveCharacterName(iClientID), L"tstamp", wscTimeStamp) != HKE_OK)
			return;

		FILETIME ft;
		ft.dwHighDateTime = strtoul(GetParam(wstos(wscTimeStamp), ',', 0).c_str(), 0, 10);
		ft.dwLowDateTime = strtoul(GetParam(wstos(wscTimeStamp), ',', 1).c_str(), 0, 10);
		time_t lastTime = filetime_to_timet(ft);

		// Get the current time; note FL stores the FILETIME in local time not UTC.
		SYSTEMTIME st;
		GetLocalTime(&st);
		SystemTimeToFileTime(&st, &ft);
		time_t currTime = filetime_to_timet(ft);

		// Calculate the expected drift.
		float drift = (float)(currTime - lastTime);
		wstring wscRights;
		HkGetAdmin((const wchar_t*)Players.GetActiveCharacterName(iClientID), wscRights);
		if (drift > MAX_DRIFT)
			drift = MAX_DRIFT;

		drift *= ((2.0f * rand() / (float)RAND_MAX) - 1.0f);

		// Adjust the ship's position.
		Vector pos = { Players[iClientID].vPosition.x, Players[iClientID].vPosition.y, Players[iClientID].vPosition.z };
		pos.x += drift / 10;
		pos.y += drift;
		pos.z += drift / 10;
		pub::Player::SetInitialPos(iClientID, pos);
	}

	void HyperJump::ExplosionHit(uint iClientID, DamageList *dmg)
	{
		if (mapJumpDrives.find(iClientID) != mapJumpDrives.end())
		{
			if (mapJumpDrives[iClientID].charging_on && mapJumpDrives[iClientID].arch->cd_disrupts_charge)
			{
				if (dmg->get_cause() == DamageCause::CruiseDisrupter || dmg->get_cause() == DamageCause::UnkDisrupter)
				{
					PrintUserCmdText(iClientID, L"Jump drive disrupted. Charging failed.");
					pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jump_drive_disrupted"));
					ShutdownJumpDrive(iClientID);
				}
			}
		}
	}

	bool HyperJump::UserCmd_AcceptBeaconRequest(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
	{
		if (!CheckForBeacon(iClientID))
		{
			PrintUserCmdText(iClientID, L"ERR No beacon equipped!");
			return true;
		}

		uint iTargetClientID = HkGetClientIdFromCharname(wscParam);
		if (iTargetClientID == UINT_MAX)
		{
			uint targetClientID = ToUInt(wscParam);
			if (targetClientID && HkIsValidClientID(targetClientID))
			{
				iTargetClientID = targetClientID;
			}
			else
			{
				PrintUserCmdText(iClientID, L"ERR Player not online");
				return true;
			}
		}

		const auto& beaconRequestsInfo = mapBeaconJumpRequests.find(iTargetClientID);
		if (beaconRequestsInfo == mapBeaconJumpRequests.end()
			|| beaconRequestsInfo->second != iClientID)
		{
			PrintUserCmdText(iClientID, L"ERR No pending jump requests from this player!");
			return true;
		}
		mapBeaconJumpRequests.erase(iTargetClientID);

		auto canJump = CanBeaconJumpToPlayer(iTargetClientID, iClientID);

		if (!canJump.first)
		{
			PrintUserCmdText(iClientID, L"ERR You're out of range for this jump request");
			PrintUserCmdText(iTargetClientID, L"ERR Beacon jump request accepted, but you're out of range");
			ShutdownJumpDrive(iTargetClientID);
			return true;
		}

		if (!CheckBeaconFuel(iClientID, false))
		{
			PrintUserCmdText(iClientID, L"ERR Insufficient fuel");
			PrintUserCmdText(iTargetClientID, L"ERR Beacon jump request accepted, but it's out of fuel");
			ShutdownJumpDrive(iTargetClientID);
			return true;
		}

		auto& beaconData = mapPlayerBeaconMatrix[iClientID];
		beaconData.incomingClientIDs.emplace_back(iTargetClientID);

		JUMPDRIVE& jd = mapJumpDrives[iTargetClientID];

		jd.charging_on = true;
		jd.last_tick_charge = 0;
		jd.curr_charge = 0.0f;
		jd.targetClient = iClientID;
		jd.jumpDistance = canJump.second;

		PrintUserCmdText(iTargetClientID, L"Beacon jump request accepted, charging...");
		pub::Player::SendNNMessage(iTargetClientID, pub::GetNicknameId("nnv_beacon_request_accepted"));
		PrintUserCmdText(iClientID, L"Beacon jump request accepted");
		pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_beacon_request_accepted"));
		return true;
	}

	bool HyperJump::UserCmd_JumpBeacon(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
	{
		// Indicate an error if the command does not appear to be formatted correctly 
		// and stop processing but tell FLHook that we processed the command.
		if (wscParam.empty())
		{
			PrintUserCmdText(iClientID, L"ERR Invalid parameters");
			PrintUserCmdText(iClientID, usage);
			return true;
		}

		// If no ship, report a warning
		IObjInspectImpl *obj = HkGetInspect(iClientID);
		if (!obj)
		{
			PrintUserCmdText(iClientID, L"ERR Can't jump while docked!");
			pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jumpdrive_charging_failed"));
			return true;
		}

		// If no jumpdrive, report a warning.
		if (!InitJumpDriveInfo(iClientID))
		{
			PrintUserCmdText(iClientID, L"ERR Jump drive not equipped");
			pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jumpdrive_charging_failed"));
			return true;
		}

		JUMPDRIVE &jd = mapJumpDrives[iClientID];

		wstring wscCharname = GetParam(wscParam, L' ', 0);
		uint iTargetClientID = HkGetClientIdFromCharname(wscCharname);
		if (iTargetClientID == UINT_MAX)
		{
			uint targetClientID = ToUInt(wscCharname);
			if (targetClientID && HkIsValidClientID(targetClientID))
			{
				iTargetClientID = targetClientID;
			}
			else
			{
				PrintUserCmdText(iClientID, L"ERR Player not online");
				return true;
			}
		}
		obj = HkGetInspect(iTargetClientID);
		if (!obj)
		{
			PrintUserCmdText(iClientID, L"ERR Ship not found");
			pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jumpdrive_not_ready"));
			return true;
		}

		auto canJump = CanBeaconJumpToPlayer(iClientID, iTargetClientID);
		if (!canJump.first)
		{
			PrintUserCmdText(iClientID, L"ERR Can't beacon jump to selected player.");
			pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_jumpdrive_not_ready"));
			return true;
		}

		const wchar_t* charName = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(iClientID));
		PrintUserCmdText(iClientID, L"Sent beacon jump request");
		pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_beacon_request_sent"));
		PrintUserCmdText(iTargetClientID, L"%ls has sent a beacon jump request", charName);
		PrintUserCmdText(iTargetClientID, L"To accept, type /acceptbeacon %u or /acceptbeacon %ls", iClientID, charName);
		pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("nnv_beacon_request_received"));

		mapBeaconJumpRequests[iClientID] = iTargetClientID;

		return true;
	}

	void HyperJump::ForceJump(CUSTOM_JUMP_CALLOUT_STRUCT jumpData)
	{
		mapJumpDrives[jumpData.iClientID].jump_type = jumpData.jumpType;
		SwitchSystem(jumpData.iClientID, jumpData.iSystemID, jumpData.pos, jumpData.ori);
	}
}