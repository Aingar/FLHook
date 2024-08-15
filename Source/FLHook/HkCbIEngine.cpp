#include "hook.h"

#define ISERVER_LOG() if(set_bDebug) AddDebugLog(__FUNCSIG__);
#define ISERVER_LOGARG_F(a) if(set_bDebug) AddDebugLog("     " #a ": %f", (float)a);
#define ISERVER_LOGARG_UI(a) if(set_bDebug) AddDebugLog("     " #a ": %u", (uint)a);
#define ISERVER_LOGARG_D(a) if(set_bDebug) AddDebugLog("     " #a ": %f", (double)a);
#define ISERVER_LOGARG_I(a) if(set_bDebug) AddDebugLog("     " #a ": %d", (int)a);
#define ISERVER_LOGARG_V(a) if(set_bDebug) AddDebugLog("     " #a ": %f %f %f", (float)a.x, (float)a.y, (float)a.z);


/**************************************************************************************************************
// misc flserver engine function hooks
**************************************************************************************************************/

namespace HkIEngine
{

	bool bAbortEventRequest;
	/**************************************************************************************************************
	// ship create & destroy
	**************************************************************************************************************/

	FARPROC fpOldUpdateCEGun;
	FARPROC fpOldRadarRange = FARPROC(0x6CF7278);
	FARPROC fpOldLoadRepCharFile;

	bool __stdcall CEGun_Update(CEGun* gun)
	{
		if (gun->owner->ownerPlayer)
		{
			return false;
		}
		return true;
	}

	__declspec(naked) void CEGun_Update_naked()
	{
		__asm
		{
			push ecx
			push ecx
			call CEGun_Update
			pop ecx
			test al, al
			jz skipLabel
			jmp fpOldUpdateCEGun
			skipLabel:
			ret 0x8
		}
	}
static float* pNPC_range    = ((float*)0x6d66aec);
static float* pPlayer_range = ((float*)0x6d66af0);
static float* pGroup_range = ((float*)0x6d66af4);
	void __fastcall CheckRange(uint player, CSimple* scannedObject)
	{
		if (!ClientInfo[player].cship)
		{
			return;
		}
		float radarRange = ClientInfo[player].fRadarRange;
		float scannerInterference = ClientInfo[player].cship->get_scanner_interference();
		scannerInterference = max(scannerInterference, scannedObject->get_scanner_interference());
		radarRange *= (1.0f - scannerInterference);
		*pNPC_range = *pPlayer_range = radarRange;
		*pGroup_range = radarRange * 4;
	}

	__declspec(naked) void Radar_Range_naked()
	{
		__asm {
			mov			ecx, [edi + 0x38]
			mov			edx, [esi + 0x10]
			call        CheckRange
			mov			eax, 0
			ret
		}
	}

	struct iobjCache
	{
		StarSystem* cacheStarSystem;
		CObject::Class objClass;
	};

	unordered_set<uint> playerShips;
	unordered_map<uint, iobjCache> cacheSolarIObjs;
	unordered_map<uint, iobjCache> cacheNonsolarIObjs;

	FARPROC FindStarListRet = FARPROC(0x6D0C846);

	PBYTE fpOldStarSystemFind;

	typedef MetaListNode* (__thiscall* FindIObjOnList)(MetaList&, uint searchedId);
	FindIObjOnList FindIObjOnListFunc = FindIObjOnList(0x6CF4F00);

	typedef IObjRW* (__thiscall* FindIObjInSystem)(StarSystemMock& starSystem, uint searchedId);
	FindIObjInSystem FindIObjFunc = FindIObjInSystem(0x6D0C840);

