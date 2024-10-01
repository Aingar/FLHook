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

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

unordered_map<uint, GuidedData> guidedDataMap;
unordered_map<uint, MineInfo> mineInfoMap;
ShieldState playerShieldState[MAX_CLIENT_ID + 1];
vector<ShieldSyncData> shieldStateUpdateMap;
unordered_map<uint, ShieldBoostData> shieldBoostMap;
unordered_map<uint, ShieldBoostFuseInfo> shieldFuseMap;
unordered_map<uint, EngineProperties> engineData;
unordered_map<uint, ExplosionDamageType> explosionTypeMap;
Archetype::Explosion* shieldExplosion;

unordered_map<uint, pair<CGuided*, float>> topSpeedWatch;

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

	string gameDir = currDir.substr(0, currDir.length() - 4);
	gameDir += string(R"(\DATA\)");

	if (!ini.open(scFreelancerIniFile.c_str(), false))
	{
		return;
	}

	vector<string> equipFiles;

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
		}
	}

	ini.close();

	for (string equipFile : equipFiles)
	{
		equipFile = gameDir + equipFile;
		if (!ini.open(equipFile.c_str(), false))
		{
			continue;
		}

		uint currNickname;
		while (ini.read_header())
		{
			if (ini.is_header("Mine"))
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
						mineInfoMap[currNickname].dispersionAngle = ini.get_value_float(0) / (180.f / 3.14f);
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
						guidedDataMap[currNickname].topSpeed = ini.get_value_float(0);
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
				ExplosionDamageType damageType;
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						currNickname = CreateID(ini.get_value_string());
					}
					else if (ini.is_value("weapon_type"))
					{
						damageType.type = CreateID(ini.get_value_string(0));
					}
				}
				if (damageType.type)
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
			if (ini.is_header("explosion"))
			{
				uint currNickname;
				ExplosionDamageType damageType;
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						currNickname = CreateID(ini.get_value_string());
					}
					else if (ini.is_value("weapon_type"))
					{
						damageType.type = CreateID(ini.get_value_string(0));
					}
				}
				if (damageType.type)
				{
					explosionTypeMap[currNickname] = damageType;
				}
			}
		}
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
	auto mineInfo = mineInfoMap.find(mine->archetype->iArchID);
	if (mineInfo != mineInfoMap.end() && mineInfo->second.dispersionAngle > 0.0f)
	{
		Vector randVecAxis = RandomVector(1.0f);

		Vector vxp = VectorCross(randVecAxis, launchVec);
		Vector vxvxp = VectorCross(randVecAxis, vxp);

		float angle = mineInfo->second.dispersionAngle;
		angle *= rand() % 10000 / 10000.f;

		vxp = VectorMultiply(vxp, sinf(angle));
		vxvxp = VectorMultiply(vxvxp, 1.0f - cosf(angle));

		launchVec.x += vxp.x + vxvxp.x;
		launchVec.y += vxp.y + vxvxp.y;
		launchVec.z += vxp.z + vxvxp.z;
	}

	PhySys::AddToVelocity(mine, launchVec);
}

static FlMap<uint, FlMap<uint, float>>* shieldResistMap = (FlMap<uint, FlMap<uint, float>>*)(0x658A9C0);

