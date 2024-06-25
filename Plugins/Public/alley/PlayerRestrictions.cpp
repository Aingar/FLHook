// AlleyPlugin for FLHookPlugin
// December 2014 by Alley
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <list>
#include <map>
#include <algorithm>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include "PlayerRestrictions.h"
#include <sstream>
#include <iostream>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>
#include <boost/lexical_cast.hpp>

static uint maxJettisonCount = 25;
static uint maxJettisonCountNoOwner = 100;
static uint lootCleanupFrequency = 420;

namespace pt = boost::posix_time;
static int set_iPluginDebug = 0;

FILE *GiftLogfile = fopen("./flhook_logs/alley_gifts.log", "at");

void GiftLogging(const char *szString, ...)
{
	char szBufString[1024];
	va_list marker;
	va_start(marker, szString);
	_vsnprintf(szBufString, sizeof(szBufString) - 1, szString, marker);

	if (GiftLogfile) {
		char szBuf[64];
		time_t tNow = time(0);
		struct tm *t = localtime(&tNow);
		strftime(szBuf, sizeof(szBuf), "%d/%m/%Y %H:%M:%S", t);
		fprintf(GiftLogfile, "%s %s\n", szBuf, szBufString);
		fflush(GiftLogfile);
		fclose(GiftLogfile);
	}
	else {
		ConPrint(L"Failed to write gift log! This might be due to inability to create the directory - are you running as an administrator?\n");
	}
	GiftLogfile = fopen("./flhook_logs/alley_gifts.log", "at");
}

/////////////////////////////////////////Penis
/*
void *DetourFunc(BYTE *src, const BYTE *dst, const int len)
{
	BYTE *jmp = (BYTE*)malloc(len+5);
	DWORD dwback;

	VirtualProtect(src, len, PAGE_READWRITE, &dwback);

	memcpy(jmp, src, len);  jmp += len;

	jmp[0] = 0xE9;
	*(DWORD*)(jmp+1) = (DWORD)(src+len - jmp) - 5;

	src[0] = 0xE9;
	*(DWORD*)(src+1) = (DWORD)(dst-src) - 5;

	VirtualProtect(src, len, dwback, &dwback);

	return (jmp-len);
}

typedef pub::AI::OP_RTYPE(__cdecl *pub__AI__SubmitDirective_t)(unsigned int spaceobj, pub::AI::BaseOp* op);
pub__AI__SubmitDirective_t real_pub__AI__SubmitDirective = NULL;

void testOp(uint spaceobj, pub::AI::BaseOp* op, bool directive)
{
	AddLog("AI Op for ship %u (%s): %s", spaceobj, directive ? "directive" : "state", typeid(*op).name());

	pub::AI::DirectiveTrailOp* top = dynamic_cast<pub::AI::DirectiveTrailOp*>(op);
	if (top)
	{
		int x = 0;
	}

	pub::AI::DirectiveGotoOp* gop = dynamic_cast<pub::AI::DirectiveGotoOp*>(op);
	if (gop)
	{
		int x = 0;
	}

	pub::AI::DirectiveFormationOp* fop = dynamic_cast<pub::AI::DirectiveFormationOp*>(op);
	if (fop)
	{
		int x = 0;
		++x;
		++x;
	}

	pub::AI::DirectiveDelayOp* dop = dynamic_cast<pub::AI::DirectiveDelayOp*>(op);
	if (dop)
	{
		int x = 0;
	}

	pub::AI::DirectiveInstantTradelaneOp* itop = dynamic_cast<pub::AI::DirectiveInstantTradelaneOp*>(op);
	if (itop)
	{
		int x = 0;
	}

	pub::AI::DirectiveLaunchOp* lop = dynamic_cast<pub::AI::DirectiveLaunchOp*>(op);
	if (lop)
	{
		int x = 0;
	}
}

pub::AI::OP_RTYPE pub__AI__SubmitDirective(unsigned int spaceobj, pub::AI::BaseOp* op)
{
	testOp(spaceobj, op, true);
	return real_pub__AI__SubmitDirective(spaceobj, op);
}

typedef pub::AI::OP_RTYPE(__cdecl *pub__AI__SubmitState_t)(unsigned int spaceobj, pub::AI::BaseOp* op);
pub__AI__SubmitState_t real_pub__AI__SubmitState = NULL;

pub::AI::OP_RTYPE pub__AI__SubmitState(unsigned int spaceobj, pub::AI::BaseOp* op)
{
	testOp(spaceobj, op, false);
	return real_pub__AI__SubmitState(spaceobj, op);
}

void Init()
{
	static bool loaded = false;
	if (!loaded)
	{
		loaded = true;

		DWORD baseadr = (DWORD)GetProcAddress(GetModuleHandle("server.dll"), "?SubmitDirective@AI@pub@@YA?AW4OP_RTYPE@12@IPAVBaseOp@12@@Z");


		real_pub__AI__SubmitDirective = (pub__AI__SubmitDirective_t)DetourFunc((BYTE*)baseadr, (BYTE*)pub__AI__SubmitDirective, 7);

		baseadr = (DWORD)GetProcAddress(GetModuleHandle("server.dll"), "?SubmitState@AI@pub@@YA?AW4OP_RTYPE@12@IPAVBaseOp@12@@Z");

		real_pub__AI__SubmitState = (pub__AI__SubmitState_t)DetourFunc((BYTE*)baseadr, (BYTE*)pub__AI__SubmitState, 0xA);
	}
}
*/
/////////////////////////////////////////Penis

