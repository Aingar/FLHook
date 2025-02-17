#include <fstream>
#include "hook.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkGetCash(const wstring &wscCharname, int &iCash)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	if ((iClientID != -1) && bIdString && HkIsInCharSelectMenu(iClientID))
		return HKE_NO_CHAR_SELECTED;
	else if ((iClientID != -1) && !HkIsInCharSelectMenu(iClientID)) { // player logged in
		pub::Player::InspectCash(iClientID, iCash);
		return HKE_OK;
	}
	else { // player not logged in
		wstring wscDir;
		if (!HKHKSUCCESS(HkGetAccountDirName(wscCharname, wscDir)))
			return HKE_CHAR_DOES_NOT_EXIST;
		wstring wscFile;
		HkGetCharFileName(wscCharname, wscFile);

		string scCharFile = scAcctPath + wstos(wscDir) + "\\" + wstos(wscFile) + ".fl";

		FILE* fTest = fopen(scCharFile.c_str(), "r");
		if (!fTest)
			return HKE_CHAR_DOES_NOT_EXIST;
		else
			fclose(fTest);

		if (HkIsEncoded(scCharFile)) {
			string scCharFileNew = scCharFile + ".ini";
			if (!flc_decode(scCharFile.c_str(), scCharFileNew.c_str()))
				return HKE_COULD_NOT_DECODE_CHARFILE;

			iCash = IniGetI(scCharFileNew, "Player", "money", -1);
			DeleteFile(scCharFileNew.c_str());
		}
		else {
			iCash = IniGetI(scCharFile, "Player", "money", -1);
		}

		return HKE_OK;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkAddCash(const wstring &wscCharname, int iAmount)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	uint iClientIDAcc = 0;
	if (iClientID == -1) {
		CAccount *acc = HkGetAccountByCharname(wscCharname);
		if (!acc)
			return HKE_CHAR_DOES_NOT_EXIST;
		iClientIDAcc = HkGetClientIdFromAccount(acc);
	}
	else
		iClientIDAcc = iClientID;

	if ((iClientID != -1) && bIdString && HkIsInCharSelectMenu(iClientID))
		return HKE_NO_CHAR_SELECTED;
	else if ((iClientID != -1) && !HkIsInCharSelectMenu(iClientID)) { // player logged in
		float value;
		pub::Player::GetAssetValue(iClientID, value);
		if (2'000'000'000 - static_cast<int>(value) < iAmount)
		{
			iAmount = 2'000'000'000 - static_cast<int>(value);
			PrintUserCmdText(iClientID, L"Warning: Would exceed 2bln ship worth, trimming cash received");
		}
		pub::Player::AdjustCash(iClientID, iAmount);
		return HKE_OK;
	}
	else { // player not logged in
		wstring wscDir;
		if (!HKHKSUCCESS(HkGetAccountDirName(wscCharname, wscDir)))
			return HKE_CHAR_DOES_NOT_EXIST;
		wstring wscFile;
		HkGetCharFileName(wscCharname, wscFile);

		string scCharFile = scAcctPath + wstos(wscDir) + "\\" + wstos(wscFile) + ".fl";
		int iRet;
		if (HkIsEncoded(scCharFile)) {
			string scCharFileNew = scCharFile + ".ini";

			if (!flc_decode(scCharFile.c_str(), scCharFileNew.c_str()))
				return HKE_COULD_NOT_DECODE_CHARFILE;

			iRet = IniGetI(scCharFileNew, "Player", "money", -1);
			// Add a space to the value so the ini file line looks like "<key> = <value>"
			// otherwise IFSO can't decode the file correctly
			IniWrite(scCharFileNew, "Player", "money", " " + itos(iRet + iAmount));

			if (!set_bDisableCharfileEncryption)
				if (!flc_encode(scCharFileNew.c_str(), scCharFile.c_str()))
					return HKE_COULD_NOT_ENCODE_CHARFILE;

			DeleteFile(scCharFileNew.c_str());
		}
		else {
			iRet = IniGetI(scCharFile, "Player", "money", -1);
			// Add a space to the value so the ini file line looks like "<key> = <value>"
			// otherwise IFSO can't decode the file correctly
			IniWrite(scCharFile, "Player", "money", " " + itos(iRet + iAmount));
		}

		if (HkIsInCharSelectMenu(wscCharname) || (iClientIDAcc != -1))
		{ // money fix in case player logs in with this account
			bool bFound = false;
			wstring wscCharnameLower = ToLower(wscCharname);
			foreach(ClientInfo[iClientIDAcc].lstMoneyFix, MONEY_FIX, i)
			{
				if ((*i).wscCharname == wscCharnameLower)
				{
					(*i).iAmount += iAmount;
					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				MONEY_FIX mf;
				mf.wscCharname = wscCharnameLower;
				mf.iAmount = iAmount;
				ClientInfo[iClientIDAcc].lstMoneyFix.push_back(mf);
			}
		}

		return HKE_OK;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkKick(CAccount *acc)
{
	acc->ForceLogout();
	return HKE_OK;
}

HK_ERROR HkKick(const wstring &wscCharname)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	if (iClientID == -1)
		return HKE_PLAYER_NOT_LOGGED_IN;

	CAccount *acc = Players.FindAccountFromClientID(iClientID);
	acc->ForceLogout();
	return HKE_OK;
}

HK_ERROR HkKickReason(const wstring &wscCharname, const wstring &wscReason)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	if (iClientID == -1)
		return HKE_PLAYER_NOT_LOGGED_IN;

	if (wscReason.length())
		HkMsgAndKick(iClientID, wscReason, set_iKickMsgPeriod);
	else
		HkKick(Players.FindAccountFromClientID(iClientID));

	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkBan(const wstring &wscCharname, bool bBan)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	CAccount *acc;
	if (iClientID != -1)
		acc = Players.FindAccountFromClientID(iClientID);
	else {
		if (!(acc = HkGetAccountByCharname(wscCharname)))
			return HKE_CHAR_DOES_NOT_EXIST;
	}

	wstring wscID = HkGetAccountID(acc);
	flstr *flStr = CreateWString(wscID.c_str());
	Players.BanAccount(*flStr, bBan);
	FreeWString(flStr);
	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkBeam(const wstring &wscCharname, const wstring &wscBasename)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	// check if logged in
	if (iClientID == -1)
		return HKE_PLAYER_NOT_LOGGED_IN;

	string scBasename = wstos(wscBasename);
	// check if ship in space
	uint iShip = 0;
	pub::Player::GetShip(iClientID, iShip);
	if (!iShip)
		return HKE_PLAYER_NOT_IN_SPACE;

	// get base id
	uint iBaseID;

	if (pub::GetBaseID(iBaseID, scBasename.c_str()) == -4)
	{
		string scBaseShortcut = IniGetS(set_scCfgFile, "names", wstos(wscBasename), "");
		if (!scBaseShortcut.length())
		{
			typedef int(*_GetString)(LPVOID, uint, wchar_t*, uint);
			_GetString GetString = (_GetString)0x4176b0;
			Universe::IBase *pBase = Universe::GetFirstBase();
			while (pBase)
			{
				wchar_t buf[1024];
				GetString(NULL, pBase->iBaseIDS, buf, 1024);
				if (wcsstr(buf, wscBasename.c_str()))
				{
					// Ignore the intro bases.
					if (_strnicmp("intro", pBase->cNickname, 5) != 0)
					{
						iBaseID = pBase->iBaseID;
						break;
					}
				}
				pBase = Universe::GetNextBase();
			}
			if (iBaseID == 0)
				return HKE_INVALID_BASENAME;
		}
		else if (pub::GetBaseID(iBaseID, scBaseShortcut.c_str()) == -4)
			return HKE_INVALID_BASENAME;
	}

	uint iSysID;
	pub::Player::GetSystem(iClientID, iSysID);
	Universe::IBase* base = Universe::get_base(iBaseID);

	pub::Player::ForceLand(iClientID, iBaseID); // beam

	// if not in the same system, emulate F1 charload
	if (base->iSystemID != iSysID) {
		Server.BaseEnter(iBaseID, iClientID);
		Server.BaseExit(iBaseID, iClientID);
		wstring wscCharFileName;
		HkGetCharFileName(ARG_CLIENTID(iClientID), wscCharFileName);
		wscCharFileName += L".fl";
		CHARACTER_ID cID;
		strcpy(cID.charFilename, wstos(wscCharFileName.substr(0, 14)).c_str());
		Server.CharacterSelect(cID, iClientID);
	}

	return HKE_OK;
}

HK_ERROR HkBeamById(const uint clientId, const uint baseId)
{
	uint systemId;
	pub::Player::GetSystem(clientId, systemId);
	Universe::IBase* baseInfo = Universe::get_base(baseId);
	if (!baseInfo)
	{
		PrintUserCmdText(clientId, L"Beam impossible, invalid base %x!", baseId);
		return HKE_INVALID_BASENAME;
	}
	pub::Player::ForceLand(clientId, baseId);
	if (systemId != baseInfo->iSystemID)
	{
		auto charName = (const wchar_t*)Players.GetActiveCharacterName(clientId);
		Server.BaseEnter(baseInfo->iBaseID, clientId);
		Server.BaseExit(baseInfo->iBaseID, clientId);
		wstring wscCharFileName;
		HkGetCharFileName(charName, wscCharFileName);
		wscCharFileName += L".fl";
		CHARACTER_ID cID;
		strcpy(cID.charFilename, wstos(wscCharFileName.substr(0, 14)).c_str());
		Server.CharacterSelect(cID, clientId);
	}
	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkSaveChar(const wstring &wscCharname)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	if (iClientID == -1)
		return HKE_PLAYER_NOT_LOGGED_IN;

	pub::Save(iClientID, 1);

	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct EQ_ITEM
{
	EQ_ITEM *next;
	uint i2;
	ushort s1;
	ushort sID;
	uint iGoodID;
	CacheString hardpoint;
	bool bMounted;
	char sz[3];
	float fStatus;
	uint iCount;
	bool bMission;
};

HK_ERROR HkEnumCargo(const wstring &wscCharname, list<CARGO_INFO> &lstCargo, int &iRemainingHoldSize)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	if (iClientID == -1 || HkIsInCharSelectMenu(iClientID))
		return HKE_PLAYER_NOT_LOGGED_IN;

	lstCargo.clear();

	char *szClassPtr;
	memcpy(&szClassPtr, &Players, 4);
	szClassPtr += 0x418 * (iClientID - 1);

	EQ_ITEM *eqLst;
	memcpy(&eqLst, szClassPtr + 0x27C, 4);
	EQ_ITEM *eq;
	eq = eqLst->next;
	while (eq != eqLst)
	{
		CARGO_INFO ci = { eq->sID, (int)eq->iCount, eq->iGoodID, eq->fStatus, eq->bMission, eq->bMounted, eq->hardpoint };
		lstCargo.push_back(ci);

		eq = eq->next;
	}

	float fRemHold;
	pub::Player::GetRemainingHoldSize(iClientID, fRemHold);
	iRemainingHoldSize = (int)fRemHold;
	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkRemoveCargo(const wstring &wscCharname, uint iID, uint iCount)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	if (iClientID == -1 || HkIsInCharSelectMenu(iClientID))
		return HKE_PLAYER_NOT_LOGGED_IN;

	for (auto& eq : Players[iClientID].equipDescList.equip)
	{
		if (eq.sID == iID && eq.iCount < iCount)
		{
			iCount = eq.iCount; // trying to remove more than actually there, thus fix
		}
	}

	pub::Player::RemoveCargo(iClientID, iID, iCount);

	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkAddCargo(const wstring &wscCharname, uint iGoodID, int iCount, bool bMission)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	if (iClientID == -1 || HkIsInCharSelectMenu(iClientID))
		return HKE_PLAYER_NOT_LOGGED_IN;

			// add
	const GoodInfo *gi;
	if (!(gi = GoodList::find_by_id(iGoodID)))
		return HKE_INVALID_GOOD;

	bool bMultiCount;
	memcpy(&bMultiCount, (char*)gi + 0x70, 1);


	uint iBase = 0;
	pub::Player::GetBase(iClientID, iBase);
	uint iLocation = 0;
	pub::Player::GetLocation(iClientID, iLocation);

	// trick cheat detection
	if (iBase)
	{
		if (iLocation)
		{
			Server.LocationExit(iLocation, iClientID);
		}
		Server.BaseExit(iBase, iClientID);
		if (!HkIsValidClientID(iClientID)) // got cheat kicked
		{
			return HKE_PLAYER_NOT_LOGGED_IN;
		}
	}

	if (bMultiCount) { 

		for(auto& eq : Players[iClientID].equipDescList.equip)
		{
			if(eq.iArchID == iGoodID && eq.bMission != bMission)
			{
				iCount += eq.iCount;
				HkRemoveCargo(wscCharname, eq.sID, eq.iCount);
			}
		}

		pub::Player::AddCargo(iClientID, iGoodID, iCount, 1, bMission);
	}
	else
	{
		for (int i = 0; (i < iCount); i++)
		{
			pub::Player::AddCargo(iClientID, iGoodID, 1, 1, bMission);
		}
	}

	if (iBase)
	{ // player docked on base
		///////////////////////////////////////////////////
		// fix, else we get anti-cheat msg when undocking
		// this DOES NOT disable anti-cheat-detection, we're
		// just making some adjustments so that we dont get kicked

		Server.BaseEnter(iBase, iClientID);
		if (iLocation)
		{
			Server.LocationEnter(iLocation, iClientID);
		}
	}

	return HKE_OK;
}

HK_ERROR HkAddCargo(const wstring &wscCharname, const wstring &wscGood, int iCount, bool bMission)
{
	uint iGoodID = ToInt(wscGood.c_str());
	if (!iGoodID)
		pub::GetGoodID(iGoodID, wstos(wscGood).c_str());
	if (!iGoodID)
		return HKE_INVALID_GOOD;

	return HkAddCargo(wscCharname, iGoodID, iCount, bMission);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkRename(const wstring &wscCharname, const wstring &wscNewCharname, bool bOnlyDelete)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	if ((iClientID == -1) && !HkGetAccountByCharname(wscCharname))
		return HKE_CHAR_DOES_NOT_EXIST;

	if (!bOnlyDelete && HkGetAccountByCharname(wscNewCharname))
		return HKE_CHARNAME_ALREADY_EXISTS;

	if (!bOnlyDelete && (wscNewCharname.length() > 23))
		return HKE_CHARNAME_TOO_LONG;

	if (!bOnlyDelete && !wscNewCharname.length())
		return HKE_CHARNAME_TOO_SHORT;

	INI_Reader ini;
	if (!bOnlyDelete && !(ini.open("..\\DATA\\CHARACTERS\\newcharacter.ini", false)))
		return HKE_MPNEWCHARACTERFILE_NOT_FOUND_OR_INVALID;

	CAccount *acc;
	wstring wscOldCharname;
	if (iClientID != -1) {
		acc = Players.FindAccountFromClientID(iClientID);
		wscOldCharname = (wchar_t*)Players.GetActiveCharacterName(iClientID);
	}
	else {
		wscOldCharname = wscCharname;
		acc = HkGetAccountByCharname(wscCharname);
	}

	wstring wscAccountDirname;
	HkGetAccountDirName(acc, wscAccountDirname);
	wstring wscNewFilename;
	HkGetCharFileName(wscNewCharname, wscNewFilename);
	wstring wscOldFilename;
	HkGetCharFileName(wscOldCharname, wscOldFilename);

	string scNewCharfilePath = scAcctPath + wstos(wscAccountDirname) + "\\" + wstos(wscNewFilename) + ".fl";
	string scOldCharfilePath = scAcctPath + wstos(wscAccountDirname) + "\\" + wstos(wscOldFilename) + ".fl";

	if (bOnlyDelete) {
		// delete character
		flstr *str = CreateWString(wscOldCharname.c_str());
		HkLockAccountAccess(acc, true); // also kicks player on this account
		Players.DeleteCharacterFromName(*str);
		FreeWString(str);
		HkUnlockAccountAccess(acc);
		return HKE_OK;
	}


	HkLockAccountAccess(acc, true); // kick player if online
	HkUnlockAccountAccess(acc);

	// Copy existing char file into tmp
	string scTmpPath = scOldCharfilePath + ".tmp";
	DeleteFile(scTmpPath.c_str());
	CopyFile(scOldCharfilePath.c_str(), scTmpPath.c_str(), FALSE);

	// Delete existing char otherwise a rename of the char in slot 5 fails.
	flstr *str = CreateWString(wscOldCharname.c_str());
	Players.DeleteCharacterFromName(*str);
	FreeWString(str);

	// Emulate char create
	SLoginInfo logindata;
	wcsncpy(logindata.wszAccount, HkGetAccountID(acc).c_str(), 36);
	Players.login(logindata, MAX_CLIENT_ID + 1);

	SCreateCharacterInfo newcharinfo;
	wcsncpy(newcharinfo.wszCharname, wscNewCharname.c_str(), 23);
	newcharinfo.wszCharname[23] = 0;

	newcharinfo.iNickName = 0;
	newcharinfo.iBase = 0;
	newcharinfo.iPackage = 0;
	newcharinfo.iPilot = 0;

	while (ini.read_header())
	{
		if (ini.is_header("Faction"))
		{
			while (ini.read_value())
			{
				if (ini.is_value("nickname"))
					newcharinfo.iNickName = CreateID(ini.get_value_string());
				else if (ini.is_value("base"))
					newcharinfo.iBase = CreateID(ini.get_value_string());
				else if (ini.is_value("Package"))
					newcharinfo.iPackage = CreateID(ini.get_value_string());
				else if (ini.is_value("Pilot"))
					newcharinfo.iPilot = CreateID(ini.get_value_string());
			}
			break;
		}
	}
	ini.close();

	if (newcharinfo.iNickName == 0)
		newcharinfo.iNickName = CreateID("new_player");
	if (newcharinfo.iBase == 0)
		newcharinfo.iBase = CreateID("Li01_01_Base");
	if (newcharinfo.iPackage == 0)
		newcharinfo.iPackage = CreateID("ge_fighter");
	if (newcharinfo.iPilot == 0)
		newcharinfo.iPilot = CreateID("trent");

	// Fill struct with valid data (though it isnt used it is needed)
	newcharinfo.iDunno[4] = 65536;
	newcharinfo.iDunno[5] = 65538;
	newcharinfo.iDunno[6] = 0;
	newcharinfo.iDunno[7] = 1058642330;
	newcharinfo.iDunno[8] = 3206125978;
	newcharinfo.iDunno[9] = 65537;
	newcharinfo.iDunno[10] = 0;
	newcharinfo.iDunno[11] = 3206125978;
	newcharinfo.iDunno[12] = 65539;
	newcharinfo.iDunno[13] = 65540;
	newcharinfo.iDunno[14] = 65536;
	newcharinfo.iDunno[15] = 65538;
	Server.CreateNewCharacter(newcharinfo, MAX_CLIENT_ID + 1);
	HkSaveChar(wscNewCharname);
	Players.logout(MAX_CLIENT_ID + 1);

	// Decode the backup of the old char and overwrite the new char file
	if (!flc_decode(scTmpPath.c_str(), scNewCharfilePath.c_str()))
	{ // file wasn't encoded, thus simply rename it
		DeleteFile(scNewCharfilePath.c_str()); // just to get sure...
		CopyFile(scTmpPath.c_str(), scNewCharfilePath.c_str(), FALSE);
	}
	DeleteFile(scTmpPath.c_str());

	// Update the char name in the new char file.
	// Add a space to the value so the ini file line looks like "<key> = <value>"
	// otherwise Ioncross Server Operator can't decode the file correctly
	string scValue = " ";
	for (uint i = 0; (i < wscNewCharname.length()); i++)
	{
		char cHiByte = wscNewCharname[i] >> 8;
		char cLoByte = wscNewCharname[i] & 0xFF;
		char szBuf[8];
		sprintf(szBuf, "%02X%02X", ((uint)cHiByte) & 0xFF, ((uint)cLoByte) & 0xFF);
		scValue += szBuf;
	}
	IniWrite(scNewCharfilePath, "Player", "Name", scValue);

	// Re-encode the char file if needed.
	if (!set_bDisableCharfileEncryption)
		if (!flc_encode(scNewCharfilePath.c_str(), scNewCharfilePath.c_str()))
			return HKE_COULD_NOT_ENCODE_CHARFILE;

	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkMsgAndKick(uint iClientID, const wstring &wscReason, uint iIntervall)
{
	if (!ClientInfo[iClientID].tmKickTime)
	{
		wstring wscMsg = ReplaceStr(set_wscKickMsg, L"%reason", XMLText(wscReason));
		HkFMsg(iClientID, wscMsg);
		ClientInfo[iClientID].tmKickTime = timeInMS() + iIntervall;
	}

	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkKill(const wstring &wscCharname)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	// check if logged in
	if (iClientID == -1)
		return HKE_PLAYER_NOT_LOGGED_IN;

	uint iShip;
	pub::Player::GetShip(iClientID, iShip);
	if (!iShip)
		return HKE_PLAYER_NOT_IN_SPACE;

	ClientInfo[iClientID].dmgLastPlayerId = 0;
	ClientInfo[iClientID].dmgLastCause = DamageCause::Admin;
	pub::SpaceObj::SetRelativeHealth(iShip, 0.0f);
	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkGetReservedSlot(const wstring &wscCharname, bool &bResult)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	CAccount *acc;
	if (iClientID != -1)
		acc = Players.FindAccountFromClientID(iClientID);
	else
		acc = HkGetAccountByCharname(wscCharname);

	if (!acc)
		return HKE_CHAR_DOES_NOT_EXIST;

	wstring wscDir;
	HkGetAccountDirName(acc, wscDir);
	string scUserFile = scAcctPath + wstos(wscDir) + "\\flhookuser.ini";

	bResult = IniGetB(scUserFile, "Settings", "ReservedSlot", false);
	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkSetReservedSlot(const wstring &wscCharname, bool bReservedSlot)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	CAccount *acc;
	if (iClientID != -1)
		acc = Players.FindAccountFromClientID(iClientID);
	else
		acc = HkGetAccountByCharname(wscCharname);

	if (!acc)
		return HKE_CHAR_DOES_NOT_EXIST;

	wstring wscDir;
	HkGetAccountDirName(acc, wscDir);
	string scUserFile = scAcctPath + wstos(wscDir) + "\\flhookuser.ini";

	if (bReservedSlot)
		IniWrite(scUserFile, "Settings", "ReservedSlot", "yes");
	else
		IniWrite(scUserFile, "Settings", "ReservedSlot", "no");
	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkResetRep(const wstring &wscCharname)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	// check if logged in
	if (iClientID == -1)
		return HKE_PLAYER_NOT_LOGGED_IN;

	INI_Reader ini;
	if (!ini.open("mpnewcharacter.fl", false))
		return HKE_MPNEWCHARACTERFILE_NOT_FOUND_OR_INVALID;

	ini.read_header();
	if (!ini.is_header("Player"))
	{
		ini.close();
		return HKE_MPNEWCHARACTERFILE_NOT_FOUND_OR_INVALID;
	}

	int iPlayerRep;
	pub::Player::GetRep(iClientID, iPlayerRep);
	while (ini.read_value())
	{
		if (ini.is_value("house"))
		{
			float fRep = ini.get_value_float(0);
			const char *szRepGroupName = ini.get_value_string(1);

			uint iRepGroupID;
			pub::Reputation::GetReputationGroup(iRepGroupID, szRepGroupName);
			pub::Reputation::SetReputation(iPlayerRep, iRepGroupID, fRep);
		}
	}

	ini.close();
	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkSetRep(const wstring &wscCharname, const wstring &wscRepGroup, float fValue)
{
	HK_GET_CLIENTID(iClientID, wscCharname);
	// check if logged in
	if (iClientID == -1)
		return HKE_PLAYER_NOT_LOGGED_IN;

	uint iRepGroupID;
	pub::Reputation::GetReputationGroup(iRepGroupID, wstos(wscRepGroup).c_str());
	if (iRepGroupID == -1)
		return HKE_INVALID_REP_GROUP;

	int iPlayerRep;
	pub::Player::GetRep(iClientID, iPlayerRep);
	pub::Reputation::SetReputation(iPlayerRep, iRepGroupID, fValue);
	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkGetRep(const wstring &wscCharname, const wstring &wscRepGroup, float &fValue)
{
	HK_GET_CLIENTID(iClientID, wscCharname);
	if (iClientID == -1)
		return HKE_PLAYER_NOT_LOGGED_IN;

	uint iRepGroupID;
	pub::Reputation::GetReputationGroup(iRepGroupID, wstos(wscRepGroup).c_str());
	if (iRepGroupID == -1)
		return HKE_INVALID_REP_GROUP;

	int iPlayerRep;
	pub::Player::GetRep(iClientID, iPlayerRep);
	pub::Reputation::GetGroupFeelingsTowards(iPlayerRep, iRepGroupID, fValue);
	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkGetGroupMembers(const wstring &wscCharname, list<GROUP_MEMBER> &lstMembers)
{
	lstMembers.clear();
	HK_GET_CLIENTID(iClientID, wscCharname);

	// check if logged in
	if (iClientID == -1)
		return HKE_PLAYER_NOT_LOGGED_IN;

	// hey, at least it works! beware of the VC optimiser.
	uint* vMembers[100] = { 0 };
	pub::Player::GetGroupMembers(iClientID, (vector<uint>&)vMembers);

	for (uint *i = vMembers[1]; i != vMembers[2]; i++)
	{
		GROUP_MEMBER gm;
		gm.iClientID = *i;
		gm.wscCharname = (wchar_t*)Players.GetActiveCharacterName(*i);
		lstMembers.push_back(gm);
	}

	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkReadCharFile(const wstring &wscCharname, list<wstring> &lstOutput)
{
	lstOutput.clear();
	HK_GET_CLIENTID(iClientID, wscCharname);

	wstring wscDir;
	CAccount *acc;
	if (iClientID != -1) {
		acc = Players.FindAccountFromClientID(iClientID);
		const wchar_t *wszCharname = (wchar_t*)Players.GetActiveCharacterName(iClientID);
		if (!wszCharname)
			return HKE_NO_CHAR_SELECTED;

		if (!HKHKSUCCESS(HkGetAccountDirName(wszCharname, wscDir)))
			return HKE_CHAR_DOES_NOT_EXIST;
	}
	else {
		if (!HKHKSUCCESS(HkGetAccountDirName(wscCharname, wscDir)))
			return HKE_CHAR_DOES_NOT_EXIST;
	}

	wstring wscFile;
	HkGetCharFileName(wscCharname, wscFile);
	string scCharFile = scAcctPath + wstos(wscDir) + "\\" + wstos(wscFile) + ".fl";
	string scFileToRead;
	bool bDeleteAfter;
	if (HkIsEncoded(scCharFile)) {
		string scCharFileNew = scCharFile + ".ini";
		if (!flc_decode(scCharFile.c_str(), scCharFileNew.c_str()))
			return HKE_COULD_NOT_DECODE_CHARFILE;
		scFileToRead = scCharFileNew;
		bDeleteAfter = true;
	}
	else {
		scFileToRead = scCharFile;
		bDeleteAfter = false;
	}

	ifstream ifs;
	ifs.open(scFileToRead.c_str(), ios_base::in);
	if (!ifs.is_open())
		return HKE_UNKNOWN_ERROR;

	string scLine;
	while (getline(ifs, scLine))
		lstOutput.push_back(stows(scLine));
	ifs.close();
	if (bDeleteAfter)
		DeleteFile(scFileToRead.c_str());
	return HKE_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

HK_ERROR HkWriteCharFile(const wstring &wscCharname, wstring wscData)
{
	HK_GET_CLIENTID(iClientID, wscCharname);

	wstring wscDir;
	CAccount *acc;
	if (iClientID != -1) {
		acc = Players.FindAccountFromClientID(iClientID);
		const wchar_t *wszCharname = (wchar_t*)Players.GetActiveCharacterName(iClientID);
		if (!wszCharname)
			return HKE_NO_CHAR_SELECTED;

		if (!HKHKSUCCESS(HkGetAccountDirName(wszCharname, wscDir)))
			return HKE_CHAR_DOES_NOT_EXIST;
	}
	else {
		if (!HKHKSUCCESS(HkGetAccountDirName(wscCharname, wscDir)))
			return HKE_CHAR_DOES_NOT_EXIST;
	}

	wstring wscFile;
	HkGetCharFileName(wscCharname, wscFile);
	string scCharFile = scAcctPath + wstos(wscDir) + "\\" + wstos(wscFile) + ".fl";
	string scFileToWrite;
	bool bEncode;
	if (HkIsEncoded(scCharFile)) {
		scFileToWrite = scCharFile + ".ini";
		bEncode = true;
	}
	else {
		scFileToWrite = scCharFile;
		bEncode = false;
	}

	ofstream ofs;
	ofs.open(scFileToWrite.c_str(), ios_base::out);
	if (!ofs.is_open())
		return HKE_UNKNOWN_ERROR;

	size_t iPos;
	while ((iPos = wscData.find(L"\\n")) != -1)
	{
		wstring wscLine = wscData.substr(0, iPos);
		ofs << wstos(wscLine) << endl;
		wscData.erase(0, iPos + 2);
	}

	if (wscData.length())
		ofs << wstos(wscData);

	ofs.close();
	if (bEncode)
	{
		flc_encode(scFileToWrite.c_str(), scCharFile.c_str());
		DeleteFile(scFileToWrite.c_str());
	}
	return HKE_OK;
}
