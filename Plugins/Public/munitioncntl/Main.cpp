// MunitionControl Plugin - Handle tracking/alert notifications for missile projectiles
// By Aingar
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "MunitionCntl.h"

constexpr int ARMOR_MOD = 100;
/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

unordered_map<uint, GuidedData> guidedDataMap;
unordered_map<uint, MineInfo> mineInfoMap;
ShieldState playerShieldState[MAX_CLIENT_ID + 1];
vector<ShieldSyncData> shieldStateUpdateMap;
unordered_map<uint, ShieldBoostData> shieldBoostMap;
unordered_map<uint, ShieldBoostFuseInfo> shieldFuseMap;
unordered_map<uint, EngineProperties> engineData;
unordered_map<uint, ExplosionDamageData> explosionTypeMap;
unordered_map<uint, unordered_map<ushort, int>> shipArmorMap;
unordered_map<uint, unordered_map<ushort, int>>::iterator shipArmorIter;
unordered_map<uint, unordered_map<ushort, int>> solarArmorMap;
unordered_map<uint, unordered_map<ushort, int>>::iterator solarArmorIter;
unordered_map<uint, MunitionData> munitionArmorPenMap;
Archetype::Explosion* shieldExplosion;
vector<float> armorReductionVector;

unordered_map<uint, unordered_map<ushort, BurstFireGunData>> shipGunData;
unordered_map<uint, BurstFireData> burstGunData;

unordered_map<uint, ShipData> shipDataMap;

unordered_map<uint, unordered_map<uint, uint>> equipOverrideMap;

vector<pair<uint, uint>> equipUpdateVector;

unordered_map<uint, uint> NewMissileUpdateMap;
unordered_map<uint, InvulData> invulMap;

float guidedZoneImpulseDrag = 30.f;

struct SpeedCheck
{
	float targetSpeed = 0;
	uint checkCounter = 0;
};
unordered_map<uint, SpeedCheck> topSpeedWatch;

struct ImpulseData
{
	float baseForce;
	float logMult;
};

unordered_map<uint, ImpulseData> zoneImpulseData;

uint lastProcessedProjectile = 0;

void LoadSettings();

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	// If we're being loaded from the command line while FLHook is running then
	// set_scCfgFile will not be empty so load the settings as FLHook only
	// calls load settings on FLHook startup and .rehash.
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		if (set_scCfgFile.length() > 0)
		{
			LoadSettings();
		}
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
	}
	return true;
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

