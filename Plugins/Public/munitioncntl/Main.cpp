// MunitionControl Plugin - Handle tracking/alert notifications for missile projectiles
// By Aingar
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <unordered_set>
#include <unordered_map>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

unordered_set<uint> setNoTrackingAlertProjectiles;

unordered_map<uint, uint> mapTrackingByObjTypeBlacklistBitmap;

struct MineInfo
{
	float armingTime = 0.0f;
	float dispersionAngle = 0.0f;
	bool detonateOnEndLifetime = false;
	bool stopSpin = false;
};

unordered_map<uint, float> guidedArmingTimesMap;
unordered_map<uint, MineInfo> mineInfoMap;

uint lastProcessedProjectile = 0;

struct ShieldState
{
	bool shieldState;
	ShieldSource changeSource;
	mstime boostUntil;
	float damageReduction;
};

ShieldState playerShieldState[MAX_CLIENT_ID + 1];

struct ShieldSyncData
{
	uint client;
	uint targetClient;
	uint count = 0;
};

vector<ShieldSyncData> shieldStateUpdateMap;

struct ShieldBoostData
{
	float durationPerBattery;
	float damageReduction;
	uint fuseId;
};

unordered_map<uint, ShieldBoostData> shieldBoostMap;

struct shieldBoostFuseInfo
{
	uint fuseId;
	mstime lastUntil;
};
unordered_map<uint, shieldBoostFuseInfo> shieldFuseMap;

constexpr uint shipObjType = (Fighter | Freighter | Transport | Gunboat | Cruiser | Capital);

struct EngineProperties
{
	bool ignoreCDWhenEKd = false;
	float engineKillCDSpeedLimit;
};
unordered_map<uint, EngineProperties> engineData;