#define POPUPDIALOG_BUTTONS_LEFT_YES 1
#define POPUPDIALOG_BUTTONS_CENTER_NO 2
#define POPUPDIALOG_BUTTONS_RIGHT_LATER 4
#define POPUPDIALOG_BUTTONS_CENTER_OK 8

static unordered_map<uint, float> notradelist;
static uint PlayerMarkArray[MAX_CLIENT_ID+1];
//Added this due to idiocy
static list<uint> MarkUsageTimer;
static map<uint, bool> reverseTrade;



char szCurDir[MAX_PATH];

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

void LoadSettings();

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	srand((uint)time(0));
	// If we're being loaded from the command line while FLHook is running then
	// set_scCfgFile will not be empty so load the settings as FLHook only
	// calls load settings on FLHook startup and .rehash.
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		if (set_scCfgFile.length() > 0)
			LoadSettings();

		HkLoadStringDLLs();
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{

		HkUnloadStringDLLs();
	}
	return true;
}

/// Hook will call this function after calling a plugin function to see if we the
/// processing to continue
EXPORT PLUGIN_RETURNCODE Get_PluginReturnCode()
{
	return returncode;
}

FILE *PMLogfile = fopen("./flhook_logs/generatedids.log", "at");
//FILE *JSON_playersonline = fopen("./flhook/playersonline.json", "w");

void PMLogging(const char *szString, ...)
{
	char szBufString[1024];
	va_list marker;
	va_start(marker, szString);
	_vsnprintf(szBufString, sizeof(szBufString) - 1, szString, marker);

	if (PMLogfile) {
		char szBuf[64];
		time_t tNow = time(0);
		struct tm *t = localtime(&tNow);
		strftime(szBuf, sizeof(szBuf), "%d/%m/%Y %H:%M:%S", t);
		fprintf(PMLogfile, "%s %s\n", szBuf, szBufString);
		fflush(PMLogfile);
		fclose(PMLogfile);
	}
	else {
		ConPrint(L"Failed to write generatedids log! This might be due to inability to create the directory - are you running as an administrator?\n");
	}
	PMLogfile = fopen("./flhook_logs/generatedids.log", "at");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Loading Settings
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct HEALING_DATA
{
	float healingMultiplier;
	float healingStatic;
	float maxHeal;
};
unordered_map<uint, HEALING_DATA> healingMultipliers;

uint repairMunitionId = CreateID("healing_gun01_ammo");

void LoadSettings()
{
	PMLogging("-------------------- starting server --------------------");
	returncode = DEFAULT_RETURNCODE;

	// The path to the configuration file.
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\alley_restrictions.cfg";

	INI_Reader ini;
	if (ini.open(scPluginCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("notrade"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("tr"))
					{
						notradelist[CreateID(ini.get_value_string(0))] = min(1.0f, max( 0.0f, ini.get_value_float(1)));
					}
				}
			}
			else if (ini.is_header("general"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("MaxJettisonCount"))
					{
						maxJettisonCount = ini.get_value_int(0);
					}
					else if (ini.is_value("MaxJettisonCountNoOwner"))
					{
						maxJettisonCountNoOwner = ini.get_value_int(0);
					}
					else if (ini.is_value("LootCleanupFrequency"))
					{
						lootCleanupFrequency = ini.get_value_int(0);
					}
				}
			}
		}
		ini.close();
	}

	string scHealingCfgFile = string(szCurDir) + "\\flhook_plugins\\healingrates.cfg";
	if (ini.open(scHealingCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("General"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("repair_munition"))
					{
						repairMunitionId = CreateID(ini.get_value_string(0));
					}
				}
			}
			else if (ini.is_header("HealingRate"))
			{
				list<uint> shipclasses;
				float multiplier = 1.0f;
				float addition = 0;
				float max_hp = 1.0f;
				while (ini.read_value())
				{
					if (ini.is_value("target_shipclass"))
					{
						shipclasses.push_back(ini.get_value_int(0));
					}
					else if (ini.is_value("addition"))
					{
						addition = ini.get_value_float(0);
					}
					else if (ini.is_value("multiplier"))
					{
						multiplier = ini.get_value_float(0) * 0.01f;
					}
					else if (ini.is_value("max_heal"))
					{
						max_hp = ini.get_value_float(0);
					}
				}
				foreach(shipclasses, uint, shipclass)
				{
					HEALING_DATA healing;
					healing.healingMultiplier = multiplier;
					healing.healingStatic = addition;
					healing.maxHeal = max_hp;
					healingMultipliers[*shipclass] = healing;
				}
			}
		}
		ini.close();
	}

	AP::LoadSettings();
	ADOCK::LoadSettings();
	SCI::LoadSettings();
	REP::LoadSettings();

	//Init();
}