void ReadMunitionDataFromInis()
{
	INI_Reader ini;

	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string currDir = string(szCurDir);
	string scFreelancerIniFile = currDir + R"(\freelancer.ini)";
	string scUniverseIniFile = currDir + R"(\..\DATA\UNIVERSE\universe.ini)";

	string gameDir = currDir.substr(0, currDir.length() - 4);
	gameDir += string(R"(\DATA\)");

	if (!ini.open(scFreelancerIniFile.c_str(), false))
	{
		return;
	}

	vector<string> equipFiles;
	vector<string> shipFiles;
	vector<string> solarFiles;
	vector<string> systemFiles;

	while (ini.read_header())
	{
		if (!ini.is_header("Data"))
		{
			continue;
		}
		while (ini.read_value())
		{
			if (ini.is_value("equipment"))
			{
				equipFiles.emplace_back(ini.get_value_string());
			}
			else if (ini.is_value("ships"))
			{
				shipFiles.emplace_back(ini.get_value_string());
			}
			else if (ini.is_value("solar"))
			{
				solarFiles.emplace_back(ini.get_value_string());
			}
		}
	}

	ini.close();

	if (ini.open(scUniverseIniFile.c_str(), false))
	{
		
		while (ini.read_header())
		{
			if (!ini.is_header("system"))
			{
				continue;
			}

			while (ini.read_value())
			{
				if (ini.is_value("file"))
				{
					systemFiles.push_back(ini.get_value_string());
					break;
				}
			}
		}
		ini.close();
	}

	for (string& sysFile : systemFiles)
	{
		string fileDir = currDir + R"(\..\DATA\UNIVERSE\)" + sysFile;
		if (!ini.open(fileDir.c_str(), false))
		{
			continue;
		}

		while (ini.read_header())
		{
			if (!ini.is_header("zone"))
			{
				continue;
			}

			uint nickname;
			while (ini.read_value())
			{
				if (ini.is_value("nickname"))
				{
					nickname = CreateID(ini.get_value_string(0));
				}
				else if (ini.is_value("custom_impulse"))
				{
					zoneImpulseData[nickname] = { ini.get_value_float(0), ini.get_value_float(1) };
					break;
				}
			}
		}

		ini.close();
	}

	int maxArmorValue = 0;
	for (string& shipFile : shipFiles)
	{
		shipFile = gameDir + shipFile;
		if (!ini.open(shipFile.c_str(), false))
		{
			continue;
		}

		uint currNickname = 0;
		ushort currSID = 0;
		while (ini.read_header())
		{
			if (ini.is_header("Ship"))
			{
				currSID = 3;
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						currNickname = CreateID(ini.get_value_string());
					}
					else if (ini.is_value("armor"))
					{
						int armorValue = ini.get_value_int(0);
						shipArmorMap[currNickname][1] = armorValue;
						maxArmorValue = max(maxArmorValue, armorValue);
						break;
					}
				}
			}
			else if (ini.is_header("CollisionGroup"))
			{
				currSID++;
				while (ini.read_value())
				{
					if (ini.is_value("hp_disable"))
					{
						shipDataMap[currNickname].colGrpHpMap[currSID].push_back(ini.get_value_string());
					}
					else if (ini.is_value("fuse"))
					{
						uint fuseHash = CreateID(ini.get_value_string(0));
						int index = 3;
						while (true)
						{
							string HpName = ini.get_value_string(index++);
							if (HpName.empty())
							{
								break;
							}
							shipDataMap[currNickname].fuseHpMap[fuseHash].push_back(HpName);
						}
					}
					else if (ini.is_value("armor"))
					{
						int armorValue = ini.get_value_int(0);
						shipArmorMap[currNickname][currSID] = armorValue;
						maxArmorValue = max(maxArmorValue, armorValue);
						break;
					}
				}
			}
		}

		ini.close();
	}


	for (string& solarFile : solarFiles)
	{
		solarFile = gameDir + solarFile;
		if (!ini.open(solarFile.c_str(), false))
		{
			continue;
		}

		uint currNickname = 0;
		ushort currSID = 0;
		while (ini.read_header())
		{
			if (ini.is_header("Solar"))
			{
				currSID = 3;
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						currNickname = CreateID(ini.get_value_string());
					}
					else if (ini.is_value("armor"))
					{
						int armorValue = ini.get_value_int(0);
						solarArmorMap[currNickname][1] = armorValue;
						maxArmorValue = max(maxArmorValue, armorValue);
						break;
					}
				}
			}
			else if (ini.is_header("CollisionGroup"))
			{
				currSID++;
				while (ini.read_value())
				{
					if (ini.is_value("armor"))
					{
						int armorValue = ini.get_value_int(0);
						solarArmorMap[currNickname][currSID] = armorValue;
						maxArmorValue = max(maxArmorValue, armorValue);
						break;
					}
				}
			}
		}

		ini.close();
	}

	armorReductionVector.reserve(maxArmorValue);
	for (int i = 0; i <= maxArmorValue; ++i)
	{
		armorReductionVector.emplace_back(1.0f - (static_cast<float>(i) / (i + ARMOR_MOD)));
	}

	for (string& equipFile : equipFiles)
	{
		equipFile = gameDir + equipFile;
		if (!ini.open(equipFile.c_str(), false))
		{
			continue;
		}

		uint currNickname;
		uint explosion_arch;
		while (ini.read_header())
		{
			if (ini.is_header("Gun"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						currNickname = CreateID(ini.get_value_string(0));
					}
					else if (ini.is_value("burst_fire"))
					{
						float baseRefire = ((Archetype::Gun*)Archetype::GetEquipment(currNickname))->fRefireDelay;
						burstGunData[currNickname] = { ini.get_value_int(0), baseRefire - ini.get_value_float(1) };
					}
				}
			}
			else if (ini.is_header("Mine"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						currNickname = CreateID(ini.get_value_string());
					}
					else if (ini.is_value("self_detonate"))
					{
						mineInfoMap[currNickname].detonateOnEndLifetime = ini.get_value_bool(0);
					}
					else if (ini.is_value("mine_arming_time"))
					{
						mineInfoMap[currNickname].armingTime = ini.get_value_float(0);
					}
					else if (ini.is_value("stop_spin"))
					{
						mineInfoMap[currNickname].stopSpin = ini.get_value_bool(0);
					}
					else if (ini.is_value("dispersion_angle"))
					{
						mineInfoMap[currNickname].dispersionAngleMax = ini.get_value_float(0) / (180.f / 3.14f);
						mineInfoMap[currNickname].dispersionAngleMin = ini.get_value_float(1) / (180.f / 3.14f);
					}
					else if (ini.is_value("explosion_arch"))
					{
						explosion_arch = CreateID(ini.get_value_string(0));
					}
					else if (ini.is_value("detonation_dist"))
					{
						explosionTypeMap[explosion_arch].detDist = ini.get_value_float(0);
					}
					else if (ini.is_value("disable_proximity_fuse"))
					{
						mineInfoMap[currNickname].disableContactExplosion = ini.get_value_bool(0);
					}
				}
			}
			else if (ini.is_header("Munition"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						currNickname = CreateID(ini.get_value_string());
					}
					else if (ini.is_value("armor_pen"))
					{
						munitionArmorPenMap[currNickname].armorPen = ini.get_value_int(0);
					}
					else if (ini.is_value("percentage_damage_hull"))
					{
						munitionArmorPenMap[currNickname].percentageHullDmg = ini.get_value_float(0);
					}
					else if (ini.is_value("percentage_damage_shield"))
					{
						munitionArmorPenMap[currNickname].percentageShieldDmg = ini.get_value_float(0);
					}
					else if (ini.is_value("percentage_damage_energy"))
					{
						munitionArmorPenMap[currNickname].percentageEnergyDmg = ini.get_value_float(0);
					}
					else if (ini.is_value("arming_time"))
					{
						guidedDataMap[currNickname].armingTime = ini.get_value_float(0);
					}
					else if (ini.is_value("no_tracking_alert") && ini.get_value_bool(0))
					{
						guidedDataMap[currNickname].noTrackingAlert = true;
					}
					else if (ini.is_value("tracking_blacklist"))
					{
						uint blacklistedTrackingTypesBitmap = 0;
						string typeStr = ToLower(ini.get_value_string(0));
						if (typeStr.find("fighter") != string::npos)
							blacklistedTrackingTypesBitmap |= Fighter;
						if (typeStr.find("freighter") != string::npos)
							blacklistedTrackingTypesBitmap |= Freighter;
						if (typeStr.find("transport") != string::npos)
							blacklistedTrackingTypesBitmap |= Transport;
						if (typeStr.find("gunboat") != string::npos)
							blacklistedTrackingTypesBitmap |= Gunboat;
						if (typeStr.find("cruiser") != string::npos)
							blacklistedTrackingTypesBitmap |= Cruiser;
						if (typeStr.find("capital") != string::npos)
							blacklistedTrackingTypesBitmap |= Capital;
						if (typeStr.find("guided") != string::npos)
							blacklistedTrackingTypesBitmap |= Guided;
						if (typeStr.find("mine") != string::npos)
								blacklistedTrackingTypesBitmap |= Mine;

						guidedDataMap[currNickname].trackingBlacklist = blacklistedTrackingTypesBitmap;
					}
					else if (ini.is_value("top_speed"))
					{
						guidedDataMap[currNickname].topSpeed = ini.get_value_float(0) * ini.get_value_float(0);
					}
					else if (ini.is_value("explosion_arch"))
					{
						explosion_arch = CreateID(ini.get_value_string(0));
					}
					else if (ini.is_value("detonation_dist"))
					{
						explosionTypeMap[explosion_arch].detDist = ini.get_value_float(0);
					}
				}
			}
			else if (ini.is_header("Engine"))
			{
				EngineProperties ep;
				bool FoundValue = false;
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						currNickname = CreateID(ini.get_value_string());
					}
					else if (ini.is_value("disruptor_engine_kill") && !ini.get_value_bool(0))
					{
						ep.ignoreCDWhenEKd = true;
						FoundValue = true;
					}
					else if (ini.is_value("disruptor_engine_kill_speed_limit"))
					{
						ep.engineKillCDSpeedLimit = ini.get_value_float(0);
						FoundValue = true;
					}
					else if (ini.is_value("hp_type"))
					{
						ep.hpType = ini.get_value_string(0);
						FoundValue = true;
					}
				}
				if (FoundValue)
				{
					engineData[currNickname] = ep;
				}
			}
			else if (ini.is_header("ShieldGenerator"))
			{
				ShieldBoostData sb;
				bool FoundValue = false;

				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						currNickname = CreateID(ini.get_value_string());
					}
					else if (ini.is_value("shield_boost"))
					{
						sb.durationPerBattery = ini.get_value_float(0);
						sb.minimumDuration = ini.get_value_float(1);
						sb.maximumDuration = ini.get_value_float(2);
						sb.damageReduction = ini.get_value_float(3);
						sb.fuseId = CreateID(ini.get_value_string(4));
						FoundValue = true;
					}
					else if (ini.is_value("shield_boost_explosion"))
					{
						sb.hullBaseDamage = ini.get_value_float(0);
						sb.hullReflectDamagePercentage = ini.get_value_float(1);
						sb.hullDamageCap = ini.get_value_float(2);
						sb.energyBaseDamage = ini.get_value_float(3);
						sb.energyReflectDamagePercentage = ini.get_value_float(4);
						sb.energyDamageCap = ini.get_value_float(5);
						sb.radius = ini.get_value_float(6);
						sb.explosionFuseId = CreateID(ini.get_value_string(7));
					}
				}
				if (FoundValue)
				{
					shieldBoostMap[currNickname] = sb;
				}
			}
			else if (ini.is_header("Explosion"))
			{
				ExplosionDamageData damageType;
				bool foundItem = false;
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						currNickname = CreateID(ini.get_value_string());
					}
					else if (ini.is_value("weapon_type"))
					{
						damageType.weaponType = CreateID(ini.get_value_string(0));
						foundItem = true;
					}
					else if (ini.is_value("damage_solars"))
					{
						damageType.damageSolars = ini.get_value_bool(0);
						foundItem = true;
					}
					else if (ini.is_value("armor_pen"))
					{
						damageType.armorPen = ini.get_value_int(0);
						foundItem = true;
					}
					else if (ini.is_value("percentage_damage_hull"))
					{
						damageType.percentageDamageHull = ini.get_value_float(0);
						foundItem = true;
					}
					else if (ini.is_value("percentage_damage_shield"))
					{
						damageType.percentageDamageShield = ini.get_value_float(0);
						foundItem = true;
					}
					else if (ini.is_value("percentage_damage_energy"))
					{
						damageType.percentageDamageEnergy = ini.get_value_float(0);
						foundItem = true;
					}
					else if (ini.is_value("cruise_disruptor"))
					{
						damageType.cruiseDisrupt = ini.get_value_bool(0);
						foundItem = true;
					}
					else if (ini.is_value("destroy_missiles"))
					{
						damageType.missileDestroy = ini.get_value_bool(0);
						foundItem = true;
					}
				}
				if (foundItem)
				{
					explosionTypeMap[currNickname] = damageType;
				}
			}
		}
		ini.close();
	}

	if (ini.open((gameDir + "\\FX\\explosions.ini").c_str(), false))
	{
		while (ini.read_header())
		{
			if (!ini.is_header("explosion"))
			{
				continue;
			}
			uint currNickname;
			ExplosionDamageData damageType;
			bool foundItem = false;
			while (ini.read_value())
			{
				if (ini.is_value("nickname"))
				{
					currNickname = CreateID(ini.get_value_string());
				}
				else if (ini.is_value("weapon_type"))
				{
					damageType.weaponType = CreateID(ini.get_value_string(0));
					foundItem = true;
				}
				else if (ini.is_value("damage_solars"))
				{
					damageType.damageSolars = ini.get_value_bool(0);
					foundItem = true;
				}
				else if (ini.is_value("armor_pen"))
				{
					damageType.armorPen = ini.get_value_int(0);
					foundItem = true;
				}
				else if (ini.is_value("percentage_damage_hull"))
				{
					damageType.percentageDamageHull = ini.get_value_float(0);
					foundItem = true;
				}
				else if (ini.is_value("percentage_damage_shield"))
				{
					damageType.percentageDamageShield = ini.get_value_float(0);
					foundItem = true;
				}
				else if (ini.is_value("percentage_damage_energy"))
				{
					damageType.percentageDamageEnergy = ini.get_value_float(0);
					foundItem = true;
				}
				else if (ini.is_value("cruise_disruptor"))
				{
					damageType.cruiseDisrupt = ini.get_value_bool(0);
					foundItem = true;
				}
				else if (ini.is_value("destroy_missiles"))
				{
					damageType.missileDestroy = ini.get_value_bool(0);
					foundItem = true;
				}
			}
			if (foundItem)
			{
				explosionTypeMap[currNickname] = damageType;
			}
		}
		ini.close();
	}

	for (string& shipFile : shipFiles)
	{
		string filename = gameDir + shipFile;
		if (!ini.open(filename.c_str(), false))
		{
			continue;
		}

		while (ini.read_header())
		{
			if (!ini.is_header("Ship"))
			{
				continue;
			}

			unordered_set<string> shipEngineHPs;
			while (ini.read_value())
			{
				uint currNickname;
				if (ini.is_value("nickname"))
				{
					currNickname = CreateID(ini.get_value_string(0));
					shipEngineHPs.clear();
				}
				else if (ini.is_value("equip_override"))
				{
					equipOverrideMap[CreateID(ini.get_value_string(0))][currNickname] = CreateID(ini.get_value_string(1));
				}
				else if (ini.is_value("internal_engine"))
				{
					shipDataMap[currNickname].internalEngine = ini.get_value_bool(0);
				}
				else if (ini.is_value("hp_type"))
				{
					string equipType = ini.get_value_string(0);
					int i = 1;
					while (i < 10)
					{
						string hardpointName = ini.get_value_string(i);
						if (hardpointName.empty())
						{
							break;
						}
						if (hardpointName.find("HpEngine") != string::npos)
						{
							if (!shipEngineHPs.count(hardpointName))
							{
								shipEngineHPs.insert(hardpointName);
								shipDataMap[currNickname].engineCount++;
							}
							shipDataMap[currNickname].engineHpMap[equipType].insert(hardpointName);
						}

						i++;
					}
				}
			}
		}

		ini.close();
	}
}

