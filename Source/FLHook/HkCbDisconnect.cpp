#include "hook.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

int __stdcall DisconnectPacketSent(uint iClientID)
{
	LOG_CORE_TIMER_START
	TRY_HOOK {
		uint iShip = 0;
		pub::Player::GetShip(iClientID, iShip);
		if (set_iDisconnectDelay && iShip)
		{ // in space
			ClientInfo[iClientID].tmF1TimeDisconnect = timeInMS() + set_iDisconnectDelay;
			CALL_PLUGINS_NORET(PLUGIN_DelayedDisconnect, , (uint, uint), (iClientID, iShip));
			return 0; // don't pass on
		}
	} CATCH_HOOK({})
	LOG_CORE_TIMER_END
	return 1; // pass on
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

FARPROC fpOldDiscPacketSent;

__declspec(naked) void _DisconnectPacketSent()
{
	__asm
	{
		pushad
		mov eax, [esi + 0x68]
		push eax
		call DisconnectPacketSent
		cmp eax, 0
		jz suppress
		popad
		jmp[fpOldDiscPacketSent]
		suppress:
		popad
			mov eax, [hModDaLib]
			add eax, ADDR_DALIB_DISC_SUPPRESS
			jmp eax
	}
}


typedef bool(__thiscall* HandleSaveFunc)(PlayerData* pd, char* filename, wchar_t* accId, uint dunno);
HandleSaveFunc HandleSaveCall = HandleSaveFunc(0x6D4CCD0);

bool __fastcall HandleSave(PlayerData* pd, void* edx, char* filename, wchar_t* accId, uint dunno)
{
	if (!pd->iBaseID && !pd->iShipID)
	{
		static _GetFLName GetFLName = (_GetFLName)((char*)hModServer + 0x66370);
		char accName[1024];
		GetFLName(accName, pd->accId);

		string charPath = scAcctPath + accName + "\\" + filename;

		bool retVal = HandleSaveCall(pd, filename, accId, dunno);

		char posbuf[100];
		sprintf_s(posbuf, "%f,%f,%f", Players[pd->iOnlineID].vPosition.x, Players[pd->iOnlineID].vPosition.y, Players[pd->iOnlineID].vPosition.z);
		WritePrivateProfileString("Player", "pos", posbuf, charPath.c_str());
		WritePrivateProfileString("Player", "rotate", "0,0,0", charPath.c_str());
		WritePrivateProfileString("Player", "base", nullptr, charPath.c_str());

		return retVal;
	}

	return HandleSaveCall(pd, filename, accId, dunno);
}