void SetFuse(uint iClientID, uint fuse, float lifetime)
{
	IObjInspectImpl *obj = HkGetInspect(iClientID);
	if (obj)
	{
		HkLightFuse((IObjRW*)obj, fuse, 0.0f, lifetime, 0.0f);
	}
}

void UnSetFuse(uint iClientID, uint fuse)
{
	IObjInspectImpl *obj = HkGetInspect(iClientID);
	if (obj)
	{
		HkUnLightFuse((IObjRW*)obj, fuse, 0.0f);
	}
}


/*bool UserCmd_ShowRestrictions(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
	{
		struct PlayerData *pPD = 0;
		while(pPD = Players.traverse_active(pPD))
		{
			uint iClientsID = HkGetClientIdFromPD(pPD);

			wstring wscMsg = L"<TRA data=\"0xfffc3b5b\" mask=\"-1\"/><TEXT>%p</TEXT>";
			wscMsg = ReplaceStr(wscMsg, L"%p", wscParam);
			HkFMsg(iClientsID, wscMsg);
		}
		return true;
	}*/

bool PirateCmd(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	uint iBaseID;
	pub::Player::GetBase(iClientID, iBaseID);
	if (!iBaseID)
	{
		PrintUserCmdText(iClientID, L"ERR Not in base");
		return true;
	}

	wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);
	HkSetRep(wscCharname, L"fc_pirate", 1.0f);

	PrintUserCmdText(iClientID, L"Do what you want cause a pirate is free, you are a pirate!");
	pub::Audio::PlaySoundEffect(iClientID, CreateID("ui_gain_level"));
	pub::Audio::PlaySoundEffect(iClientID, CreateID("dx_s070x_0801_Jacobi"));

	return true;
}


bool GiftCmd(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	HK_ERROR err;
	// Indicate an error if the command does not appear to be formatted correctly 
	// and stop processing but tell FLHook that we processed the command.
	if (wscParam.size() == 0)
	{
		PrintUserCmdText(iClientID, L"ERR Invalid parameters");
		PrintUserCmdText(iClientID, usage);
		return true;
	}

	uint iBaseID;
	pub::Player::GetBase(iClientID, iBaseID);
	if (!iBaseID)
	{
		PrintUserCmdText(iClientID, L"ERR Not in base");
		return true;
	}

	// Read the current number of credits for the player
	// and check that the character has enough cash.
	wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);
	wstring wscCash = GetParam(wscParam, L' ', 0);
	int Cash = ToInt(wscCash);

	if (Cash <= 0)
	{
		PrintUserCmdText(iClientID, L"ERR Invalid parameters");
		PrintUserCmdText(iClientID, usage);
		return true;
	}

	int iCash = 0;
	if ((err = HkGetCash(wscCharname, iCash)) != HKE_OK)
	{
		PrintUserCmdText(iClientID, L"ERR " + HkErrGetText(err));
		return true;
	}
	if (Cash > 0 && iCash < Cash)
	{
		PrintUserCmdText(iClientID, L"ERR Insufficient credits");
		return true;
	}

	// Remove cash if we're charging for it.
	if (Cash > 0)
		HkAddCash(wscCharname, 0 - Cash);

	string timestamp = pt::to_iso_string(pt::second_clock::local_time());
	wstring wscMsg = L"[%stamp] %name has gifted $%money to the kitty.";
	wscMsg = ReplaceStr(wscMsg, L"%stamp", stows(timestamp).c_str());
	wscMsg = ReplaceStr(wscMsg, L"%name", wscCharname.c_str());
	wscMsg = ReplaceStr(wscMsg, L"%money", boost::lexical_cast<std::wstring>(Cash));

	PrintUserCmdText(iClientID, wscMsg.c_str());
	string scText = wstos(wscMsg);
	GiftLogging("%s", scText.c_str());

	pub::Audio::PlaySoundEffect(iClientID, CreateID("ui_gain_level"));
	pub::Audio::PlaySoundEffect(iClientID, CreateID("dx_s076x_0401_juni"));

	return true;
}

void AdminCmd_GenerateID(CCmds* cmds, wstring argument)
{
	uint thegeneratedid = CreateID(wstos(argument).c_str());

	string s;
	stringstream out;
	out << thegeneratedid;
	s = out.str();



	wstring wscMsg = L"string <%sender> would equal to <%d> as internal id";
	wscMsg = ReplaceStr(wscMsg, L"%sender", argument.c_str());
	wscMsg = ReplaceStr(wscMsg, L"%d", stows(s).c_str());
	string scText = wstos(wscMsg);
	cmds->Print(L"OK %s\n", wscMsg.c_str());
	PMLogging("%s", scText.c_str());

	return;
}