struct SrvGun
{
	void* vtable;
	CELauncher* launcher;
};

typedef void(__thiscall* PlayerFireRemoveAmmo)(PlayerData* pd, uint archId, int amount, float hp, bool syncPlayer);
PlayerFireRemoveAmmo PlayerFireRemoveAmmoFunc;

void __fastcall PlayerFireRemoveAmmoDetour(PlayerData* pd, void* edx, uint archId, int amount, float hp, bool syncPlayer)
{
	SrvGun** SrvGunPtr = (SrvGun**)(DWORD(&archId) + 0x14);
	
	CELauncher* launcher = ((*SrvGunPtr)->launcher);
	PlayerFireRemoveAmmoFunc(pd, archId, launcher->GetProjectilesPerFire(), hp, syncPlayer);
}

void MineSpin(CMine* mine, Vector& spinVec)
{
	auto mineInfo = mineInfoMap.find(mine->archetype->iArchID);
	if (mineInfo == mineInfoMap.end() || !mineInfo->second.stopSpin)
	{
		PhySys::AddToAngularVelocityOS(mine, spinVec);
	}
}

void MineImpulse(CMine* mine, Vector& launchVec)
{
	using UpdateZonesType = void(__thiscall*)(CSimple*, float, uint);
	static_cast<UpdateZonesType>((void*)0x62B5D00)(mine, 0.0f, 0);

	auto mineInfo = mineInfoMap.find(mine->archetype->iArchID);
	if (mineInfo != mineInfoMap.end() && mineInfo->second.dispersionAngleMax > 0.0f)
	{
		IObjRW* obj;
		StarSystem* sys;
		GetShipInspect(mine->ownerId, obj, sys);
		if (!obj)
		{
			return;
		}

		Vector shipVelocityVec = obj->get_velocity();

		launchVec.x -= shipVelocityVec.x;
		launchVec.y -= shipVelocityVec.y;
		launchVec.z -= shipVelocityVec.z;


		Vector randVecAxis = RandomVector(1.0f);

		Vector vxp = VectorCross(randVecAxis, launchVec);
		Vector vxvxp = VectorCross(randVecAxis, vxp);

		float angle = mineInfo->second.dispersionAngleMax - mineInfo->second.dispersionAngleMin;
		angle *= rand() % 10000 / 10000.f;
		angle += mineInfo->second.dispersionAngleMin;

		vxp = VectorMultiply(vxp, sinf(angle));
		vxvxp = VectorMultiply(vxvxp, 1.0f - cosf(angle));

		launchVec.x += vxp.x + vxvxp.x + shipVelocityVec.x;
		launchVec.y += vxp.y + vxvxp.y + shipVelocityVec.y;
		launchVec.z += vxp.z + vxvxp.z + shipVelocityVec.z;
	}

	PhySys::AddToVelocity(mine, launchVec);
}