	IObjRW* FindNonSolar(StarSystemMock* starSystem, uint searchedId)
	{
		MetaListNode* node = FindIObjOnListFunc(starSystem->starSystem.shipList, searchedId);
		if (node)
		{
			cacheNonsolarIObjs[searchedId] = { node->value->starSystem, node->value->cobj->objectClass };
			return node->value;
		}
		node = FindIObjOnListFunc(starSystem->starSystem.lootList, searchedId);
		if (node)
		{
			cacheNonsolarIObjs[searchedId] = { node->value->starSystem, node->value->cobj->objectClass };
			return node->value;
		}
		node = FindIObjOnListFunc(starSystem->starSystem.guidedList, searchedId);
		if (node)
		{
			cacheNonsolarIObjs[searchedId] = { node->value->starSystem, node->value->cobj->objectClass };
			return node->value;
		}
		node = FindIObjOnListFunc(starSystem->starSystem.mineList, searchedId);
		if (node)
		{
			cacheNonsolarIObjs[searchedId] = { node->value->starSystem, node->value->cobj->objectClass };
			return node->value;
		}
		node = FindIObjOnListFunc(starSystem->starSystem.counterMeasureList, searchedId);
		if (node)
		{
			cacheNonsolarIObjs[searchedId] = { node->value->starSystem, node->value->cobj->objectClass };
			return node->value;
		}
		return nullptr;
	}

	IObjRW* FindSolar(StarSystemMock* starSystem, uint searchedId)
	{
		MetaListNode* node = FindIObjOnListFunc(starSystem->starSystem.solarList, searchedId);
		if (node)
		{
			cacheSolarIObjs[searchedId] = { node->value->starSystem, node->value->cobj->objectClass };
			return node->value;
		}
		node = FindIObjOnListFunc(starSystem->starSystem.asteroidList, searchedId);
		if (node)
		{
			cacheSolarIObjs[searchedId] = { node->value->starSystem, node->value->cobj->objectClass };
			return node->value;
		}
		return nullptr;
	}

	IObjRW* __stdcall FindInStarList(StarSystemMock* starSystem, uint searchedId)
	{
		static StarSystem* lastFoundInSystem = nullptr;
		static uint lastFoundItem = 0;
		IObjRW* retVal = nullptr;
		
		if (searchedId == 0)
		{
			return nullptr;
		}

		if (lastFoundItem == searchedId && lastFoundInSystem != &starSystem->starSystem)
		{
			return nullptr;
		}

		if (searchedId & 0x80000000) // check if solar
		{
			auto iter = cacheSolarIObjs.find(searchedId);
			if (iter == cacheSolarIObjs.end())
			{
				return FindSolar(starSystem, searchedId);
			}

			if (iter->second.cacheStarSystem != &starSystem->starSystem)
			{
				lastFoundItem = searchedId;
				lastFoundInSystem = iter->second.cacheStarSystem;
				return nullptr;
			}

			MetaListNode* node;
			switch (iter->second.objClass)
			{
			case CObject::Class::CSOLAR_OBJECT:
				node = FindIObjOnListFunc(starSystem->starSystem.solarList, searchedId);
				if (node)
				{
					retVal = node->value;
				}
				break;
			case CObject::Class::CASTEROID_OBJECT:
				node = FindIObjOnListFunc(starSystem->starSystem.asteroidList, searchedId);
				if (node)
				{
					retVal = node->value;
				}
				break;
			}
			
			cacheSolarIObjs.erase(searchedId);
		}
		else
		{
			if (!playerShips.count(searchedId)) // player can swap systems, for them search just the system's shiplist
			{
				auto iter = cacheNonsolarIObjs.find(searchedId);
				if (iter == cacheNonsolarIObjs.end())
				{
					return FindNonSolar(starSystem, searchedId);
				}

				if (iter->second.cacheStarSystem != &starSystem->starSystem)
				{
					lastFoundItem = searchedId;
					lastFoundInSystem = iter->second.cacheStarSystem;
					return nullptr;
				}

				MetaListNode* node;
				switch (iter->second.objClass)
				{
				case CObject::Class::CSHIP_OBJECT:
					node = FindIObjOnListFunc(starSystem->starSystem.shipList, searchedId);
					if (node)
					{
						retVal = node->value;
					}
					break;
				case CObject::Class::CLOOT_OBJECT:
					node = FindIObjOnListFunc(starSystem->starSystem.lootList, searchedId);
					if (node)
					{
						retVal = node->value;
					}
					break;
				case CObject::Class::CGUIDED_OBJECT:
					node = FindIObjOnListFunc(starSystem->starSystem.guidedList, searchedId);
					if (node)
					{
						retVal = node->value;
					}
					break;
				case CObject::Class::CMINE_OBJECT:
					node = FindIObjOnListFunc(starSystem->starSystem.mineList, searchedId);
					if (node)
					{
						retVal = node->value;
					}
					break;
				case CObject::Class::CCOUNTERMEASURE_OBJECT:
					node = FindIObjOnListFunc(starSystem->starSystem.counterMeasureList, searchedId);
					if (node)
					{
						retVal = node->value;
					}
					break;
				}

				cacheNonsolarIObjs.erase(searchedId);
			}
			else
			{
				MetaListNode* node = FindIObjOnListFunc(starSystem->starSystem.shipList, searchedId);
				if (node)
				{
					retVal = node->value;
				}
			}
		}

		return retVal;
	}