void AdminCmd_missiontest1(CCmds* cmds, wstring argument)
{
	if (cmds->rights != RIGHT_SUPERADMIN)
	{
		cmds->Print(L"ERR No permission\n");
		return;
	}

	const wchar_t *wszTargetName = 0;
	wszTargetName = L"New mission available";

	wstring wscXML = cmds->ArgStrToEnd(1);
	wstring wscPlayerInfo = L"<RDL><PUSH/><TEXT>" + wscXML + L"</TEXT><PARA/><PARA/><POP/></RDL>";

	struct PlayerData *pPD = 0;
	while (pPD = Players.traverse_active(pPD))
	{

		uint iClientID = HkGetClientIdFromPD(pPD);

		HkChangeIDSString(iClientID, 500000, wszTargetName);
		HkChangeIDSString(iClientID, 526999, wscPlayerInfo);

		FmtStr caption(0, 0);
		caption.begin_mad_lib(500000);
		caption.end_mad_lib();

		FmtStr message(0, 0);
		message.begin_mad_lib(526999);
		message.end_mad_lib();

		pub::Player::PopUpDialog(iClientID, caption, message, POPUPDIALOG_BUTTONS_CENTER_OK);
		pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("mission_data_received"));

	}

	cmds->Print(L"OK\n");

	return;
}

void AdminCmd_missiontest2(CCmds* cmds, wstring argument)
{
	if (cmds->rights != RIGHT_SUPERADMIN)
	{
		cmds->Print(L"ERR No permission\n");
		return;
	}

	const wchar_t *wszTargetName = argument.c_str();

	struct PlayerData *pPD = 0;
	while (pPD = Players.traverse_active(pPD))
	{
		uint iClientID = HkGetClientIdFromPD(pPD);
		HkChangeIDSString(iClientID, 526999, wszTargetName);

		FmtStr caption(0, 0);
		caption.begin_mad_lib(526999);
		caption.end_mad_lib();

		pub::Player::DisplayMissionMessage(iClientID, caption, MissionMessageType::MissionMessageType_Type1, true);
	}

	cmds->Print(L"OK\n");

	return;
}

void AdminCmd_missiontest2b(CCmds* cmds, wstring argument)
{
	if (cmds->rights != RIGHT_SUPERADMIN)
	{
		cmds->Print(L"ERR No permission\n");
		return;
	}

	HKPLAYERINFO adminPlyr;
	if (HkGetPlayerInfo(cmds->GetAdminName(), adminPlyr, false) != HKE_OK || adminPlyr.iShip == 0)
	{
		cmds->Print(L"ERR Not in space\n");
		return;
	}

	uint iShip;
	pub::Player::GetShip(adminPlyr.iClientID, iShip);

	Vector pos;
	Matrix rot;
	pub::SpaceObj::GetLocation(iShip, pos, rot);

	uint iSystem;
	pub::Player::GetSystem(adminPlyr.iClientID, iSystem);

	const wchar_t *wszTargetName = argument.c_str();

	struct PlayerData *pPD = 0;
	while (pPD = Players.traverse_active(pPD))
	{
		// Get the this player's current system and location in the system.
		uint iClientID2 = HkGetClientIdFromPD(pPD);
		uint iSystem2 = 0;
		pub::Player::GetSystem(iClientID2, iSystem2);
		if (iSystem != iSystem2)
			continue;

		HkChangeIDSString(iClientID2, 526999, wszTargetName);

		FmtStr caption(0, 0);
		caption.begin_mad_lib(526999);
		caption.end_mad_lib();

		pub::Player::DisplayMissionMessage(iClientID2, caption, MissionMessageType::MissionMessageType_Type1, true);
	}

	HkChangeIDSString(adminPlyr.iClientID, 526999, wszTargetName);

	FmtStr caption(0, 0);
	caption.begin_mad_lib(526999);
	caption.end_mad_lib();

	pub::Player::DisplayMissionMessage(adminPlyr.iClientID, caption, MissionMessageType::MissionMessageType_Type1, true);

	cmds->Print(L"OK\n");

	return;
}