void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	HANDLE servHandle = GetModuleHandle("server.dll");
	HANDLE commonHandle = GetModuleHandle("common.dll");
	PatchCallAddr((char*)servHandle, 0xD921, (char*)PlayerFireRemoveAmmoDetour);
	PatchCallAddr((char*)commonHandle, 0x4CB81, (char*)MineSpin);
	PatchCallAddr((char*)commonHandle, 0x4CAF1, (char*)MineImpulse);
	PlayerFireRemoveAmmoFunc = (PlayerFireRemoveAmmo)(DWORD(servHandle) + 0x6F260);

	uint addr = (uint)GetWeaponModifier;
	WriteProcMem((char*)servHandle + 0x8426C, &addr, sizeof(addr));

	ReadMunitionDataFromInis();
	LoadHookOverrides();

	ID_String str;
	str.id = CreateID("explosion_positron_discharge");
	shieldExplosion = Archetype::GetExplosion(str);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FindAndDisableEquip(uint client, const string& hardpoint)
{
	for (auto& equip : Players[client].equipDescList.equip)
	{
		if (strcmp(equip.szHardPoint.value, hardpoint.c_str()) == 0)
		{
			XActivateEquip aq;
			aq.bActivate = false;
			aq.iSpaceID = Players[client].iShipID;
			aq.sID = equip.sID;

			HookClient->Send_FLPACKET_COMMON_ACTIVATEEQUIP(client, aq);
			Server.ActivateEquip(client, aq);
			return;
		}
	}
}

bool VerifyEngines(uint client)
{
	CShip* cship = ClientInfo[client].cship;

	if (!cship)
	{
		return true;
	}

	const auto& equip = Players[client].equipDescList;

	const auto& shipHpDataIter = shipDataMap.find(Players[client].iShipArchetype);
	if (shipHpDataIter == shipDataMap.end())
	{
		int counter = 0;
		CEquipTraverser tr(Engine);
		CEEngine* cequip;
		while (cequip = reinterpret_cast<CEEngine*>(cship->equip_manager.Traverse(tr)))
		{
			counter++;
		}
		if (counter == 1)
		{
			return true;
		}
		return false;
	}

	auto& shipHpData = shipHpDataIter->second;

	int mountedEngineCounter = 0;

	bool internalEngineFound = false;

	CEquipTraverser tr(Engine);
	CEEngine* cequip;
	while (cequip = reinterpret_cast<CEEngine*>(cship->equip_manager.Traverse(tr)))
	{
		auto equipDesc = equip.find_equipment_item(cequip->iSubObjId);
		string hardpoint = equipDesc->szHardPoint.value;

		if (hardpoint == "BAY")
		{
			if (!internalEngineFound && (shipHpData.internalEngine || shipHpData.engineHpMap.empty()))
			{
				internalEngineFound = true;
				continue;
			}
			
			return false;
		}

		mountedEngineCounter++;

		auto engineType = engineData.find(cequip->archetype->iArchID);
		if (engineType == engineData.end())
		{
			return false;
		}

		auto hpMapIter = shipHpData.engineHpMap.find(engineType->second.hpType);
		if (hpMapIter == shipHpData.engineHpMap.end())
		{
			return false;
		}

		if (!hpMapIter->second.count(hardpoint))
		{
			return false;
		}
	}

	if (mountedEngineCounter != shipHpData.engineCount)
	{
		return false;
	}

	return true;
}

void ProcessGuided(FLPACKET_CREATEGUIDED& createGuidedPacket)
{
	CGuided* guided = reinterpret_cast<CGuided*>(CObject::Find(createGuidedPacket.iProjectileId, CObject::CGUIDED_OBJECT));
	if (!guided)
	{
		return;
	}

	guided->Release();

	using UpdateZonesType = void(__thiscall*)(CSimple*, float, uint);
	static_cast<UpdateZonesType>((void*)0x62B5D00)(guided, 0.0f, 0);

	auto guidedInfo = guidedDataMap.find(guided->archetype->iArchID);
	if (guidedInfo == guidedDataMap.end())
	{
		return;
	}

	if (guidedInfo->second.noTrackingAlert) // for 'dumbified' seeker missiles, disable alert, used for flaks and snub dumbfires
	{
		createGuidedPacket.iTargetId = 0; // prevents the 'incoming missile' warning client-side
	}

}

void __stdcall CreateGuided(uint& iClientID, FLPACKET_CREATEGUIDED& createGuidedPacket)
{
	returncode = DEFAULT_RETURNCODE;

	//Packet hooks are executed once for every player in range, but we only need to process the missile packet once, since it's passed by reference.
	if (lastProcessedProjectile != createGuidedPacket.iProjectileId)
	{
		lastProcessedProjectile = createGuidedPacket.iProjectileId;
		ProcessGuided(createGuidedPacket);
	}

}

void GuidedInit(CGuided* guided, CGuided::CreateParms& parms)
{
	returncode = DEFAULT_RETURNCODE;

	NewMissileUpdateMap[parms.id] = { 0 };

	uint objId = parms.ownerId;
	IObjRW* owner;
	StarSystem* dummy;
	GetShipInspect(objId, owner, dummy);
	if (owner)
	{
		IObjRW* ownerTarget = nullptr;
		owner->get_target(ownerTarget);
		if (!ownerTarget)
		{
			parms.target = nullptr;
			parms.subObjId = 0;
		}
	}

	auto guidedData = guidedDataMap.find(guided->archetype->iArchID);
	if (guidedData == guidedDataMap.end())
	{
		return;
	}

	auto& guidedInfo = guidedData->second;

	if (guidedInfo.trackingBlacklist && parms.target)
	{
		if (parms.target->cobj->type & guidedInfo.trackingBlacklist)
		{
			parms.target = nullptr;
			parms.subObjId = 0;
		}
	}

	if (guidedInfo.topSpeed)
	{
		topSpeedWatch[parms.id] = { guidedInfo.topSpeed, 0 };
	}

}