enum TRACKING_STATE {
	TRACK_ALERT,
	TRACK_NOALERT,
	NOTRACK_NOALERT
};

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
						float armingTime = ini.get_value_float(0);
						if (armingTime > 0.0f)
						{
							guidedArmingTimesMap[currNickname] = armingTime;
						}
					}
					else if (ini.is_value("no_tracking_alert") && ini.get_value_bool(0))
					{
						setNoTrackingAlertProjectiles.insert(CreateID(ini.get_value_string(0)));
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

						mapTrackingByObjTypeBlacklistBitmap[currNickname] = blacklistedTrackingTypesBitmap;
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
						sb.damageReduction = ini.get_value_float(1);
						sb.fuseId = CreateID(ini.get_value_string(2));
						FoundValue = true;
					}
				}
				if (FoundValue)
				{
					shieldBoostMap[currNickname] = sb;
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

void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	HANDLE servHandle = GetModuleHandle("server.dll");
	HANDLE commonHandle = GetModuleHandle("common.dll");
	PatchCallAddr((char*)servHandle, 0xD921, (char*)PlayerFireRemoveAmmoDetour);
	PatchCallAddr((char*)commonHandle, 0x4CB81, (char*)MineSpin);
	PatchCallAddr((char*)commonHandle, 0x4CAF1, (char*)MineImpulse);
	PlayerFireRemoveAmmoFunc = (PlayerFireRemoveAmmo)(DWORD(servHandle) + 0x6F260);

	ReadMunitionDataFromInis();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProcessGuided(FLPACKET_CREATEGUIDED& createGuidedPacket)
{
	uint ownerType;
	pub::SpaceObj::GetType(createGuidedPacket.iOwner, ownerType);
	if (!(ownerType & shipObjType)) //GetTarget throws an exception for non-ship entities.
	{
		return;
	}
	uint targetId;
	pub::SpaceObj::GetTarget(createGuidedPacket.iOwner, targetId);

	TRACKING_STATE tracking = TRACK_ALERT;

	if (!targetId) // prevent missiles from tracking cloaked ships, and missiles sticking targeting to last selected target
	{
		tracking = NOTRACK_NOALERT;
	}
	else if (setNoTrackingAlertProjectiles.count(createGuidedPacket.iMunitionId)) // for 'dumbified' seeker missiles, disable alert, used for flaks and snub dumbfires
	{
		tracking = TRACK_NOALERT;
	}
	else if (mapTrackingByObjTypeBlacklistBitmap.count(createGuidedPacket.iMunitionId)) // disable tracking for selected ship types
	{
		uint targetType;
		pub::SpaceObj::GetType(createGuidedPacket.iTargetId, targetType);
		const auto& blacklistedShipTypeTargets = mapTrackingByObjTypeBlacklistBitmap.at(createGuidedPacket.iMunitionId);
		if (blacklistedShipTypeTargets & targetType)
		{
			tracking = NOTRACK_NOALERT;
		}
	}

	switch (tracking)
	{
		case NOTRACK_NOALERT:
		{
			CGuided* projectile = reinterpret_cast<CGuided*>(CObject::Find(createGuidedPacket.iProjectileId, CObject::CGUIDED_OBJECT));
			projectile->Release();
			projectile->set_target(nullptr); //disable tracking, switch fallthrough to also disable alert
		}
		case TRACK_NOALERT:
		{
			createGuidedPacket.iTargetId = 0; // prevents the 'incoming missile' warning client-side
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

	if (guidedArmingTimesMap.count(iobj->cobj->archetype->iArchID))
	{
		float armingTime = guidedArmingTimesMap.at(iobj->cobj->archetype->iArchID);
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

	mstime currTime = timeInMS();
	vector<uint> keysToRemove;
	for (auto& shieldFuse : shieldFuseMap)
	{
		if (shieldFuse.second.lastUntil < currTime)
		{
			IObjInspectImpl* iobj1;
			uint dummy;
			GetShipInspect(Players[shieldFuse.first].iShipID, iobj1, dummy);
			IObjRW* iobj = reinterpret_cast<IObjRW*>(iobj1);
			if (iobj)
			{
				HkUnLightFuse(iobj, shieldFuse.second.fuseId, 0.0f);
			}
			keysToRemove.emplace_back(shieldFuse.first);
		}
	}
	for (uint key : keysToRemove)
	{
		shieldFuseMap.erase(key);
	}
}

void __stdcall ExplosionHit(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg)
{
	returncode = DEFAULT_RETURNCODE;
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

	mstime boostDuration = 0;
	float boostReduction = 0.0f;
	uint fuse = 0;
	bool isFirst = true;
	while (shield = eqManager.Traverse(tr))
	{
		const auto& shieldData = shieldBoostMap.find(shield->archetype->iArchID);
		if (shieldData == shieldBoostMap.end())
		{
			continue;
		}

		if (isFirst)
		{
			isFirst = false;
			fuse = shieldData->second.fuseId;
		}
		boostDuration += shieldData->second.durationPerBattery;
		boostReduction += shieldData->second.damageReduction;
	}

	if (!boostDuration || !boostReduction)
	{
		return;
	}

	shieldState.damageReduction = min(1.0f, boostReduction);

	mstime currTime = timeInMS();
	if (shieldState.boostUntil && shieldState.boostUntil > currTime)
	{
		shieldState.boostUntil += boostDuration;
	}
	else
	{
		shieldState.boostUntil = currTime + boostDuration;
	}

	const auto& usedItem = reinterpret_cast<const CECargo*>(eqManager.FindByID(p1.sItemId));
	int currBattCount = 0;
	if (usedItem)
	{
		currBattCount = usedItem->count;
	}
	uint usedAmount = p1.sAmountUsed - currBattCount;
	boostDuration *= usedAmount * 1000;

	IObjInspectImpl* iobj2;
	uint dummy;
	GetShipInspect(Players[iClientID].iShipID, iobj2, dummy);

	if (!iobj2)
	{
		return;
	}

	IObjRW* iobj = reinterpret_cast<IObjRW*>(iobj2);
	HkLightFuse(iobj, fuse, 0.0f, 0.0f, 0.0f);
	shieldFuseMap[iClientID] = { fuse, shieldState.boostUntil };
}

void __stdcall ShipShieldDamage(IObjRW* iobj, float& dmg)
{
	returncode = DEFAULT_RETURNCODE;

	uint clientId = iobj->cobj->ownerPlayer;
	if (!iobj->cobj->ownerPlayer)
	{
		return;
	}

	auto& shieldState = playerShieldState[clientId];
	if (!shieldState.damageReduction)
	{
		return;
	}
	if (!shieldState.boostUntil)
	{
		return;
	}
	mstime currTime = timeInMS();
	if (shieldState.boostUntil > currTime)
	{
		dmg *= 1.0f - shieldState.damageReduction;
	}
	else
	{
		shieldState.boostUntil = 0;
		shieldState.damageReduction = 0;
	}
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
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CreateGuided, PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_CREATEGUIDED, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&MineDestroyed, PLUGIN_MineDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&GuidedDestroyed, PLUGIN_GuidedDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CreatePlayerShip, PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_CREATESHIP_PLAYER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseEnter, PLUGIN_HkIServerImpl_PlayerLaunch_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect_AFTER, PLUGIN_HkIServerImpl_CharacterSelect_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Timer, PLUGIN_HkTimerCheckKick, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExplosionHit, PLUGIN_ExplosionHit, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UseItemRequest, PLUGIN_HkIServerImpl_SPRequestUseItem, -1));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UseItemRequest_AFTER, PLUGIN_HkIServerImpl_SPRequestUseItem_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipShieldDamage, PLUGIN_ShipShieldDmg, 0));
	
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Plugin_Communication_CallBack, PLUGIN_Plugin_Communication, 2));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));

	return p_PI;
}