	__declspec(naked) void FindInStarListNaked()
	{
		__asm
		{
			push ecx
			push[esp + 0x8]
			sub ecx, 4
			push ecx
			call FindInStarList
			pop ecx
			ret 0x4
		}
	}

	__declspec(naked) void FindInStarListNaked2()
	{
		__asm
		{
			mov eax, [esp+0x4]
			mov edx, [eax]
 			mov [esp+0x4], edx
			push ecx
			push[esp + 0x8]
			sub ecx, 4
			push ecx
			call FindInStarList
			pop ecx
			ret 0x4
		}
	}

	void __stdcall GameObjectDestructor(uint id)
	{
		if (id & 0x8000000)
		{
			cacheSolarIObjs.erase(id);
		}
		else
		{
			cacheNonsolarIObjs.erase(id);
		}
	}

	uint GameObjectDestructorRet = 0x6CEE4A7;
	__declspec(naked) void GameObjectDestructorNaked()
	{
		__asm {
			push ecx
			mov ecx, [ecx+0x4]
			mov ecx, [ecx+0xB0]
			push ecx
			call GameObjectDestructor
			pop ecx
			push 0xFFFFFFFF
			push 0x6d60776
			jmp GameObjectDestructorRet
		}
	}

	int __fastcall VectorOptimize(UnkOptimize* obj)
	{
		obj->vec3.x += obj->vec1.x;
		obj->vec3.y += obj->vec1.y;
		obj->vec3.z += obj->vec1.z;

		obj->vec4.x += obj->vec2.x;
		obj->vec4.y += obj->vec2.y;
		obj->vec4.z += obj->vec2.z;

		obj->vec1.x = 0;
		obj->vec1.y = 0;
		obj->vec1.z = 0;

		obj->vec2.x = 0;
		obj->vec2.y = 0;
		obj->vec2.z = 0;

		return 0;
	}

	unordered_map<uint, CObject*> CSolarMap;
	unordered_map<uint, CObject*> CLootMap;
	unordered_map<uint, CObject*> CShipMap;
	unordered_map<uint, CObject*> CAsteroidMap;
	unordered_map<uint, CObject*> CProjectileMap;

	void __fastcall CSimpleInit(CSimple* csimple, void* edx, CSimple::CreateParms& param)
	{
		switch (csimple->objectClass)
		{
		case CObject::CSOLAR_OBJECT:
			CSolarMap[param.id] = csimple;
			break;
		case CObject::CSHIP_OBJECT:
			CShipMap[param.id] = csimple;
			break;
		case CObject::CLOOT_OBJECT:
			CLootMap[param.id] = csimple;
			break;
		case CObject::CASTEROID_OBJECT:
			CAsteroidMap[param.id] = csimple;
			break;
		default:
			if ((csimple->objectClass & CObject::CPROJECTILE_MASK) == CObject::CPROJECTILE_MASK)
			{
				CProjectileMap[param.id] = csimple;
				break;
			}
		}
	}