void __stdcall ShipDestroyed(IObjRW* iobj, bool isKill, uint killerId)
{
	returncode = DEFAULT_RETURNCODE;

	shipGunData.erase(iobj->cobj->id);
}

bool __stdcall MineDestroyed(IObjRW* iobj, DestroyType& destroyType, uint killerId, uint callSource)
{
	returncode = DEFAULT_RETURNCODE;

	CMine* mine = reinterpret_cast<CMine*>(iobj->cobj);
	Archetype::Mine* mineArch = reinterpret_cast<Archetype::Mine*>(mine->archetype);

	auto& mineInfo = mineInfoMap.find(mineArch->iArchID);
	if (mineInfo != mineInfoMap.end())
	{
		if (mineInfo->second.disableContactExplosion && callSource == 0x6CF3853 && destroyType == DestroyType::FUSE) // contact or lifetime expiration
		{
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return false;
		}
		if (mineArch->fLifeTime - mine->remainingLifetime < mineInfo->second.armingTime)
		{
			destroyType = DestroyType::VANISH;
			return true;
		}

		if (mineInfo->second.detonateOnEndLifetime)
		{
			destroyType = DestroyType::FUSE;
		}
	}
	return true;
}

bool __stdcall GuidedDestroyed(IObjRW* iobj, DestroyType& destroyType, uint killerId)
{
	returncode = DEFAULT_RETURNCODE;

	NewMissileUpdateMap.erase(iobj->cobj->id);

	auto guidedInfo = guidedDataMap.find(iobj->cobj->archetype->iArchID);
	if (guidedInfo == guidedDataMap.end())
	{
		return true;
	}

	if (guidedInfo->second.armingTime)
	{
		float armingTime = guidedInfo->second.armingTime;
		CGuided* guided = reinterpret_cast<CGuided*>(iobj->cobj);
		if (guided->lifetime < armingTime)
		{
			destroyType = DestroyType::VANISH;
		}
	}
	return true;
}

void CreatePlayerShip(uint client, FLPACKET_CREATESHIP& pShip)
{
	returncode = DEFAULT_RETURNCODE;

	shieldStateUpdateMap.push_back({ client, pShip.clientId, 0 });
}

void Plugin_Communication_CallBack(PLUGIN_MESSAGE msg, void* data)
{
	returncode = DEFAULT_RETURNCODE;
	if (msg == CUSTOM_SHIELD_STATE_CHANGE)
	{
		CUSTOM_SHIELD_CHANGE_STATE_STRUCT* info = reinterpret_cast<CUSTOM_SHIELD_CHANGE_STATE_STRUCT*>(data);

		CShip* cship = ClientInfo[info->client].cship;
		if (!cship)
		{
			return;
		}

		if (!playerShieldState[info->client].shieldState
			&& playerShieldState[info->client].changeSource != ShieldSource::UNSET
			&& playerShieldState[info->client].changeSource != info->source)
		{
			return;
		}

		XActivateEquip eq;
		eq.iSpaceID = cship->id;
		eq.bActivate = info->newState;

		CEquipTraverser tr(ShieldGenerator);
		CEShieldGenerator* shield;

		while (shield = reinterpret_cast<CEShieldGenerator*>(cship->equip_manager.Traverse(tr)))
		{
			auto shieldGenArch = shield->ShieldGenArch();
			if (shieldGenArch->fRebuildPowerDraw <= 0.0f && shieldGenArch->fConstantPowerDraw <= 0.0f && shieldGenArch->fRegenerationRate == 0.0f)
			{
				continue;
			}
			eq.sID = shield->iSubObjId;
			HookClient->Send_FLPACKET_COMMON_ACTIVATEEQUIP(info->client, eq);
			Server.ActivateEquip(info->client, eq);
		}
		playerShieldState[info->client].shieldState = info->newState;
		playerShieldState[info->client].changeSource = info->source;
		info->success = true;
	}
}


void __stdcall CharacterSelect_AFTER(struct CHARACTER_ID const& charId, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	playerShieldState[iClientID].shieldState = true;
	for (auto iter = shieldStateUpdateMap.begin(); iter != shieldStateUpdateMap.end();)
	{
		if (iter->targetClient == iClientID)
		{
			iter = shieldStateUpdateMap.erase(iter);
		}
		else
		{
			++iter;
		}
	}
}

void UnmountEngines(uint client)
{
	for(auto& eq : Players[client].equipDescList.equip)
	{
		auto equipArch = Archetype::GetEquipment(eq.iArchID);
		if (!equipArch)
		{
			continue;
		}
		if (equipArch->get_class_type() == Archetype::AClassType::ENGINE)
		{
			eq.bMounted = false;
		}
	}
}

void PlayerLaunch_After(unsigned int shipId, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	playerShieldState[iClientID] = ShieldState();
	shieldFuseMap.erase(iClientID);

	for (auto iter = shieldStateUpdateMap.begin(); iter != shieldStateUpdateMap.end();)
	{
		if (iter->targetClient == iClientID)
		{
			iter = shieldStateUpdateMap.erase(iter);
		}
		else
		{
			++iter;
		}
	}

	if (!VerifyEngines(iClientID))
	{
		HkBeamById(iClientID, Players[iClientID].iLastBaseID);
		PrintUserCmdText(iClientID, L"ERR Invalid engine(s) detected. You will be kicked to unmount the engines.");
		UnmountEngines(iClientID);
		Plugin_Communication(CUSTOM_BEAM_LAST_BASE, &iClientID);
		HkDelayedKick(iClientID, 5);
	}

	auto cship = ClientInfo[iClientID].cship;
	if (cship)
	{
		float rootColGrpHpLoss = 0.0f;
		auto currColGrp = cship->shiparch()->collisiongroup;
		while (currColGrp)
		{
			if (!currColGrp->rootHealthProxy)
			{
				currColGrp = currColGrp->next;
				continue;
			}

			auto foundColGrp = cship->archGroupManager.FindByID(currColGrp->id);
			if (!foundColGrp)
			{
				rootColGrpHpLoss += (float)currColGrp->hitPts;
			}
			else
			{
				rootColGrpHpLoss += foundColGrp->hitPts - (float)currColGrp->hitPts;
			}

			currColGrp = currColGrp->next;
		}

		float maxAllowedHp = cship->shiparch()->fHitPoints - rootColGrpHpLoss;
		if (cship->hitPoints > maxAllowedHp)
		{
			pub::SpaceObj::SetRelativeHealth(cship->id, maxAllowedHp / cship->shiparch()->fHitPoints);
		}
	}

	equipUpdateVector.push_back({ iClientID, 0 });
}

