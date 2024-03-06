#include "hook.h"

/**************************************************************************************************************
**************************************************************************************************************/

wstring SetSizeToSmall(const wstring &wscDataFormat)
{
	uint iFormat = wcstoul(wscDataFormat.c_str() + 2, 0, 16);
	wchar_t wszStyleSmall[32];
	wcscpy(wszStyleSmall, wscDataFormat.c_str());
	swprintf(wszStyleSmall + wcslen(wszStyleSmall) - 2, L"%02X", 0x90 | (iFormat & 7));
	return wszStyleSmall;
}

/**************************************************************************************************************
Send "Death: ..." chat-message
**************************************************************************************************************/

void SendDeathMsg(const wstring &wscMsg, uint iSystemID, uint iClientIDVictim, uint iClientIDKiller)
{
	ClientInfo[iClientIDVictim].cship = nullptr;

	CALL_PLUGINS_V(PLUGIN_SendDeathMsg, , (const wstring&, uint&, uint&, uint&), (wscMsg, iSystemID, iClientIDVictim, iClientIDKiller));

	// encode xml string(default and small)
	// non-sys
	wstring wscXMLMsg = L"<TRA data=\"" + set_wscDeathMsgStyle + L"\" mask=\"-1\"/> <TEXT>";
	wscXMLMsg += XMLText(wscMsg);
	wscXMLMsg += L"</TEXT>";

	char szBuf[0xFFFF];
	uint iRet;
	if (!HKHKSUCCESS(HkFMsgEncodeXML(wscXMLMsg, szBuf, sizeof(szBuf), iRet)))
		return;

	wstring wscStyleSmall = SetSizeToSmall(set_wscDeathMsgStyle);
	wstring wscXMLMsgSmall = wstring(L"<TRA data=\"") + wscStyleSmall + L"\" mask=\"-1\"/> <TEXT>";
	wscXMLMsgSmall += XMLText(wscMsg);
	wscXMLMsgSmall += L"</TEXT>";
	char szBufSmall[0xFFFF];
	uint iRetSmall;
	if (!HKHKSUCCESS(HkFMsgEncodeXML(wscXMLMsgSmall, szBufSmall, sizeof(szBufSmall), iRetSmall)))
		return;

	// sys
	wstring wscXMLMsgSys = L"<TRA data=\"" + set_wscDeathMsgStyleSys + L"\" mask=\"-1\"/> <TEXT>";
	wscXMLMsgSys += XMLText(wscMsg);
	wscXMLMsgSys += L"</TEXT>";
	char szBufSys[0xFFFF];
	uint iRetSys;
	if (!HKHKSUCCESS(HkFMsgEncodeXML(wscXMLMsgSys, szBufSys, sizeof(szBufSys), iRetSys)))
		return;

	wstring wscStyleSmallSys = SetSizeToSmall(set_wscDeathMsgStyleSys);
	wstring wscXMLMsgSmallSys = L"<TRA data=\"" + wscStyleSmallSys + L"\" mask=\"-1\"/> <TEXT>";
	wscXMLMsgSmallSys += XMLText(wscMsg);
	wscXMLMsgSmallSys += L"</TEXT>";
	char szBufSmallSys[0xFFFF];
	uint iRetSmallSys;
	if (!HKHKSUCCESS(HkFMsgEncodeXML(wscXMLMsgSmallSys, szBufSmallSys, sizeof(szBufSmallSys), iRetSmallSys)))
		return;

	// send
	// for all players
	struct PlayerData *pPD = 0;
	while (pPD = Players.traverse_active(pPD))
	{
		uint iClientID = HkGetClientIdFromPD(pPD);
		uint iClientSystemID = 0;
		pub::Player::GetSystem(iClientID, iClientSystemID);

		char *szXMLBuf;
		int iXMLBufRet;
		char *szXMLBufSys;
		int iXMLBufRetSys;
		if (set_bUserCmdSetDieMsgSize && (ClientInfo[iClientID].dieMsgSize == CS_SMALL)) {
			szXMLBuf = szBufSmall;
			iXMLBufRet = iRetSmall;
			szXMLBufSys = szBufSmallSys;
			iXMLBufRetSys = iRetSmallSys;
		}
		else {
			szXMLBuf = szBuf;
			iXMLBufRet = iRet;
			szXMLBufSys = szBufSys;
			iXMLBufRetSys = iRetSys;
		}

		if (!set_bUserCmdSetDieMsg)
		{ // /set diemsg disabled, thus send to all
			if (iSystemID == iClientSystemID)
				HkFMsgSendChat(iClientID, szXMLBufSys, iXMLBufRetSys);
			else
				HkFMsgSendChat(iClientID, szXMLBuf, iXMLBufRet);
			continue;
		}

		if (ClientInfo[iClientID].dieMsg == DIEMSG_NONE)
			continue;
		else if ((ClientInfo[iClientID].dieMsg == DIEMSG_SYSTEM) && (iSystemID == iClientSystemID))
			HkFMsgSendChat(iClientID, szXMLBufSys, iXMLBufRetSys);
		else if ((ClientInfo[iClientID].dieMsg == DIEMSG_SELF) && ((iClientID == iClientIDVictim) || (iClientID == iClientIDKiller)))
			HkFMsgSendChat(iClientID, szXMLBufSys, iXMLBufRetSys);
		else if (ClientInfo[iClientID].dieMsg == DIEMSG_ALL) {
			if (iSystemID == iClientSystemID)
				HkFMsgSendChat(iClientID, szXMLBufSys, iXMLBufRetSys);
			else
				HkFMsgSendChat(iClientID, szXMLBuf, iXMLBufRet);
		}
	}
}