bool  UserCmd_MarkObjGroup(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{

	for (list<uint>::iterator Mark = MarkUsageTimer.begin(); Mark != MarkUsageTimer.end(); Mark++)
	{
		if (*Mark == iClientID)
		{
			PrintUserCmdText(iClientID, L"You must wait before you can mark another target.");
			return true;
		}
	}

	auto cShip = ClientInfo[iClientID].cship;
	if (!cShip)
	{
		PrintUserCmdText(iClientID, L"ERR Not in space");
		return true;
	}

	auto target = cShip->get_target();
	if (!target)
	{
		PrintUserCmdText(iClientID, L"ERR No target selected");
		return true;
	}

	if (!target->is_player())
	{
		PrintUserCmdText(iClientID, L"ERR Target not a player");
		return true;
	}

	uint targetShip = target->get_id();
	uint iClientIDTarget = ((CShip*)target->cobj)->ownerPlayer;

	wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);
	wstring wscTargetCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientIDTarget);
	list<GROUP_MEMBER> lstMembers;
	HkGetGroupMembers((const wchar_t*)Players.GetActiveCharacterName(iClientID), lstMembers);

	wstring wscMsg = L"Target: %name";
	wscMsg = ReplaceStr(wscMsg, L"%name", wscTargetCharname.c_str());

	wstring wscMsg2 = L"%player has set %name as group target.";
	wscMsg2 = ReplaceStr(wscMsg2, L"%name", wscTargetCharname.c_str());
	wscMsg2 = ReplaceStr(wscMsg2, L"%player", wscCharname.c_str());


	FmtStr caption(0, 0);
	caption.begin_mad_lib(526999);
	caption.end_mad_lib();

	foreach(lstMembers, GROUP_MEMBER, gm)
	{
		uint iClientShip = Players[gm->iClientID].iShipID;
		if (iClientShip == targetShip)
			continue;

		PrintUserCmdText(gm->iClientID, wscMsg2.c_str());
		HkChangeIDSString(gm->iClientID, 526999, wscMsg);

		//Register all players informed of the mark
		MarkUsageTimer.push_back(gm->iClientID);

		pub::Player::DisplayMissionMessage(gm->iClientID, caption, MissionMessageType::MissionMessageType_Type2, true);

		if (PlayerMarkArray[gm->iClientID])
		{
			pub::Player::MarkObj(gm->iClientID, PlayerMarkArray[gm->iClientID], 0);
		}

		pub::Player::MarkObj(gm->iClientID, targetShip, 1);
		PlayerMarkArray[gm->iClientID] = targetShip;
	}

	PrintUserCmdText(iClientID, L"OK");
	return true;
}

void RemoveSurplusJettisonItems()
{
	unordered_map<uint, vector<uint>> jettisonedItemsMap;
	auto cObj = dynamic_cast<CLoot*>(CObject::FindFirst(CObject::CLOOT_OBJECT));
	for (; cObj; cObj = dynamic_cast<CLoot*>(CObject::FindNext()))
	{
		jettisonedItemsMap[cObj->get_owner()].emplace_back(cObj->id);
	}
	for (const auto& jettisonData : jettisonedItemsMap)
	{
		uint itemLimit;
		if (jettisonData.first == 0)
		{
			itemLimit = maxJettisonCountNoOwner;
		}
		else
		{
			itemLimit = maxJettisonCount;
		}
		if (jettisonData.second.size() <= itemLimit)
			continue;
		//iterate from the beginning until the defined amount of elements remain
		for (uint i = 0; i < jettisonData.second.size() - itemLimit; i++)
		{
			pub::SpaceObj::Destroy(jettisonData.second[i], DestroyType::VANISH);
		}
	}
}

bool UserCmd_JettisonAll(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	uint baseID = 0;
	pub::Player::GetBase(iClientID, baseID);
	if (baseID)
	{
		PrintUserCmdText(iClientID, L"Can't jettison while docked");
		return true;
	}

	wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);
	uint iSystem = 0;
	pub::Player::GetSystem(iClientID, iSystem);
	uint iShip = 0;
	pub::Player::GetShip(iClientID, iShip);
	Vector vLoc = { 0.0f, 0.0f, 0.0f };
	Matrix mRot = { 0.0f, 0.0f, 0.0f };
	pub::SpaceObj::GetLocation(iShip, vLoc, mRot);
	vLoc.x += 30.0;

	list<CARGO_INFO> lstCargo;
	int iRemainingHoldSize = 0;
	uint items = 0;
	bool isFirstJettisonItem = true;
	if (HkEnumCargo(wscCharname, lstCargo, iRemainingHoldSize) == HKE_OK)
	{
		uint shipClass = Archetype::GetShip(Players[iClientID].iShipArchetype)->iShipClass;
		foreach(lstCargo, CARGO_INFO, item)
		{
			bool flag = false;
			pub::IsCommodity(item->iArchID, flag);
			if (!item->bMounted && flag)
			{
				if (notradelist.count(item->iArchID))
				{
					continue;
				}
				if (shipclassitems.count(item->iArchID) && !shipclassitems.at(item->iArchID).canmount.empty())
				{
					continue;
				}

				if (isFirstJettisonItem)
				{
					isFirstJettisonItem = false;
					XJettisonCargo jettisonCargo;
					jettisonCargo.iShip = iShip;
					jettisonCargo.iCount = item->iCount;
					jettisonCargo.iSlot = item->iID;
					pub::Player::SendNNMessage(iClientID, pub::GetNicknameId("cargo_jettisoned"));
					Server.JettisonCargo(iClientID, jettisonCargo);
				}
				else
				{
					HkRemoveCargo(wscCharname, item->iID, item->iCount); 
					uint lootCrateId = Archetype::GetEquipment(item->iArchID)->get_loot_appearance()->iArchID;
					Server.MineAsteroid(iSystem, vLoc, lootCrateId, item->iArchID, item->iCount, iClientID);
				}
				items++;
			}
		}
	}
	PrintUserCmdText(iClientID, L"OK, jettisoned %u item(s)", items);
	return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef bool(*_UserCmdProc)(uint, const wstring &, const wstring &, const wchar_t*);