void Timer()
{
	static unordered_map<uint, vector<pair<ushort, bool>>*> eqSids;
	for (auto iter = shieldStateUpdateMap.begin(); iter != shieldStateUpdateMap.end();)
	{
		++iter->count;
		if (iter->count > 1)
		{
			XActivateEquip eq;

			CShip* cship = ClientInfo[iter->targetClient].cship;
			if (!cship)
			{
				iter = shieldStateUpdateMap.erase(iter);
				continue;
			}
			eq.iSpaceID = cship->id;

			vector<pair<ushort, bool>>* sids;
			auto sidsIter = eqSids.find(iter->targetClient);
			if (sidsIter != eqSids.end())
			{
				sids = sidsIter->second;
			}
			else
			{
				sids = new vector<pair<ushort, bool>>();
				CEquipTraverser tr(ShieldGenerator);
				CEquip* shield;

				while (shield = cship->equip_manager.Traverse(tr))
				{
					sids->push_back({ shield->iSubObjId, shield->isActive });
				}
				eqSids[iter->targetClient] = sids;
			}

			CEquip* shield = cship->equip_manager.FindFirst(Shield);
			if (shield && shield->GetHitPoints() != shield->GetMaxHitPoints())
			{
				DamageList dmg;
				dmg.add_damage_entry(shield->iSubObjId, shield->GetHitPoints(), DamageEntry::SubObjFate(0));
				HookClient->Send_FLPACKET_SERVER_DAMAGEOBJECT(iter->client, cship->id, dmg);
			}

			for (auto sid : *sids)
			{
				eq.sID = sid.first;
				eq.bActivate = sid.second;
				HookClient->Send_FLPACKET_COMMON_ACTIVATEEQUIP(iter->client, eq);
			}
		}

		if (iter->count > 2)
		{
			iter = shieldStateUpdateMap.erase(iter);
		}
		else
		{
			iter++;
		}
	}
	for (auto iter : eqSids)
	{
		delete iter.second;
	}
	eqSids.clear();

	for (auto& iter = equipUpdateVector.begin(); iter != equipUpdateVector.end();)
	{
		if (!Players[iter->first].iShipID)
		{
			iter = equipUpdateVector.erase(iter);
			continue;
		}

		iter->second++;
		if (iter->second <= 3)
		{
			auto shipDataIter = shipDataMap.find(Players[iter->first].iShipArchetype);
			if (shipDataIter == shipDataMap.end() || shipDataIter->second.colGrpHpMap.empty())
			{
				iter = equipUpdateVector.erase(iter);
				continue;
			}

			for (auto& colGrp : Players[iter->first].collisionGroupDesc)
			{
				if (colGrp.health > 0.0f)
				{
					continue;
				}
				auto& colGrpData = shipDataIter->second.colGrpHpMap.find(colGrp.id);
				if (colGrpData == shipDataIter->second.colGrpHpMap.end())
				{
					continue;
				}

				for (auto& hp : colGrpData->second)
				{
					FindAndDisableEquip(iter->first, hp);
				}
			}
			iter = equipUpdateVector.erase(iter);
			continue;
		}

		iter++;
	}

	returncode = DEFAULT_RETURNCODE;
}

int Update()
{
	for (auto iter = topSpeedWatch.begin(); iter != topSpeedWatch.end(); )
	{
		auto iGuided = HkGetInspectObj(iter->first);
		if (!iGuided)
		{
			iter = topSpeedWatch.erase(iter);
			continue;
		}

		CGuided* guided = reinterpret_cast<CGuided*>(iGuided->cobj);
		SpeedCheck& speedData = iter->second;

		Vector velocityVec = guided->get_velocity();
		float velocity = SquaredVectorMagnitude(velocityVec);

		if (velocity > speedData.targetSpeed * 1.02f)
		{
			ResizeVector(velocityVec, sqrt(speedData.targetSpeed));
			guided->motorData = nullptr;

			const uint physicsPtr = *reinterpret_cast<uint*>(PCHAR(*reinterpret_cast<uint*>(uint(guided) + 84)) + 152);
			Vector* linearVelocity = reinterpret_cast<Vector*>(physicsPtr + 164);
			*linearVelocity = velocityVec;
		}
		iter++;
	}

	for (auto iter = NewMissileUpdateMap.begin(); iter != NewMissileUpdateMap.end();)
	{

		uint counter = ++iter->second;

		if (counter == 1)
		{
			iter++;
			continue;
		}

		uint id = iter->first;
		IObjRW* guided = nullptr;
		StarSystem* starSystem;
		GetShipInspect(id, guided, starSystem);

		if (!guided)
		{
			iter = NewMissileUpdateMap.erase(iter);
			continue;
		}

		SSPObjUpdateInfoSimple ssp;
		ssp.iShip = iter->first;
		ssp.vPos = guided->cobj->vPos;
		ssp.vDir = HkMatrixToQuaternion(guided->cobj->mRot);
		ssp.throttle = 0;
		ssp.state = 0;
		
		for(auto& observer : starSystem->observerList)
		{
			ssp.fTimestamp = static_cast<float>(observer.timestamp);

			HookClient->Send_FLPACKET_COMMON_UPDATEOBJECT(observer.clientId, ssp);
		}

		if (counter >= 3)
		{
			iter = NewMissileUpdateMap.erase(iter);
		}
		else
		{
			iter++;
		}
	}

	if (shieldFuseMap.empty())
	{
		returncode = DEFAULT_RETURNCODE;
		return 0;
	}

	mstime currTime = timeInMS();
	static vector<uint> keysToRemove;
	for (auto& shieldFuse : shieldFuseMap)
	{
		if (shieldFuse.second.lastUntil > currTime)
		{
			continue;
		}

		keysToRemove.emplace_back(shieldFuse.first);
		IObjRW* iobj;
		StarSystem* dummy;
		GetShipInspect(Players[shieldFuse.first].iShipID, iobj, dummy);
		if (!iobj)
		{
			continue;
		}
		HkUnLightFuse(iobj, shieldFuse.second.boostData->fuseId, 0.0f);

		const ShieldBoostData* boostData = shieldFuse.second.boostData;

		if (boostData->radius == 0.0f)
		{
			continue;
		}

		shieldExplosion->fRadius = boostData->radius;
		shieldExplosion->fHullDamage = min(boostData->hullDamageCap, boostData->hullBaseDamage + boostData->hullReflectDamagePercentage * playerShieldState[shieldFuse.first].damageTaken);
		shieldExplosion->fEnergyDamage = min(boostData->energyDamageCap, boostData->energyBaseDamage + boostData->energyReflectDamagePercentage * playerShieldState[shieldFuse.first].damageTaken);

		auto starSystem = StarSystemMap->find(Players[shieldFuse.first].iSystemID);
		ExplosionDamageEvent expl;
		expl.attackerId = Players[shieldFuse.first].iShipID;
		expl.projectileId = expl.attackerId;
		expl.dmgCause = DamageCause::Mine;
		expl.explosionPosition = iobj->cobj->vPos;
		expl.explosionArchetype = shieldExplosion;
		expl.dunno = 0;
		TriggerExplosionFunc(&starSystem->second, &expl);

		if (boostData->explosionFuseId)
		{
			HkUnLightFuse(iobj, boostData->explosionFuseId, 0.0f);
			HkLightFuse(iobj, boostData->explosionFuseId, 0.0f, 0.0f, -1.0f);
		}
	}

	for (uint key : keysToRemove)
	{
		shieldFuseMap.erase(key);
	}
	keysToRemove.clear();

	returncode = DEFAULT_RETURNCODE;
	return 0;
}