/**************************************************************************************************************
Called when ship was destroyed
**************************************************************************************************************/

void __stdcall ShipDestroyed(IObjRW* iobj, bool isKill, uint killerId)
{

	LOG_CORE_TIMER_START
	TRY_HOOK {
		if (isKill)
		{
			CALL_PLUGINS_V(PLUGIN_ShipDestroyed, __stdcall, (IObjRW * iobj, bool isKill, uint killerId), (iobj, isKill, killerId));

			CShip *cship = (CShip*)iobj->cobj;
			uint iClientID = cship->ownerPlayer;

			if (iClientID) { // a player was killed

				wstring wscEvent;
				wscEvent.reserve(256);
				wscEvent = L"kill";

				uint iSystemID;
				pub::Player::GetSystem(iClientID, iSystemID);
				wchar_t wszSystem[64];
				swprintf(wszSystem, L"%u", iSystemID);

				DamageCause iCause = ClientInfo[iClientID].dmgLastCause;
				uint iClientIDKiller = HkGetClientIDByShip(killerId);

				wstring wscVictim = (wchar_t*)Players.GetActiveCharacterName(iClientID);
				wscEvent += L" victim=" + wscVictim;
				if (iClientIDKiller) {
					wstring wscType = L"";
					if (iCause == DamageCause::MissileTorpedo)
						wscType = L"Missile/Torpedo";
					else if (iCause == DamageCause::Mine)
						wscType = L"Mine";
					else if ((iCause == DamageCause::CruiseDisrupter) || (iCause == DamageCause::UnkDisrupter) || (iCause == DamageCause::DummyDisrupter))
						wscType = L"Wasp/Hornet";
					else if (iCause == DamageCause::Collision)
						wscType = L"Collision";
					else if (iCause == DamageCause::Gun)
						wscType = L"Gun";
					else {
						wscType = L"Gun"; //0x02
	//					AddLog("get_cause() returned %X", iCause);
					}

					wstring wscMsg;
					if (iClientID == iClientIDKiller) {
						wscEvent += L" type=selfkill";
						wscMsg = ReplaceStr(set_wscDeathMsgTextSelfKill, L"%victim", wscVictim);
					}
					else {
						wscEvent += L" type=player";
						wstring wscKiller = (wchar_t*)Players.GetActiveCharacterName(iClientIDKiller);
						wscEvent += L" by=" + wscKiller;

						wscMsg = ReplaceStr(set_wscDeathMsgTextPlayerKill, L"%victim", wscVictim);
						wscMsg = ReplaceStr(wscMsg, L"%killer", wscKiller);
					}

					wscMsg = ReplaceStr(wscMsg, L"%type", wscType);
					if (set_bDieMsg && wscMsg.length())
						SendDeathMsg(wscMsg, iSystemID, iClientID, iClientIDKiller);
					ProcessEvent(L"%s", wscEvent.c_str());

				}
				else if (iCause == DamageCause::Admin) {
					wstring wscMsg = ReplaceStr(set_wscDeathMsgTextAdminKill, L"%victim", wscVictim);

					if (set_bDieMsg && wscMsg.length())
						SendDeathMsg(wscMsg, iSystemID, iClientID, 0);
				}
				else if (!killerId) {
					wscEvent += L" type=suicide";
					wstring wscMsg = ReplaceStr(set_wscDeathMsgTextSuicide, L"%victim", wscVictim);

					if (set_bDieMsg && wscMsg.length())
						SendDeathMsg(wscMsg, iSystemID, iClientID, 0);
					ProcessEvent(L"%s", wscEvent.c_str());
				}
				else 
				{
					wstring wscType = L"";
					if (iCause == DamageCause::MissileTorpedo)
						wscType = L"Missile/Torpedo";
					else if (iCause == DamageCause::Mine)
						wscType = L"Mine";
					else if ((iCause == DamageCause::CruiseDisrupter) || (iCause == DamageCause::DummyDisrupter) || (iCause == DamageCause::UnkDisrupter))
						wscType = L"Wasp/Hornet";
					else if (iCause == DamageCause::Collision)
						wscType = L"Collision";
					else
						wscType = L"Gun"; //0x02

					wscEvent += L" type=npc";
					wstring wscMsg = ReplaceStr(set_wscDeathMsgTextNPC, L"%victim", wscVictim);
					wscMsg = ReplaceStr(wscMsg, L"%type", wscType);

					if (set_bDieMsg && wscMsg.length())
						SendDeathMsg(wscMsg, iSystemID, iClientID, 0);
					ProcessEvent(L"%s", wscEvent.c_str());
				}
			}

			ClientInfo[iClientID].iShipOld = ClientInfo[iClientID].iShip;
			ClientInfo[iClientID].iShip = 0;
		}
	} CATCH_HOOK({})
	LOG_CORE_TIMER_END

}