	uint CSimpleInitRetAddr = 0x62B5B66;
	__declspec(naked) void CSimpleInitNaked()
	{
		__asm {
			push ecx
			push [esp+0x8]
			call CSimpleInit
			pop ecx
			push ebx
			push ebp
			mov ebp, [esp+0xC]
			jmp CSimpleInitRetAddr
		}
	}


	void __fastcall CSimpleDestr(CSimple* csimple)
	{
		switch (csimple->objectClass)
		{
		case CObject::CSOLAR_OBJECT:
			CSolarMap.erase(csimple->id);
			break;
		case CObject::CSHIP_OBJECT:
			CShipMap.erase(csimple->id);
			break;
		case CObject::CLOOT_OBJECT:
			CLootMap.erase(csimple->id);
			break;
		case CObject::CASTEROID_OBJECT:
			CAsteroidMap.erase(csimple->id);
			break;
		default:
			if ((csimple->objectClass & CObject::CPROJECTILE_MASK) == CObject::CPROJECTILE_MASK)
			{
				CProjectileMap.erase(csimple->id);
				break;
			}
		}
	}

	uint CSimpleDestrRetAddr = 0x62B5987;
	__declspec(naked) void CSimpleDestrOrgNaked()
	{
		__asm {
			push ecx
			call CSimpleDestr
			pop ecx
			push 0xFFFFFFFF
			push 0x0639473F
			jmp CSimpleDestrRetAddr
		}
	}

	CObject* __cdecl CObjectFindDetour(const uint& spaceObjId, CObject::Class objClass)
	{
		unordered_map<uint, CObject*> cobjMap;
		switch (objClass) {
		case CObject::CSOLAR_OBJECT:
			cobjMap = CSolarMap;
			break;
		case CObject::CSHIP_OBJECT:
			cobjMap = CShipMap;
			break;
		case CObject::CLOOT_OBJECT:
			cobjMap = CLootMap;
			break;
		case CObject::CASTEROID_OBJECT:
			cobjMap = CAsteroidMap;
			break;
		default:
			if ((objClass & CObject::CPROJECTILE_MASK) == CObject::CPROJECTILE_MASK)
			{
				cobjMap = CProjectileMap;
				break;
			}
		}

		auto result = cobjMap.find(spaceObjId);
		if (result != cobjMap.end())
		{
			++result->second->referenceCounter;
			return result->second;
		}
		else
		{
			return CObject::Find(spaceObjId, objClass);
		}
	}

	/**************************************************************************************************************
	// flserver memory leak bugfix
	**************************************************************************************************************/