int shipArmorRating = 0;
uint shipArmorArch = 0;
int solarArmorRating = 0;
uint solarArmorArch = 0;


MunitionData* weaponMunitionData = nullptr;
uint weaponMunitionDataArch = 0;

bool armorEnabled = 0;

void __stdcall ShipHullDamage(IObjRW* iobj, float& incDmg, DamageList* dmg)
{
	returncode = DEFAULT_RETURNCODE;

	if (armorEnabled)
	{
		FetchShipArmor(iobj->cobj->archetype->iArchID);

		int finalArmorValue = shipArmorRating;
		if (weaponMunitionData)
		{
			incDmg += iobj->cobj->archetype->fHitPoints * weaponMunitionData->percentageHullDmg;
			finalArmorValue = max(0, finalArmorValue - weaponMunitionData->armorPen);
		}

		if (finalArmorValue)
		{
			incDmg *= armorReductionVector.at(finalArmorValue);
		}
	}

	armorEnabled = false;

	if (iobj->is_player())
	{
		auto cship = iobj->cobj;
		auto invulIter = invulMap.find(cship->id);
		if (invulIter == invulMap.end() || invulIter->second.invulType == InvulType::EQUIPONLY)
		{
			return;
		}

		float minHpAllowed = invulIter->second.minHpPerc * cship->archetype->fHitPoints;
		if (cship->hitPoints <= minHpAllowed)
		{
			incDmg = 0;
			return;
		}

		float hpPercPostDamage = cship->hitPoints - incDmg;
		incDmg = min(incDmg, cship->hitPoints - minHpAllowed);
	}
}

bool usedBatts = false;
void __stdcall UseItemRequest(SSPUseItem const& p1, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	const static uint BATTERY_ARCH_ID = CreateID("ge_s_battery_01");
	usedBatts = false;

	if (!ClientInfo[iClientID].cship)
	{
		return;
	}
	const auto& eqManager = ClientInfo[iClientID].cship->equip_manager;
	const auto& usedItem = reinterpret_cast<const CECargo*>(eqManager.FindByID(p1.sItemId));
	if (!usedItem)
	{
		return;
	}
	uint itemArchId = usedItem->archetype->iArchID;
	if (itemArchId == BATTERY_ARCH_ID)
	{
		usedBatts = true;
	}
}

void __stdcall UseItemRequest_AFTER(SSPUseItem const& p1, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;

	if (!usedBatts)
	{
		return;
	}
	usedBatts = false;

	ShieldState& shieldState = playerShieldState[iClientID];
	shieldState = ShieldState();

	const auto& eqManager = ClientInfo[iClientID].cship->equip_manager;
	CEquipTraverser tr(ShieldGenerator);
	const CEquip* shield;

	const ShieldBoostData* primaryBoost = nullptr;

	while (shield = eqManager.Traverse(tr))
	{
		const auto& shieldData = shieldBoostMap.find(shield->archetype->iArchID);
		if (shieldData == shieldBoostMap.end())
		{
			continue;
		}

		primaryBoost = &shieldData->second;
		break;
	}

	if (!primaryBoost)
	{
		return;
	}

	shieldState.damageReduction = min(1.0f, primaryBoost->damageReduction);

	const auto& usedItem = reinterpret_cast<const CECargo*>(eqManager.FindByID(p1.sItemId));
	int currBattCount = 0;
	if (usedItem)
	{
		currBattCount = usedItem->count;
	}
	uint usedAmount = p1.sAmountUsed - currBattCount;

	float boostDuration = primaryBoost->durationPerBattery * usedAmount;

	if (boostDuration < primaryBoost->minimumDuration)
	{
		return;
	}

	boostDuration = min(primaryBoost->maximumDuration, boostDuration);
	boostDuration *= 1000;

	mstime currTime = timeInMS();
	if (shieldState.boostUntil && shieldState.boostUntil > currTime)
	{
		shieldState.boostUntil += static_cast<mstime>(boostDuration);
	}
	else
	{
		shieldState.boostUntil = currTime + static_cast<mstime>(boostDuration);
	}

	ShieldBoostFuseInfo& boostInfo = shieldFuseMap[iClientID];
	boostInfo.boostData = primaryBoost;
	boostInfo.lastUntil = shieldState.boostUntil;

	if (!primaryBoost->fuseId)
	{
		return;
	}

	IObjRW* iobj;
	StarSystem* dummy;
	GetShipInspect(Players[iClientID].iShipID, iobj, dummy);

	if (!iobj)
	{
		return;
	}

	HkUnLightFuse(iobj, primaryBoost->fuseId, 0.0f);
	HkLightFuse(iobj, primaryBoost->fuseId, 0.0f, 0.0f, -1.0f);
}

void __stdcall ReqAddItem(uint& goodId, char const* hardpoint, int count, float status, bool& mounted, unsigned int clientId)
{
	returncode = DEFAULT_RETURNCODE;
	if (mounted && strcmp(hardpoint, "BAY") == 0)
	{
		auto equip = Archetype::GetEquipment(goodId);
		if (equip->get_class_type() == Archetype::AClassType::ENGINE)
		{
			mounted = false;
		}
	}

	auto overrideMapIter = equipOverrideMap.find(goodId);
	if (overrideMapIter == equipOverrideMap.end())
	{
		return;
	}

	auto shipOverrideIter = overrideMapIter->second.find(Players[clientId].iShipArchetype);
	if (shipOverrideIter == overrideMapIter->second.end())
	{
		return;
	}
	goodId = shipOverrideIter->second;
	
}