void __stdcall SolarDestroyed(IObjRW* iobj, bool isKill, uint killerId)
{
	if (isKill)
	{
		CALL_PLUGINS_V(PLUGIN_BaseDestroyed, __stdcall, (IObjRW * iobj, bool isKill, uint killerId), (iobj, isKill, killerId));
	}
}
FARPROC fpOldSolarDestroyed;

__declspec(naked) void SolarDestroyedNaked()
{
	__asm
	{
		push ecx
		push[esp + 0xC]
		push[esp + 0xC]
		push ecx
		call SolarDestroyed
		pop ecx
		mov eax, [fpOldSolarDestroyed]
		jmp eax
	}
}


FARPROC fpOldShipDestroyed;

__declspec(naked) void ShipDestroyedNaked()
{
	__asm
	{
		push ecx
		push[esp + 0xC]
		push[esp + 0xC]
		push ecx
		call ShipDestroyed
		pop ecx
		mov eax, [fpOldShipDestroyed]
		jmp eax
	}
}


inline bool MineDestroyedPluginCaller(IObjRW* iobj, bool isKill, uint killerId)
{
	CALL_PLUGINS(PLUGIN_MineDestroyed, bool, __stdcall, (IObjRW * iobj, bool isKill, uint killerId), (iobj, isKill, killerId));
	return true;
}

bool __stdcall MineDestroyed(IObjRW* iobj, bool isKill, uint killerId)
{
	if (!MineDestroyedPluginCaller(iobj, isKill, killerId) && !isKill)
	{
		pub::SpaceObj::Destroy(((CSimple*)iobj->cobj)->id, DestroyType::FUSE);
		return false;
	}
	return true;
}

