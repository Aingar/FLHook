// Player Control plugin for FLHookPlugin
// Feb 2010 by Cannon
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>

#define POPUPDIALOG_BUTTONS_LEFT_YES 1
#define POPUPDIALOG_BUTTONS_CENTER_NO 2
#define POPUPDIALOG_BUTTONS_RIGHT_LATER 4
#define POPUPDIALOG_BUTTONS_CENTER_OK 8

//#include <PluginUtilities.h>
#include "Main.h"

struct PlayerSetInfo
{
	bool initialized = false;
	bool pulledInfos = false;
	bool changedSinceLastLaunch = true;
	wstring playerInfo;
	vector<wstring> infoVector;
};

PlayerSetInfo playerInfoData[MAX_CLIENT_ID + 1];
static const uint SETINFO_START_INFOCARD = 267500;

#define RSRCID_PLAYERINFO_TITLE 500000
#define RSRCID_PLAYERINFO_TEXT RSRCID_PLAYERINFO_TITLE + 1
#define MAX_PARAGRAPHS 5
#define MAX_CHARACTERS 2000

void PropagatePlayerInfo(uint clientId)
{
	if (playerInfoData[clientId].changedSinceLastLaunch)
	{
		PlayerData* pd = nullptr;
		while (pd = Players.traverse_active(pd))
		{
			if (pd->iOnlineID == clientId)
			{
				continue;
			}
			HkChangeIDSString(pd->iOnlineID, clientId + SETINFO_START_INFOCARD, playerInfoData[clientId].playerInfo);
		}
		playerInfoData[clientId].changedSinceLastLaunch = false;
	}
}

void FetchPlayerInfo(uint clientId)
{
	if (!playerInfoData[clientId].pulledInfos)
	{
		for (int i = 1; i < MAX_CLIENT_ID + 1; i++)
		{
			if (playerInfoData[i].initialized)
			{
				HkChangeIDSString(clientId, SETINFO_START_INFOCARD + i, playerInfoData[i].playerInfo);
			}
		}
		playerInfoData[clientId].pulledInfos = true;
	}
}

void InitializePlayerInfo(uint clientId)
{
	playerInfoData[clientId].initialized = true;
	wstring playerName = (const wchar_t*)Players.GetActiveCharacterName(clientId);

	string scFilePath = GetUserFilePath(playerName, "-info.ini");
	INI_Reader ini;
	if (!ini.open(scFilePath.c_str(), false))
	{
		return;
	}
	wstring playerInfo = L"<RDL><PUSH/>";
	uint paraCount = 0;
	while (ini.read_header())
	{
		if (!ini.is_header("PlayerInfo"))
		{
			break;
		}
		while (ini.read_value())
		{
			if (!ini.is_value("info"))
			{
				break;
			}
			if (++paraCount > MAX_PARAGRAPHS)
			{
				break;
			}
			playerInfo += L"<TEXT>";
			wstring playerInfoLine = stows(ini.get_value_string());
			playerInfoData[clientId].infoVector.emplace_back(playerInfoLine);
			playerInfo += playerInfoLine;
			playerInfo += L"</TEXT><PARA/><PARA/>";
		}
	}
	playerInfo += L"<POP/></RDL>";

	playerInfoData[clientId].playerInfo = playerInfo;
}

void FormatString(wstring& text)
{
	text = ReplaceStr(text, L"<", L"&#60;");
	text = ReplaceStr(text, L">", L"&#62;");
	text = ReplaceStr(text, L"&", L"&#38;");
	text = ReplaceStr(text, L"\\n", L"</TEXT><PARA/><TEXT>");
}

void RecalculateInfoText(uint clientId)
{
	wstring& playerInfo = playerInfoData[clientId].playerInfo;

	playerInfo = L"<RDL><PUSH/>";
	for (wstring& info : playerInfoData[clientId].infoVector)
	{
		playerInfoData[clientId].playerInfo += L"<TEXT>";
		playerInfoData[clientId].playerInfo += info;
		playerInfoData[clientId].playerInfo += L"</TEXT><PARA/><PARA/>";
	}
	playerInfo += L"<POP/></RDL>";
	playerInfoData[clientId].changedSinceLastLaunch = true;
}