struct USERCMD
{
	wchar_t *wszCmd;
	_UserCmdProc proc;
	wchar_t *usage;
};


USERCMD UserCmds[] =
{
	/*{ L"/showrestrictions", UserCmd_ShowRestrictions, L"Usage: /showrestrictions"},
	{ L"/showrestrictions*", UserCmd_ShowRestrictions, L"Usage: /showrestrictions"},*/
	{ L"/nodock", ADOCK::NoDockCommand, L"Usage: /nodock"},
	{ L"/nodock*", ADOCK::NoDockCommand, L"Usage: /nodock"},
	{ L"/police", ADOCK::PoliceCmd, L"Usage: /nodock" },
	{ L"/police*", ADOCK::PoliceCmd, L"Usage: /nodock" },
	{ L"/pirate", PirateCmd, L"Usage: /pirate"},
	{ L"/pirate*", PirateCmd, L"Usage: /pirate"},
	{ L"/racestart", AP::RacestartCmd, L"Usage: /racestart" },
	{ L"/gift", GiftCmd, L"Usage: /gift amount"},
	{ L"/gift*", GiftCmd, L"Usage: /gift amount"},
	{ L"/chase", AP::AlleyCmd_Chase, L"Usage: /chase <charname>"},
	{ L"/chase*", AP::AlleyCmd_Chase, L"Usage: /chase <charname>"},
	{ L"/marktarget",	UserCmd_MarkObjGroup, L"Usage: /marktarget"},
	{ L"/marktarget*",	UserCmd_MarkObjGroup, L"Usage: /marktarget"},
	{ L"/jettisonall", UserCmd_JettisonAll, L"Usage: /jettisonall"},
};


/**
This function is called by FLHook when a user types a chat string. We look at the
string they've typed and see if it starts with one of the above commands. If it
does we try to process it.
*/
bool UserCmd_Process(uint iClientID, const wstring &wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	wstring wscCmdLineLower = ToLower(wscCmd);

	// If the chat string does not match the USER_CMD then we do not handle the
	// command, so let other plugins or FLHook kick in. We require an exact match
	for (uint i = 0; (i < sizeof(UserCmds) / sizeof(USERCMD)); i++)
	{

		if (wscCmdLineLower.find(UserCmds[i].wszCmd) == 0)
		{
			// Extract the parameters string from the chat string. It should
			// be immediately after the command and a space.
			wstring wscParam = L"";
			if (wscCmd.length() > wcslen(UserCmds[i].wszCmd))
			{
				if (wscCmd[wcslen(UserCmds[i].wszCmd)] != ' ')
					continue;
				wscParam = wscCmd.substr(wcslen(UserCmds[i].wszCmd) + 1);
			}

			// Dispatch the command to the appropriate processing function.
			if (UserCmds[i].proc(iClientID, wscCmd, wscParam, UserCmds[i].usage))
			{
				// We handled the command tell FL hook to stop processing this
				// chat string.
				returncode = SKIPPLUGINS_NOFUNCTIONCALL; // we handled the command, return immediatly
				return true;
			}
		}
	}
	return false;
}