FARPROC MineDestroyedOrigFunc;
__declspec(naked) void MineDestroyedNaked()
{
	__asm {
		push ecx
		push[esp + 0xC]
		push[esp + 0xC]
		push ecx
		call MineDestroyed
		pop ecx
		test al, al
		jz skipLabel
		mov eax, [MineDestroyedOrigFunc]
		jmp eax
		skipLabel :
		ret 0x8
	}
}

inline bool GuidedDestroyedPluginCaller(IObjRW* iobj, bool isKill, uint killerId)
{
	static IObjRW* lastIObj = nullptr;
	if (lastIObj == iobj)
	{
		return true;
	}
	lastIObj = iobj;

	CALL_PLUGINS(PLUGIN_GuidedDestroyed, bool, __stdcall, (IObjRW * iobj, bool isKill, uint killerId), (iobj, isKill, killerId));
	return true;
}

bool __stdcall GuidedDestroyed(IObjRW* iobj, bool isKill, uint killerId)
{
	if (!GuidedDestroyedPluginCaller(iobj, isKill, killerId))
	{
		pub::SpaceObj::Destroy(((CSimple*)iobj->cobj)->id, DestroyType::VANISH);
		return false;
	}
	return true;
}

FARPROC GuidedDestroyedOrigFunc;
__declspec(naked) void GuidedDestroyedNaked()
{

	__asm {
		push ecx
		push[esp + 0xC]
		push[esp + 0xC]
		push ecx
		call GuidedDestroyed
		pop ecx
		test al, al
		jz skipLabel
		mov eax, [GuidedDestroyedOrigFunc]
		jmp eax
		skipLabel :
		ret 0x8
	}
}

FARPROC ColGrpDeathOrigFunc;
void __stdcall ShipColGrpDestroyedHook(IObjRW* iobj, CArchGroup* colGrp, DamageEntry::SubObjFate fate, DamageList* dmgList)
{
	CALL_PLUGINS_V(PLUGIN_ShipColGrpDestroyed, , (IObjRW* , CArchGroup* , DamageEntry::SubObjFate fate, DamageList*), (iobj, colGrp, fate, dmgList));
}
__declspec(naked) void ShipColGrpDestroyedHookNaked()
{
	__asm
	{
		push ecx
		push [esp + 0x10]
		push [esp + 0x10]
		push [esp + 0x10]
		push ecx
		call ShipColGrpDestroyedHook
		pop ecx
		mov eax, [ColGrpDeathOrigFunc]
		jmp eax
	}
}

void __stdcall SolarColGrpDestroyedHook(IObjRW* iobj, CArchGroup* colGrp, DamageEntry::SubObjFate fate, DamageList* dmgList)
{
	CALL_PLUGINS_V(PLUGIN_SolarColGrpDestroyed, , (IObjRW*, CArchGroup*, DamageEntry::SubObjFate fate, DamageList*), (iobj, colGrp, fate, dmgList));
}
__declspec(naked) void SolarColGrpDestroyedHookNaked()
{
	__asm
	{
		push ecx
		push[esp + 0x10]
		push[esp + 0x10]
		push[esp + 0x10]
		push ecx
		call SolarColGrpDestroyedHook
		pop ecx
		mov eax, [ColGrpDeathOrigFunc]
		jmp eax
	}
}

/**************************************************************************************************************
Called when base was destroyed
**************************************************************************************************************/

void BaseDestroyed(uint iObject, uint iClientIDBy)
{

	CALL_PLUGINS_V(PLUGIN_BaseDestroyed, , (uint, uint), (iObject, iClientIDBy));

	uint iID;
	pub::SpaceObj::GetDockingTarget(iObject, iID);
	Universe::IBase *base = Universe::get_base(iID);

	char *szBaseName = "";
	if (base)
	{
		__asm
		{
			pushad
			mov ecx, [base]
			mov eax, [base]
			mov eax, [eax]
			call[eax + 4]
			mov[szBaseName], eax
			popad
		}
	}

	ProcessEvent(L"basedestroy basename=%s basehash=%u solarhash=%u by=%s",
		stows(szBaseName).c_str(),
		iObject,
		iID,
		(wchar_t*)Players.GetActiveCharacterName(iClientIDBy));

}