void WriteInfoFile(uint clientId, string filePath)
{
	FILE* file = fopen(filePath.c_str(), "w");
	if (!file)
	{
		return;
	}
	fprintf(file, "[PlayerInfo]\n");
	for (wstring& infoLine : playerInfoData[clientId].infoVector)
	{
		fprintf(file, "info = %s\n", wstos(infoLine).c_str());
	}
	fclose(file);
}

bool PlayerInfo::UserCmd_SetInfo(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	uint iPara = ToInt(GetParam(wscParam, ' ', 0));
	const wstring &wscCommand = GetParam(wscParam, ' ', 1);
	const wstring &wscMsg = GetParamToEnd(wscParam, ' ', 2);

	if (!iPara || iPara > MAX_PARAGRAPHS)
	{
		PrintUserCmdText(iClientID, L"ERR Invalid paragraph!");
		return false;
	}
	iPara--;

	if (!playerInfoData[iClientID].initialized)
	{
		InitializePlayerInfo(iClientID);
	}

	string scFilePath = GetUserFilePath((const wchar_t*)Players.GetActiveCharacterName(iClientID), "-info.ini");
	if (scFilePath.empty())
	{
		return false;
	}

	auto& infoVec = playerInfoData[iClientID].infoVector;
	if (wscCommand == L"a")
	{
		static wstring temp = L"</TEXT><PARA/><PARA/>";
		wstring currString = playerInfoData[iClientID].playerInfo;

		if (iPara > infoVec.size())
		{
			PrintUserCmdText(iClientID, L"ERR You can't skip paragraphs!");
			return false;
		}
		
		if (!playerInfoData[iClientID].infoVector.empty() && (wscMsg.size() + playerInfoData[iClientID].infoVector[iPara].length()) > MAX_CHARACTERS)
		{
			PrintUserCmdText(iClientID, L"ERR Text will be too long!(including formatting) Current lenght: %u, Max Lenght: %u", playerInfoData[iClientID].playerInfo.length(), MAX_CHARACTERS);
			return false;
		}
		
		if (iPara < infoVec.size())
		{
			infoVec[iPara] += wscMsg;
		}
		else
		{
			infoVec.emplace_back(wscMsg);
		}
		RecalculateInfoText(iClientID);
		WriteInfoFile(iClientID, scFilePath);
		PropagatePlayerInfo(iClientID);
		PrintUserCmdText(iClientID, L"OK");
	}
	else if (wscCommand == L"d")
	{
		infoVec.erase(infoVec.begin() + iPara);
		PrintUserCmdText(iClientID, L"OK");
		playerInfoData[iClientID].initialized = false;
	}
	else
	{
		PrintUserCmdText(iClientID, L"ERR Invalid parameters");
		PrintUserCmdText(iClientID, L"/setinfo <paragraph> <command> <text>");
		PrintUserCmdText(iClientID, L"|  <paragraph> The paragraph number in the range 1-%d", MAX_PARAGRAPHS);
		PrintUserCmdText(iClientID, L"|  <command> The command to perform on the paragraph, 'a' for append, 'd' for delete");
	}

	return true;
}

bool PlayerInfo::UserCmd_ShowInfoSelf(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{

	if (!playerInfoData[iClientID].initialized)
	{
		InitializePlayerInfo(iClientID);
	}
	
	HkChangeIDSString(iClientID, SETINFO_START_INFOCARD, L"Your Information");
	HkChangeIDSString(iClientID, SETINFO_START_INFOCARD + iClientID, playerInfoData[iClientID].playerInfo);
	FmtStr caption(0, 0);
	caption.begin_mad_lib(SETINFO_START_INFOCARD);
	caption.end_mad_lib();

	FmtStr message(0, 0);
	message.begin_mad_lib(SETINFO_START_INFOCARD + iClientID);
	message.end_mad_lib();

	pub::Player::PopUpDialog(iClientID, caption, message, POPUPDIALOG_BUTTONS_CENTER_OK);
	return true;
}

void PlayerInfo::ClearInfo(uint clientId)
{
	playerInfoData[clientId].initialized = false;
	playerInfoData[clientId].playerInfo = L"";
}

void PlayerInfo::PlayerLaunch(uint clientId)
{
	if (playerInfoData[clientId].initialized)
	{
		return;
	}

	FetchPlayerInfo(clientId);
	InitializePlayerInfo(clientId);

	PropagatePlayerInfo(clientId);
}