void ShipColGrpDestroyed(IObjRW* iobj, CArchGroup* colGrp, DamageEntry::SubObjFate fate, DamageList* dmgList)
{
	returncode = DEFAULT_RETURNCODE;

	CShip* cship = reinterpret_cast<CShip*>(iobj->cobj);

	if (!cship->ownerPlayer)
	{
		return;
	}

	auto shipDataIter = shipDataMap.find(cship->archetype->iArchID);
	if (shipDataIter == shipDataMap.end())
	{
		return;
	}

	auto colGrpDataIter = shipDataIter->second.colGrpHpMap.find(colGrp->colGrp->id);
	if (colGrpDataIter == shipDataIter->second.colGrpHpMap.end())
	{
		return;
	}

	for (auto& hp : colGrpDataIter->second)
	{
		FindAndDisableEquip(cship->ownerPlayer, hp);
	}
}

void CSimpleUpdateHook(CSimple* csimple, float deltaTime)
{
	returncode = DEFAULT_RETURNCODE;

	if (!csimple->currentDamageZone)
	{
		return;
	}

	auto zoneDataIter = zoneImpulseData.find(csimple->currentDamageZone->iZoneID);
	if (zoneDataIter == zoneImpulseData.end())
	{
		return;
	}

	Vector relativePosition = VectorSubtract(csimple->vPos, csimple->currentDamageZone->vPos);
	float distanceFromCenter = VectorMagnitude(relativePosition);
	if (distanceFromCenter < 30)
	{
		return;
	}
	float relativeDepth = csimple->currentDamageZone->vSize.x / distanceFromCenter;
	float distanceMult = powf(relativeDepth, zoneDataIter->second.logMult);
	float mass = csimple->archetype->fMass;
	float force = -zoneDataIter->second.baseForce * mass * distanceMult * deltaTime;

	ResizeVector(relativePosition, 1.0f);
	relativePosition.x *= force;
	relativePosition.y *= force;
	relativePosition.z *= force;
	csimple->add_impulse(relativePosition);
}

int __fastcall CMineUpdate(CMine* mine, void* edx, float deltatime, uint dunno)
{
	using CMineUpdateType = int(__thiscall*)(CMine*, float, uint);
	int retVal = static_cast<CMineUpdateType>((void*)0x62ACA00)(mine, deltatime, dunno);

	CSimpleUpdateHook(mine, deltatime);

	return retVal;
}
int __fastcall CGuidedUpdate(CGuided* guided, void* edx, float deltatime, uint dunno)
{
	using CMineUpdateType = int(__thiscall*)(CGuided*, float, uint);
	int retVal = static_cast<CMineUpdateType>((void*)0x62ACD40)(guided, deltatime, dunno);

	CSimpleUpdateHook(guided, deltatime);

	return retVal;
}

#define IS_CMD(a) !args.compare(L##a)
#define RIGHT_CHECK(a) if(!(cmd->rights & a)) { cmd->Print(L"ERR No permission\n"); return true; }
bool ExecuteCommandString_Callback(CCmds* cmd, const wstring& args)
{
	returncode = DEFAULT_RETURNCODE;

	if (IS_CMD("setinvul"))
	{
		RIGHT_CHECK(RIGHT_SPECIAL2)

		HKPLAYERINFO adminPlyr;
		if (HkGetPlayerInfo(cmd->GetAdminName(), adminPlyr, false) != HKE_OK)
		{
			cmd->Print(L"ERR Not in space\n");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}

		auto targetName = cmd->ArgCharname(1);
		HKPLAYERINFO targetPlyr;
		if (HkGetPlayerInfo(targetName, targetPlyr, false) != HKE_OK || targetPlyr.iShip == 0)
		{
			cmd->Print(L"ERR Player not found or not in space\n");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}

		auto equipDmgStr = ToLower(cmd->ArgStr(2));

		InvulType invulType;
		if (equipDmgStr == L"all")
		{
			invulType = InvulType::ALL;
		}
		else if (equipDmgStr == L"hull")
		{
			invulType = InvulType::HULLONLY;
		}
		else if (equipDmgStr == L"equip")
		{
			invulType = InvulType::EQUIPONLY;
		}
		else
		{
			cmd->Print(L"ERR invulnerability type variable must be 'all', 'hull' or 'equip'\n");
			cmd->Print(L"Syntax: .setinvul <target> <invulType> <min HP percentage>\n");
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return true;
		}

		auto percentage = cmd->ArgInt(3);
		float floatPerc = 0.01f * (float)percentage;
		floatPerc = min(floatPerc, 1.0f);
		floatPerc = max(floatPerc, 0.0f);

		if (floatPerc == 0.0f)
		{
			invulMap.erase(targetPlyr.iShip);
			cmd->Print(L"Invul off\n");
		}
		else if (floatPerc == 1.0f)
		{
			invulMap[targetPlyr.iShip] = { floatPerc, invulType };
			switch (invulType)
			{
			case InvulType::ALL:
				cmd->Print(L"Full invul set\n");
				break;
			case InvulType::HULLONLY:
				cmd->Print(L"Full hull-only invul set\n");
				break;
			case InvulType::EQUIPONLY:
				cmd->Print(L"Full equip-only invul set\n");
				break;
			}
		}
		else
		{
			invulMap[targetPlyr.iShip] = { floatPerc, invulType };
			switch (invulType)
			{
			case InvulType::ALL:
				cmd->Print(L"Invul set, can't go below %d%% HP\n", percentage);
				break;
			case InvulType::HULLONLY:
				cmd->Print(L"Hull-only invul set, can't go below %d%% HP\n", percentage);
				break;
			case InvulType::EQUIPONLY:
				cmd->Print(L"Equip-only invul set, can't go below %d%% HP\n", percentage);
				break;
			}
		}
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}
	return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions to hook
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Munition Controller";
	p_PI->sShortName = "munitioncntl";
	p_PI->bMayPause = false;
	p_PI->bMayUnload = false;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CreateGuided, PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_CREATEGUIDED, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&GuidedInit, PLUGIN_HkIEngine_CGuided_init, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&MineDestroyed, PLUGIN_MineDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipDestroyed, PLUGIN_ShipDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&GuidedDestroyed, PLUGIN_GuidedDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipColGrpDestroyed, PLUGIN_ShipColGrpDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CreatePlayerShip, PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_CREATESHIP_PLAYER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch_After, PLUGIN_HkIServerImpl_PlayerLaunch_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect_AFTER, PLUGIN_HkIServerImpl_CharacterSelect_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipExplosionHit, PLUGIN_ExplosionHit, 10));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipHullDamage, PLUGIN_ShipHullDmg, 20));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipShieldDamage, PLUGIN_ShipShieldDmg, 20));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UseItemRequest, PLUGIN_HkIServerImpl_SPRequestUseItem, -1));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UseItemRequest_AFTER, PLUGIN_HkIServerImpl_SPRequestUseItem_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ReqAddItem, PLUGIN_HkIServerImpl_ReqAddItem, 0));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Timer, PLUGIN_HkTimerCheckKick, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Update, PLUGIN_HkIServerImpl_Update, 0));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Plugin_Communication_CallBack, PLUGIN_Plugin_Communication, 2));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));

	return p_PI;
}