void __stdcall ShipDamageHull(IObjRW* iobj, float& incDmg, DamageList* dmg)
{
	returncode = DEFAULT_RETURNCODE;

	if (iDmgMunitionID != repairMunitionId
		|| !iobj->is_player()
		|| !dmg->iInflictorPlayerID)
		return;

	CShip* cship = reinterpret_cast<CShip*>(iobj->cobj);

	if (cship->hitPoints <= 0.0f)
	{
		incDmg = 0.0f;
		return;
	}

	const Archetype::Ship* TheShipArchHealed = cship->shiparch();

	const auto& healingData = healingMultipliers.find(TheShipArchHealed->iShipClass);
	if (healingData == healingMultipliers.end())
	{
		incDmg = 0;
		return;
	}
	float currHP = cship->hitPoints;
	float maxHP = cship->archetype->fHitPoints;

	const HEALING_DATA& healing = healingData->second;

	float maxPossibleHealing = (maxHP * healing.maxHeal) - currHP;

	if (maxPossibleHealing <= 0.0f)
	{
		incDmg = 0;
		return;
	}
	float healedHP = healing.healingStatic + (healing.healingMultiplier * maxHP);
	incDmg = -min(maxPossibleHealing, healedHP);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Actual Code
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Clean up when a client disconnects */
void ClearClientInfo(uint iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	AP::ClearClientInfo(iClientID);
	ADOCK::ClearClientInfo(iClientID);
	SCI::ClearClientInfo(iClientID);
}

void __stdcall SystemSwitchOutComplete(unsigned int iShip, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	// Make player invincible to fix JHs/JGs near mine fields sometimes
	// exploding player while jumping (in jump tunnel)
	pub::SpaceObj::SetInvincible(iShip, true, true, 0);
	if (AP::SystemSwitchOutComplete(iShip, iClientID))
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
}

void JettisonCargo(unsigned int iClientID, struct XJettisonCargo const &jc)
{
	returncode = DEFAULT_RETURNCODE;
	
	CShip* cship = ClientInfo[iClientID].cship;
	if (!cship)
	{
		return;
	}
	CECargo* cargo;
	CEquipTraverser tr(EquipmentClass::Cargo);

	while (cargo = reinterpret_cast<CECargo*>(cship->equip_manager.Traverse(tr)))
	{
		if (cargo->iSubObjId != jc.iSlot)
		{
			return;
		}

		const auto& noTradeGood = notradelist.find(cargo->archetype->iArchID);
		if (noTradeGood == notradelist.end()) {
			return;
		}

		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		const GoodInfo* gi = GoodList::find_by_id(noTradeGood->first);
		wstring goodName = HkGetWStringFromIDS(gi->iIDSName).c_str();
		if (noTradeGood->second == 0.0f)
		{
			PrintUserCmdText(iClientID, L"ERR you can't jettison %ls.", goodName.c_str());
		}
		else
		{
			uint amountToJettison = static_cast<uint>(jc.iCount * noTradeGood->second);
			PrintUserCmdText(iClientID, L"%u units of %ls jettisoned, %u units lost in the process.", jc.iCount, goodName.c_str(), jc.iCount - amountToJettison);
			pub::Player::RemoveCargo(iClientID, static_cast<ushort>(jc.iSlot), jc.iCount);
			if (amountToJettison > 0) {
				uint shipId;
				uint sysId;
				Vector pos;
				Matrix ori;
				pub::Player::GetSystem(iClientID, sysId);
				pub::Player::GetShip(iClientID, shipId);
				pub::SpaceObj::GetLocation(shipId, pos, ori);
				pos.x += 30.0;
				Server.MineAsteroid(sysId, pos, CreateID("lootcrate_ast_loot_metal"), cargo->archetype->iArchID, amountToJettison, iClientID);
			}
		}
		return;
	}
}

void AddTradeEquip(unsigned int iClientID, struct EquipDesc const &ed)
{
	if (notradelist.find(ed.iArchID) != notradelist.end())
	{
		for (auto i = notradelist.begin(); i != notradelist.end(); ++i)
		{
			if (i->first == ed.iArchID)
			{
				returncode = SKIPPLUGINS_NOFUNCTIONCALL;
				const GoodInfo* gi = GoodList::find_by_id(i->first);
				PrintUserCmdText(iClientID, L"ERR you can't trade %ls. The player will not receive it.", HkGetWStringFromIDS(gi->iIDSName).c_str());
				return;
			}
		}
	}
	else
	{
		returncode = DEFAULT_RETURNCODE;
		//PrintUserCmdText(iClientID, L"Ok you can trade this item.");
	}
}



void __stdcall BaseEnter_AFTER(unsigned int iBaseID, unsigned int iClientID)
{
	//ClearClientInfo(iClientID);
	returncode = DEFAULT_RETURNCODE;
	//wstring wscIp = L"???";
	//HkGetPlayerIP(iClientID, wscIp);

	//string scText = wstos(wscIp);
	//PMLogging("BaseEnter: %s", scText.c_str());
	AP::BaseEnter_AFTER(iBaseID, iClientID);
}




/*void __stdcall OnConnect(unsigned int iClientID)
{
	wstring wscIp = L"???";
	HkGetPlayerIP(iClientID, wscIp);

	string scText = wstos(wscIp);
	PMLogging("Connect: %s", scText.c_str());
}*/

void __stdcall PlayerLaunch_AFTER(unsigned int iShip, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	//wstring wscIp = L"???";
	//HkGetPlayerIP(client, wscIp);
	//string scText = wstos(wscIp);
	//PMLogging("PlayerLaunch: %s", scText.c_str());

	if (!ClientInfo[client].cship)
	{
		return;
	}

	ADOCK::PlayerLaunch(iShip, client);
	SCI::CheckOwned(client);
	SCI::UpdatePlayerID(client);
	PlayerMarkArray[client] = 0;
}

int __cdecl Dock_Call(unsigned int const &iShip, unsigned int const &iDockTarget, int& iCancel, enum DOCK_HOST_RESPONSE& response)
{
	returncode = DEFAULT_RETURNCODE;

	if ((response == PROCEED_DOCK || response == DOCK) && iCancel != -1)
	{
		uint iClientID = HkGetClientIDByShip(iShip);
		if (!iClientID)
		{
			return 0;
		}
		if (!ADOCK::IsDockAllowed(iShip, iDockTarget, iClientID))
		{
			iCancel = -1;
			response = DOCK_DENIED;
			return 0;
		}
		if (!SCI::CanDock(iDockTarget, iClientID))
		{
			iCancel = -1;
			response = DOCK_DENIED;
			return 0;
		}
	}
	return 0;
}

void __stdcall BaseExit(unsigned int iBaseID, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	SCI::CheckItems(iClientID);
}

void __stdcall ReqAddItem(unsigned int iArchID, char const *Hardpoint, int count, float p4, bool bMounted, unsigned int iClientID)
{
	/*
	string HP(Hardpoint);
	if (HP == "BAY")
	{
		PrintUserCmdText(iClientID, L"Triggered on add cargo");
	}
	else
	{
		PrintUserCmdText(iClientID, L"Triggered on add equip");
	}
	*/

	returncode = DEFAULT_RETURNCODE;
	if (reverseTrade[iClientID])
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
	}
}