	int __cdecl FreeReputationVibe(int const &p1)
	{
		__asm
		{
			mov eax, p1
			push eax
			mov eax, [hModServer]
			add eax, 0x65C20
			call eax
			add esp, 4
		}

		return Reputation::Vibe::Free(p1);

	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	void __cdecl Update_Time(double dInterval)
	{
		
		//CALL_PLUGINS_V(PLUGIN_HkCb_Update_Time, , (double), (dInterval));

		Timing::UpdateGlobalTime(dInterval);

		//CALL_PLUGINS_V(PLUGIN_HkCb_Update_Time_AFTER, , (double), (dInterval));
	}

	/**************************************************************************************************************
	**************************************************************************************************************/


	uint iLastTicks = 0;

	void __stdcall Elapse_Time(float p1)
	{

		//CALL_PLUGINS_V(PLUGIN_HkCb_Elapse_Time, __stdcall, (float), (p1));

		Server.ElapseTime(p1);

		//CALL_PLUGINS_V(PLUGIN_HkCb_Elapse_Time_AFTER, __stdcall, (float), (p1));

		// low serverload missile jitter bugfix
		uint iCurLoad = GetTickCount() - iLastTicks;
		if (iCurLoad < 5) {
			uint iFakeLoad = 5 - iCurLoad;
			Sleep(iFakeLoad);
		}
		iLastTicks = GetTickCount();

	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	int __cdecl Dock_Call(unsigned int const &uShipID, unsigned int const &uSpaceID, int iDockPort, enum DOCK_HOST_RESPONSE dockResponse)
	{

		//	iDockPort == -1, dockResponse -> 2 --> Dock Denied!
		//	iDockPort == -1, dockResponse -> 3 --> Dock in Use
		//	iDockPort != -1, dockResponse -> 4 --> Dock ok, proceed
		//	iDockPort == -1, dockResponse -> 5 --> now DOCK!

		DOCK_HOST_RESPONSE prePluginResponse = dockResponse;
		int prePluginDockPort = iDockPort;

		int returnValue;

		CALL_PLUGINS(PLUGIN_HkCb_Dock_Call, int, , (unsigned int const &, unsigned int const &, int&, DOCK_HOST_RESPONSE&), (uShipID, uSpaceID, iDockPort, dockResponse));

		LOG_CORE_TIMER_START
		TRY_HOOK {
			returnValue = pub::SpaceObj::Dock(uShipID, uSpaceID, iDockPort, dockResponse);
		} CATCH_HOOK({})
		LOG_CORE_TIMER_END

		CALL_PLUGINS(PLUGIN_HkCb_Dock_Call_AFTER, int, , (unsigned int const &, unsigned int const &, int&, DOCK_HOST_RESPONSE&), (uShipID, uSpaceID, iDockPort, dockResponse));

		//if original response was positive and new response is negative, set the dock event for immediate cancellation
		//also ACCESS_DENIED response doesn't automatically trigger the appropriate voice line
		if (prePluginDockPort != -1 && iDockPort == -1 &&
			(uint)prePluginResponse >= 3 && (uint)dockResponse < 3)
		{
			bAbortEventRequest = true;
			if (dockResponse == ACCESS_DENIED)
			{
				uint client = HkGetClientIDByShip(uShipID);
				pub::Player::SendNNMessage(client, pub::GetNicknameId("info_access_denied"));
			}
		}

		return returnValue;
	}


	/**************************************************************************************************************
	**************************************************************************************************************/


	FARPROC fpOldLaunchPos;

	bool __stdcall LaunchPos(uint iSpaceID, struct CEqObj &p1, Vector &p2, Matrix &p3, int iDock)
	{

		CALL_PLUGINS(PLUGIN_LaunchPosHook, bool, __stdcall, (uint, CEqObj &, Vector &, Matrix &, int), (iSpaceID, p1, p2, p3, iDock));

		return p1.launch_pos(p2, p3, iDock);

	}


	__declspec(naked) void _LaunchPos()
	{
		__asm
		{
			push ecx //4
			push[esp + 8 + 8] //8
			push[esp + 12 + 4] //12
			push[esp + 16 + 0] //16
			push ecx
			push[ecx + 176]
			call LaunchPos
			pop ecx
			ret 0x0C
		}

	}

	/**************************************************************************************************************
	**************************************************************************************************************/

	struct LOAD_REP_DATA
	{
		uint iRepID;
		float fAttitude;
	};

	struct REP_DATA_LIST
	{
		uint iDunno;
		LOAD_REP_DATA* begin;
		LOAD_REP_DATA* end;
	};

	bool __stdcall HkLoadRepFromCharFile(REP_DATA_LIST* savedReps, LOAD_REP_DATA* repToSave)
	{
		// check of the rep id is valid
		if (repToSave->iRepID == 0xFFFFFFFF)
			return false; // rep id not valid!

		LOAD_REP_DATA* repIt = savedReps->begin;

		while (repIt != savedReps->end)
		{
			if (repIt->iRepID == repToSave->iRepID)
				return false; // we already saved this rep!

			repIt++;
		}

		// everything seems fine, add
		return true;
	}

	__declspec(naked) void _HkLoadRepFromCharFile()
	{
		__asm
		{
			push ecx // save ecx because thiscall
			push[esp + 4 + 4 + 8] // rep data
			push ecx // rep data list
			call HkLoadRepFromCharFile
			pop ecx // recover ecx
			test al, al
			jz abort_lbl
			jmp[fpOldLoadRepCharFile]
			abort_lbl:
			ret 0x0C
		}

	}


	/**************************************************************************************************************
	**************************************************************************************************************/


}