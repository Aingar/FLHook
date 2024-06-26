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

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

unordered_set<uint> setNoTrackingAlertProjectiles;

unordered_map<uint, uint> mapTrackingByObjTypeBlacklistBitmap;

struct MineInfo
{
	float armingTime = 0.0f;
	bool detonateOnEndLifetime = false;
};

unordered_map<uint, float> guidedArmingTimesMap;
unordered_map<uint, MineInfo> mineArmingTimesMap;

uint lastProcessedProjectile = 0;

struct ShieldState
{
	bool shieldState;
	ShieldSource changeSource;
};

ShieldState playerShieldState[MAX_CLIENT_ID + 1];

struct ShieldSyncData
{
	uint client;
	uint targetClient;
	uint count = 0;
};

vector<ShieldSyncData> shieldStateUpdateMap;

constexpr uint shipObjType = (Fighter | Freighter | Transport | Gunboat | Cruiser | Capital);

bool debug = false;

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
						mineArmingTimesMap[currNickname].detonateOnEndLifetime = ini.get_value_bool(0);
					}
					else if (ini.is_value("mine_arming_time"))
					{
						mineArmingTimesMap[currNickname].armingTime = ini.get_value_float(0);
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

void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	HANDLE servHandle = GetModuleHandle("server.dll");
	PatchCallAddr((char*)servHandle, 0xD921, (char*)PlayerFireRemoveAmmoDetour);
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
		if (debug)
		{
			ConPrint(L"Projectile %x notarget notrack\n", createGuidedPacket.iMunitionId);
		}
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
			if (debug)
			{
				ConPrint(L"Projectile %08x notarget notrack\n", createGuidedPacket.iMunitionId);
				ConPrint(L"filter %08x target %08x\n", blacklistedShipTypeTargets, targetType);
			}
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

	auto& mineInfo = mineArmingTimesMap.find(mineArch->iArchID);
	if (mineInfo != mineArmingTimesMap.end())
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
}

#define IS_CMD(a) !args.compare(L##a)
#define RIGHT_CHECK(a) if(!(cmd->rights & a)) { cmd->Print(L"ERR No permission\n"); return true; }
bool ExecuteCommandString_Callback(CCmds* cmd, const wstring& args)
{
	returncode = DEFAULT_RETURNCODE;

	if (IS_CMD("munitiondebug"))
	{
		debug = !debug;
		ConPrint(L"munitioncntl debug %u\n", (uint)debug);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return false;
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

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Plugin_Communication_CallBack, PLUGIN_Plugin_Communication, 2));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));

	return p_PI;
}