void __stdcall GFGoodBuy(struct SGFGoodBuyInfo const &gbi, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	if (!SCI::CanBuyItem(gbi.iGoodID, client)) {
		const GoodInfo* gi = GoodList::find_by_id(gbi.iGoodID);
		switch (gi->iType) {
			case GOODINFO_TYPE_COMMODITY: {
				PrintUserCmdText(client, L"ERR Your ship can't load this cargo");
				break;
			}
			case GOODINFO_TYPE_EQUIPMENT: {
				PrintUserCmdText(client, L"ERR Your ship can't use this equipment");
				break;
			}
			default:
				PrintUserCmdText(client, L"ERR You can't buy this");
		}

		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		reverseTrade[client] = true;
		return;
	}

	reverseTrade[client] = false;
}

void __stdcall ReqChangeCash(int cash, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	if (reverseTrade[iClientID])
	{
		reverseTrade[iClientID] = false;
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
	}
}

void __stdcall ReqSetCash(int cash, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	if (reverseTrade[iClientID])
	{
		reverseTrade[iClientID] = false;
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
	}
}

void __stdcall DisConnect(unsigned int iClientID, enum  EFLConnection state)
{
	returncode = DEFAULT_RETURNCODE;
	ClearClientInfo(iClientID);
}

void __stdcall CharacterSelect_AFTER(struct CHARACTER_ID const &charId, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	ClearClientInfo(iClientID);
}

void HkTimerCheckKick()
{
	returncode = DEFAULT_RETURNCODE;

	uint curr_time = (uint)time(0);
	ADOCK::Timer();

	//Every 15 seconds, wipe the timer list.
	if ((curr_time % 15) == 0)
	{
		MarkUsageTimer.clear();
	}
	// every 7 minutes, sweep and clean up CLoot entries
	if ((curr_time % lootCleanupFrequency) == 0)
	{
		RemoveSurplusJettisonItems();
	}

	AP::Timer();
}

void Plugin_Communication_CallBack(PLUGIN_MESSAGE msg, void* data)
{
	returncode = DEFAULT_RETURNCODE;
	if (msg == CLIENT_CLOAK_INFO)
	{
		CLIENT_CLOAK_STRUCT* info = reinterpret_cast<CLIENT_CLOAK_STRUCT*>(data);
		if (info->isCloaked)
		{
			uint shipId = Players[info->iClientID].iShipID;
			for (int i = 1; i<250 ; ++i)
			{
				if (shipId == PlayerMarkArray[i])
				{
					pub::Player::MarkObj(i, shipId, 0);
					PlayerMarkArray[i] = 0;
				}
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions to hook
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "AlleyPlugin by Alley";
	p_PI->sShortName = "alley";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&HkTimerCheckKick, PLUGIN_HkTimerCheckKick, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipDamageHull, PLUGIN_ShipHullDmg, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&JettisonCargo, PLUGIN_HkIServerImpl_JettisonCargo, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&AddTradeEquip, PLUGIN_HkIServerImpl_AddTradeEquip, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&GFGoodBuy, PLUGIN_HkIServerImpl_GFGoodBuy, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ReqChangeCash, PLUGIN_HkIServerImpl_ReqChangeCash, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ReqSetCash, PLUGIN_HkIServerImpl_ReqSetCash, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseEnter_AFTER, PLUGIN_HkIServerImpl_BaseEnter_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ClearClientInfo, PLUGIN_ClearClientInfo, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&SystemSwitchOutComplete, PLUGIN_HkIServerImpl_SystemSwitchOutComplete, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch_AFTER, PLUGIN_HkIServerImpl_PlayerLaunch_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Dock_Call, PLUGIN_HkCb_Dock_Call, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseExit, PLUGIN_HkIServerImpl_BaseExit, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ReqAddItem, PLUGIN_HkIServerImpl_ReqAddItem, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&DisConnect, PLUGIN_HkIServerImpl_DisConnect, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect_AFTER, PLUGIN_HkIServerImpl_CharacterSelect_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Plugin_Communication_CallBack, PLUGIN_Plugin_Communication, 0));

	return p_PI;
}