float __fastcall GetWeaponModifier(CEShield* shield, void* edx, uint& weaponType)
{
	if (!weaponType || !shield || !shield->highestToughnessShieldGenArch)
	{
		return 1.0f;
	}
	auto shieldResistIter = shieldResistMap->find(weaponType);
	if (shieldResistIter == shieldResistMap->end())
	{
		return 1.0f;
	}

	auto shieldResistMap2 = shieldResistIter.value();
	auto shieldResistIter2 = shieldResistMap2->find(shield->highestToughnessShieldGenArch->iShieldTypeID);
	if (shieldResistIter2 == shieldResistMap2->end() || !shieldResistIter2.key())
	{
		return 1.0f;
	}

	return *shieldResistIter2.value();
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
	str.id = CreateID("missile03_mark04_explosion");
	shieldExplosion = Archetype::GetExplosion(str);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProcessGuided(FLPACKET_CREATEGUIDED& createGuidedPacket)
{
	CGuided* guided = reinterpret_cast<CGuided*>(CObject::Find(createGuidedPacket.iProjectileId, CObject::CGUIDED_OBJECT));
	if (!guided)
	{
		return;
	}

	uint ownerType;
	pub::SpaceObj::GetType(createGuidedPacket.iOwner, ownerType);
	if (!(ownerType & shipObjType)) //GetTarget throws an exception for non-ship entities.
	{
		return;
	}

	uint targetId;
	pub::SpaceObj::GetTarget(createGuidedPacket.iOwner, targetId);

	if (!targetId)
	{
		guided->set_target(nullptr); //disable tracking, switch fallthrough to also disable alert
	}

	auto guidedInfo = guidedDataMap.find(createGuidedPacket.iMunitionId);
	if (guidedInfo == guidedDataMap.end())
	{
		return;
	}

	if (guidedInfo->second.topSpeed)
	{
		topSpeedWatch[createGuidedPacket.iProjectileId] = { guided, guidedInfo->second.topSpeed };
	}

	if (guidedInfo->second.noTrackingAlert) // for 'dumbified' seeker missiles, disable alert, used for flaks and snub dumbfires
	{
		createGuidedPacket.iTargetId = 0; // prevents the 'incoming missile' warning client-side
	}
	else if (guidedInfo->second.trackingBlacklist) // disable tracking for selected ship types
	{
		uint targetType;
		pub::SpaceObj::GetType(createGuidedPacket.iTargetId, targetType);
		if (guidedInfo->second.trackingBlacklist & targetType)
		{
			guided->set_target(nullptr); //disable tracking, switch fallthrough to also disable alert
		}
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

int __stdcall MineDestroyed(IObjRW* iobj, bool isKill, uint killerId)
{
	returncode = DEFAULT_RETURNCODE;

	CMine* mine = reinterpret_cast<CMine*>(iobj->cobj);
	Archetype::Mine* mineArch = reinterpret_cast<Archetype::Mine*>(mine->archetype);

	auto& mineInfo = mineInfoMap.find(mineArch->iArchID);
	if (mineInfo != mineInfoMap.end())
	{
		if (mineArch->fLifeTime - mine->remainingLifetime < mineInfo->second.armingTime)
		{
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return 2;
		}

		if (mineInfo->second.detonateOnEndLifetime)
		{
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return 1;
		}
	}
	return 0;
}

bool __stdcall GuidedDestroyed(IObjRW* iobj, bool isKill, uint killerId)
{
	returncode = DEFAULT_RETURNCODE;

	topSpeedWatch.erase(iobj->get_id());

	auto guidedInfo = guidedDataMap.find(iobj->get_id());
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
			returncode = SKIPPLUGINS_NOFUNCTIONCALL;
			return false;
		}
	}
	return true;
}

void CreatePlayerShip(uint client, FLPACKET_CREATESHIP& pShip)
{
	returncode = DEFAULT_RETURNCODE;

	if (!playerShieldState[pShip.clientId].shieldState)
	{
		shieldStateUpdateMap.push_back({ client, pShip.clientId, 0 });
	}
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
		CEquip* shield;

		while (shield = cship->equip_manager.Traverse(tr))
		{
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

void BaseEnter(unsigned int iBaseID, unsigned int iClientID)
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
}

void Timer()
{
	static unordered_map<uint, vector<ushort>*> eqSids;
	for (auto iter = shieldStateUpdateMap.begin(); iter != shieldStateUpdateMap.end();)
	{
		++iter->count;
		if (iter->count > 1)
		{
			XActivateEquip eq;
			eq.bActivate = false;

			CShip* cship = ClientInfo[iter->targetClient].cship;
			if (!cship)
			{
				iter = shieldStateUpdateMap.erase(iter);
				continue;
			}
			eq.iSpaceID = cship->id;

			vector<ushort>* sids;
			auto sidsIter = eqSids.find(iter->targetClient);
			if (sidsIter != eqSids.end())
			{
				sids = sidsIter->second;
			}
			else
			{
				sids = new vector<ushort>();
				CEquipTraverser tr(ShieldGenerator);
				CEquip* shield;

				while (shield = cship->equip_manager.Traverse(tr))
				{
					sids->emplace_back(shield->iSubObjId);
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

			for (ushort sid : *sids)
			{
				eq.sID = sid;
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
}

int Update()
{
	returncode = DEFAULT_RETURNCODE;

	for (auto iter = topSpeedWatch.begin(); iter != topSpeedWatch.end();)
	{
		if (!iter->second.first->motorData)
		{
			continue;
		}

		Vector velocityVec = iter->second.first->get_velocity();
		float velocity = VectorMagnitude(velocityVec);
		if (velocity >= iter->second.second)
		{
			ResizeVector(velocityVec, iter->second.second);
			iter->second.first->motorData = nullptr;

			const uint physicsPtr = *reinterpret_cast<uint*>(PCHAR(*reinterpret_cast<uint*>(uint(iter->second.first) + 84)) + 152);
			Vector* linearVelocity = reinterpret_cast<Vector*>(physicsPtr + 164);
			*linearVelocity = velocityVec;

			iter = topSpeedWatch.erase(iter);
		}
		else
		{
			iter++;
		}
	}

	if (shieldFuseMap.empty())
	{
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
		IObjInspectImpl* iobj1;
		StarSystem* dummy;
		GetShipInspect(Players[shieldFuse.first].iShipID, iobj1, dummy);
		IObjRW* iobj = reinterpret_cast<IObjRW*>(iobj1);
		if (!iobj)
		{
			continue;
		}
		HkUnLightFuse(iobj, shieldFuse.second.boostData->fuseId, 0.0f);

		ShieldBoostData* boostData = shieldFuse.second.boostData;

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

	return 0;
}

void __stdcall ExplosionHit(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg)
{
	returncode = DEFAULT_RETURNCODE;

	if (engineData.empty())
	{
		return;
	}

	if (dmg->damageCause != DamageCause::CruiseDisrupter)
	{
		return;
	}
	
	CShip* cship = reinterpret_cast<CShip*>(iobj->cobj);
	if (!cship->ownerPlayer)
	{
		return;
	}

	if (!ClientInfo[cship->ownerPlayer].bEngineKilled)
	{
		return;
	}

	bool isCDImmune = false;
	CEEngine* engine = nullptr;
	CEquipTraverser tr(Engine);
	while (engine = reinterpret_cast<CEEngine*>(cship->equip_manager.Traverse(tr)))
	{
		auto engineDataIter = engineData.find(engine->archetype->iArchID);
		if (engineDataIter == engineData.end() || !engineDataIter->second.ignoreCDWhenEKd)
		{
			continue;
		}

		Vector velocity = cship->get_velocity();

		float velocityMagnitude = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);

		if (velocityMagnitude <= engineDataIter->second.engineKillCDSpeedLimit)
		{
			dmg->damageCause = DamageCause::DummyDisrupter;
		}
		return;
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

	ShieldBoostData* primaryBoost = nullptr;

	while (shield = eqManager.Traverse(tr))
	{
		const auto& shieldData = shieldBoostMap.find(shield->archetype->iArchID);
		if (shieldData == shieldBoostMap.end())
		{
			continue;
		}

		primaryBoost = &shieldData->second;
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

	IObjInspectImpl* iobj2;
	StarSystem* dummy;
	GetShipInspect(Players[iClientID].iShipID, iobj2, dummy);

	if (!iobj2)
	{
		return;
	}

	IObjRW* iobj = reinterpret_cast<IObjRW*>(iobj2);
	HkUnLightFuse(iobj, primaryBoost->fuseId, 0.0f);
	HkLightFuse(iobj, primaryBoost->fuseId, 0.0f, 0.0f, -1.0f);
}

#define IS_CMD(a) !args.compare(L##a)
#define RIGHT_CHECK(a) if(!(cmd->rights & a)) { cmd->Print(L"ERR No permission\n"); return true; }
bool ExecuteCommandString_Callback(CCmds* cmd, const wstring& args)
{
	returncode = DEFAULT_RETURNCODE;

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
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&MineDestroyed, PLUGIN_MineDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&GuidedDestroyed, PLUGIN_GuidedDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CreatePlayerShip, PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_CREATESHIP_PLAYER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseEnter, PLUGIN_HkIServerImpl_PlayerLaunch_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect_AFTER, PLUGIN_HkIServerImpl_CharacterSelect_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExplosionHit, PLUGIN_ExplosionHit, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UseItemRequest, PLUGIN_HkIServerImpl_SPRequestUseItem, -1));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UseItemRequest_AFTER, PLUGIN_HkIServerImpl_SPRequestUseItem_AFTER, 0));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Timer, PLUGIN_HkTimerCheckKick, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Update, PLUGIN_HkIServerImpl_Update, 0));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Plugin_Communication_CallBack, PLUGIN_Plugin_Communication, 2));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));

	return p_PI;
}
