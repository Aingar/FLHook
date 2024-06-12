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
	void __fastcall CheckRange(uint player)
	{
		float radarRange = ClientInfo[player].fRadarRange;
		*pNPC_range = *pPlayer_range = radarRange;
		*pGroup_range = radarRange * 4;
	}

	__declspec(naked) void Radar_Range_naked()
	{
		__asm {
			mov        ecx, [edi + 0x38]
			call        CheckRange
			mov			eax, 0
			ret
		}
	}

	unordered_set<uint> playerShips;
	unordered_map<uint, IObjRW*> epicSolarMap;
	unordered_map<uint, IObjRW*> epicNonSolarMap;

	FARPROC fpOldShipInitialized;

	static IObjRW* lastIObj;

	void __stdcall CShipInitialized(IObjRW* iobj)
	{
		lastIObj = iobj;
	}

	__declspec(naked) void CShipInitializedNaked()
	{
		__asm
		{
			push ecx
			push ecx
			call CShipInitialized
			pop ecx
			jmp fpOldShipInitialized
		}
	}

	void __stdcall cshipInit(uint* shipId)
	{
		epicNonSolarMap[*shipId] = lastIObj;
	}
	FARPROC fpOldCshipInit;
	__declspec(naked) void cshipInitNaked()
	{
		__asm
		{
			push ecx
			push [esp+0x8]
			call cshipInit
			pop ecx
			jmp fpOldCshipInit
		}
	}

	void __stdcall csolarInit(uint* solarId)
	{
		epicSolarMap[*solarId] = lastIObj;
	}

	FARPROC fpOldCsolarInit;
	__declspec(naked) void csolarInitNaked()
	{
		__asm
		{
			push ecx
			push[esp + 0x8]
			call csolarInit
			pop ecx
			jmp fpOldCsolarInit
		}
	}

	FARPROC fpOldSolarInitialized;

	void __stdcall CSolarInitialized(IObjRW* iobj)
	{
		lastIObj = iobj;
	}

	__declspec(naked) void CSolarInitializedNaked()
	{
		__asm
		{
			push ecx
			push ecx
			call CSolarInitialized
			pop ecx
			jmp fpOldSolarInitialized
		}
	}

	FARPROC FindStarListRet = FARPROC(0x6D0C846);

	static uint mapCounter = 0;

	PBYTE fpOldStarSystemFind;

	typedef MetaListNode* (__thiscall* FindIObjOnList)(MetaList&, uint searchedId);
	FindIObjOnList FindIObjOnListFunc = FindIObjOnList(0x6CF4F00);

	typedef IObjRW* (__thiscall* FindIObjInSystem)(StarSystem& starSystem, uint searchedId);
	FindIObjInSystem FindIObjFunc = FindIObjInSystem(0x6D0C840);

	IObjRW* __stdcall FindInStarList(StarSystem* starSystem, uint searchedId)
	{
		IObjRW* retVal = nullptr;
		
		if (searchedId == 0)
		{
			return nullptr;
		}

		if (searchedId & 0x80000000) // check if solar
		{
			//auto iter = epicSolarMap.find(searchedId);
			//if (iter == epicSolarMap.end())
			//{
				MetaListNode* node = FindIObjOnListFunc(starSystem->solarList, searchedId);
				if (node)
				{
					//epicSolarMap[searchedId] = node->value;
					return node->value;
				}
				return nullptr;
			//}
			//else
			//{
			//	retVal = iter->second;
			//}
			
			//if (retVal)
			//{
			//	if (!retVal->cobj)
			//	{
			//		epicSolarMap.erase(searchedId);
			//		return nullptr;
			//	}
			//	if (retVal->cobj->system != starSystem->systemId)
			//	{
			//		return nullptr;
			//	}
			//	return retVal;
			//}
			//else
			//{
			//	return nullptr;
			//}
		}
		else
		{
			//if (!playerShips.count(searchedId)) // player can swap systems, for them search just the system's shiplist
			//{
				//auto iter = epicNonSolarMap.find(searchedId);
				//if (iter == epicNonSolarMap.end())
				//{
					MetaListNode* node = FindIObjOnListFunc(starSystem->shipList, searchedId);
					if (node)
					{
						retVal = node->value;
						epicNonSolarMap[searchedId] = retVal;
						return retVal;
					}
					node = FindIObjOnListFunc(starSystem->lootList, searchedId);
					if (node)
					{
						retVal = node->value;
						epicNonSolarMap[searchedId] = retVal;
						return retVal;
					}
					node = FindIObjOnListFunc(starSystem->guidedList, searchedId);
					if (node)
					{
						retVal = node->value;
						epicNonSolarMap[searchedId] = retVal;
						return retVal;
					}
					node = FindIObjOnListFunc(starSystem->mineList, searchedId);
					if (node)
					{
						retVal = node->value;
						epicNonSolarMap[searchedId] = retVal;
						return retVal;
					}
					return nullptr;
				//}
				//else if (!iter->second->cobj)
				//{
				//	epicNonSolarMap.erase(searchedId);
				//	return nullptr;
				//}
				//else if (iter->second->cobj->system == starSystem->systemId)
				//{
				//	return iter->second;
				//}
				//else
				//{
				//	return nullptr;
				//}
			//}
			//else
			//{
			//	MetaListNode* node = FindIObjOnListFunc(starSystem->shipList, searchedId);
			//	if (node)
			//	{
			//		return node->value;
			//	}
			//	return nullptr;
			//}
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