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

	bool __fastcall RadarDetection(Observer* observer, void* edx, IObjRW* scannedIObj)
	{
		if (!scannedIObj)
		{
			return true;
		}

		CSimple* scannedCObj = reinterpret_cast<CSimple*>(scannedIObj->cobj);
		if (!scannedCObj || ((scannedCObj->objectClass & CObject::CSIMPLE_MASK) != CObject::CSIMPLE_MASK))
		{
			return false;
		}

		if (observer->clientId == scannedCObj->ownerPlayer)
		{
			return true;
		}

		Vector distanceVector = scannedCObj->vPos;

		distanceVector.x -= observer->position.x;
		distanceVector.y -= observer->position.y;
		distanceVector.z -= observer->position.z;

		float distanceSquared = distanceVector.x * distanceVector.x + distanceVector.y * distanceVector.y + distanceVector.z * distanceVector.z;

		uint scannedClientID = scannedCObj->ownerPlayer;
		float radarRange = ClientInfo[observer->clientId].fRadarRange;
		float interference = scannedCObj->currentDamageZone ? scannedCObj->currentDamageZone->scannerInterference : 0.0f;
		CShip* observerShip = ClientInfo[observer->clientId].cship;
		if (observerShip && observerShip->currentDamageZone)
		{
			interference = max(interference, observerShip->currentDamageZone->scannerInterference);
		}
		if (interference)
		{
			interference = 1.0f - interference;
			radarRange *= interference * interference;
		}

		if (scannedClientID)
		{
			auto scannedClientGroup = Players[scannedClientID].PlayerGroup;
			if (scannedClientGroup && scannedClientGroup == Players[observer->clientId].PlayerGroup)
			{
				return distanceSquared < radarRange * 4;
			}
		}

		return distanceSquared < radarRange;
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

	void __stdcall GameObjectDestructor(uint id)
	{
		if (id & 0x80000000)
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

	struct CObjNode
	{
		CObjNode* next;
		CObjNode* prev;
		CObject* cobj;
	};

	struct CObjEntryNode
	{
		CObjNode* first;
		CObjNode* last;
	};

	struct CObjList
	{
		uint dunno;
		CObjEntryNode* entry;
		uint size;
	};

	unordered_map<CObject*, CObjNode*> CMineMap;
	unordered_map<CObject*, CObjNode*> CCmMap;
	unordered_map<CObject*, CObjNode*> CBeamMap;
	unordered_map<CObject*, CObjNode*> CGuidedMap;
	unordered_map<CObject*, CObjNode*> CSolarMap;
	unordered_map<CObject*, CObjNode*> CShipMap;
	unordered_map<CObject*, CObjNode*> CAsteroidMap;
	unordered_map<CObject*, CObjNode*> CLootMap;
	unordered_map<CObject*, CObjNode*> CEquipmentMap;
	unordered_map<CObject*, CObjNode*> CObjectMap;

	unordered_map<uint, CSimple*> CAsteroidMap2;

	typedef CObjList* (__cdecl* CObjListFunc)(CObject::Class);
	CObjListFunc CObjListFind = CObjListFunc(0x62AE690);

	typedef void(__thiscall* RemoveCobjFromVector)(CObjList*, void*, CObjNode*);
	RemoveCobjFromVector removeCObjNode = RemoveCobjFromVector(0x62AF830);

	uint CObjAllocJmp = 0x62AEE55;
	__declspec(naked) CSimple* __cdecl CObjAllocCallOrig(CObject::Class objClass)
	{
		__asm {
			push ecx
			mov eax, [esp + 8]
			jmp CObjAllocJmp
		}
	}

	CObject* __cdecl CObjAllocDetour(CObject::Class objClass)
	{
		CSimple* retVal = CObjAllocCallOrig(objClass);
		CObjList* cobjList = CObjListFind(objClass);

		switch (objClass)
		{
		case CObject::CASTEROID_OBJECT:
			CAsteroidMap[retVal] = cobjList->entry->last;
			break;
		case CObject::CEQUIPMENT_OBJECT:
			CEquipmentMap[retVal] = cobjList->entry->last;
			break;
		case CObject::COBJECT_MASK:
			CObjectMap[retVal] = cobjList->entry->last;
			break;
		case CObject::CSOLAR_OBJECT:
			CSolarMap[retVal] = cobjList->entry->last;
			break;
		case CObject::CSHIP_OBJECT:
			CShipMap[retVal] = cobjList->entry->last;
			break;
		case CObject::CLOOT_OBJECT:
			CLootMap[retVal] = cobjList->entry->last;
			break;
		case CObject::CBEAM_OBJECT:
			CBeamMap[retVal] = cobjList->entry->last;
			break;
		case CObject::CGUIDED_OBJECT:
			CGuidedMap[retVal] = cobjList->entry->last;
			break;
		case CObject::CCOUNTERMEASURE_OBJECT:
			CCmMap[retVal] = cobjList->entry->last;
			break;
		case CObject::CMINE_OBJECT:
			CMineMap[retVal] = cobjList->entry->last;
			break;
		}

		return retVal;
	}

	void __fastcall CAsteroidInit(CSimple* csimple, void* edx, CSimple::CreateParms& param)
	{
		CAsteroidMap2[param.id] = csimple;
	}

	constexpr uint CAsteroidInitRetAddr = 0x62A28F6;
	__declspec(naked) void CAsteroidInitNaked()
	{
		__asm {
			push ecx
			push [esp+0x8]
			call CAsteroidInit
			pop ecx
			push esi
			push edi
			mov edi, [esp+0xC]
			jmp CAsteroidInitRetAddr
		}
	}

	void __fastcall CGuidedInit(CGuided* cguided, void* edx, CGuided::CreateParms& param)
	{
		CALL_PLUGINS_NORET(PLUGIN_HkIEngine_CGuided_init, , (CGuided*, CGuided::CreateParms&), (cguided, param));
	}

	constexpr uint CGuidedInitRetAddr = 0x62ACCB6;
	__declspec(naked) void CGuidedInitNaked()
	{
		__asm {
			push ecx
			push [esp+0x8]
			call CGuidedInit
			pop ecx
			push esi
			push edi
			mov edi, [esp+0xC]
			jmp CGuidedInitRetAddr
		}
	}

	void __fastcall CObjDestr(CObject* cobj)
	{
		unordered_map<CObject*, CObjNode*>* cobjMap;
		switch (cobj->objectClass) {
		case CObject::CASTEROID_OBJECT:
			CAsteroidMap2.erase(reinterpret_cast<CSimple*>(cobj)->id);
			cobjMap = &CAsteroidMap;
			break;
		case CObject::CEQUIPMENT_OBJECT:
			cobjMap = &CEquipmentMap;
			break;
		case CObject::COBJECT_MASK:
			cobjMap = &CObjectMap;
			break;
		case CObject::CSOLAR_OBJECT:
			cobjMap = &CSolarMap;
			break;
		case CObject::CSHIP_OBJECT:
			cobjMap = &CShipMap;
			break;
		case CObject::CLOOT_OBJECT:
			cobjMap = &CLootMap;
			break;
		case CObject::CBEAM_OBJECT:
			cobjMap = &CBeamMap;
			break;
		case CObject::CGUIDED_OBJECT:
			cobjMap = &CGuidedMap;
			break;
		case CObject::CCOUNTERMEASURE_OBJECT:
			cobjMap = &CCmMap;
			break;
		case CObject::CMINE_OBJECT:
			cobjMap = &CMineMap;
			break;
		}

		auto item = cobjMap->find(cobj);
		if (item != cobjMap->end())
		{
			CObjList* cobjList = CObjListFind(cobj->objectClass);
			cobjMap->erase(item);
			static uint dummy;
			removeCObjNode(cobjList, &dummy, item->second);
		}
	}

	uint CObjDestrRetAddr = 0x62AF447;
	__declspec(naked) void CObjDestrOrgNaked()
	{
		__asm {
			push ecx
			call CObjDestr
			pop ecx
			push 0xFFFFFFFF
			push 0x06394364
			jmp CObjDestrRetAddr
		}
	}

	CObject* __cdecl CObjectFindDetour(const uint& spaceObjId, CObject::Class objClass)
	{
		if(objClass != CObject::CASTEROID_OBJECT)
		{
			return CObject::Find(spaceObjId, objClass);
		}
		
		auto result = CAsteroidMap2.find(spaceObjId);
		if (result != CAsteroidMap2.end())
		{
			++result->second->referenceCounter;
			return result->second;
		}
		return nullptr;
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

	int __cdecl Dock_Call(unsigned int const &uShipID, unsigned int const &uSpaceID, int iDockPort, DOCK_HOST_RESPONSE dockResponse)
	{

		//	iDockPort == -1, dockResponse -> 2 --> Dock Denied!
		//	iDockPort == -1, dockResponse -> 3 --> Dock in Use
		//	iDockPort != -1, dockResponse -> 4 --> Dock ok, proceed
		//	iDockPort == -1, dockResponse -> 5 --> now DOCK!

		DOCK_HOST_RESPONSE prePluginResponse = dockResponse;
		int prePluginDockPort = iDockPort;

		int returnValue;

		CALL_PLUGINS(PLUGIN_HkCb_Dock_Call, int, __cdecl, (unsigned int const &, unsigned int const &, int&, DOCK_HOST_RESPONSE&), (uShipID, uSpaceID, iDockPort, dockResponse));

		LOG_CORE_TIMER_START
		TRY_HOOK {
			returnValue = pub::SpaceObj::Dock(uShipID, uSpaceID, iDockPort, dockResponse);
		} CATCH_HOOK({})
		LOG_CORE_TIMER_END

		CALL_PLUGINS(PLUGIN_HkCb_Dock_Call_AFTER, int, __cdecl, (unsigned int const &, unsigned int const &, int&, DOCK_HOST_RESPONSE&), (uShipID, uSpaceID, iDockPort, dockResponse));

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

	void __fastcall FixPlanetSpin(CEGun* gun, void* edx, CSimple*& target, ushort& subobjid)
	{
		gun->GetTarget(target, subobjid);
		if (target && target->id & 0x80000000)
		{
			target = nullptr;
			subobjid = 0;
		}
	}

	/**************************************************************************************************************
	**************************************************************************************************************/


}