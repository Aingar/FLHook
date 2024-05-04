// Player Control plugin for FLHookPlugin
// Feb 2010 by Cannon
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include <sstream>

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

wstring GetFormatHex(uint newFormat)
{
	wchar_t buf[3];
	swprintf(buf, L"%02x", newFormat);
	return buf;
}

static uint RgbToBgr(const uint color) { return color & 0xFF000000 | (color & 0xFF0000) >> 16 | color & 0x00FF00 | (color & 0x0000FF) << 16; };

wstring GetColorHex(TextColor newColor)
{
	wchar_t buf[7];
	swprintf(buf, L"%06x", RgbToBgr(static_cast<uint>(newColor)));
	return buf;
}

bool SetFormatColor(wstring& currFormat, wstring& currColor, wchar_t input1, wchar_t input2)
{
	uint newFormat = 0;
	bool format = false;
	switch (input1)
	{
	case L'r':
		switch (input2)
		{
		case L'd':
			currColor = GetColorHex(TextColor::SystemBlue);
			break;
		case L'B':
			currColor = GetColorHex(TextColor::Black);
			break;
		case L'W':
			currColor = GetColorHex(TextColor::White);
			break;
		case L'r':
			currColor = GetColorHex(TextColor::Red);
			break;
		case L'g':
			currColor = GetColorHex(TextColor::Green);
			break;
		case L'b':
			currColor = GetColorHex(TextColor::DeepSkyBlue);
			break;
		case L'v':
			currColor = GetColorHex(TextColor::Violet);
			break;
		case L'y':
			currColor = GetColorHex(TextColor::Yellow);
			break;
		case L'o':
			currColor = GetColorHex(TextColor::Orange);
			break;
		case L'G':
			currColor = GetColorHex(TextColor::Gray);
			break;
		default:
			return false;
		}
		break;
	case L'l':
		switch (input2)
		{
		case L'r':
			currColor = GetColorHex(TextColor::LightSalmon);
			break;
		case L'g':
			currColor = GetColorHex(TextColor::LightGreen);
			break;
		case L'b':
			currColor = GetColorHex(TextColor::SystemBlue);
			break;
		case L'v':
			currColor = GetColorHex(TextColor::BlueViolet);
			break;
		case L'y':
			currColor = GetColorHex(TextColor::LightYellow);
			break;
		case L'o':
			currColor = GetColorHex(TextColor::Wheat);
			break;
		case L'G':
			currColor = GetColorHex(TextColor::LightGray);
			break;
		default:
			return false;
		}
		break;
	case L'd':
		switch (input2)
		{
		case L'r':
			currColor = GetColorHex(TextColor::DarkRed);
			break;
		case L'g':
			currColor = GetColorHex(TextColor::DarkGreen);
			break;
		case L'b':
			currColor = GetColorHex(TextColor::Blue);
			break;
		case L'v':
			currColor = GetColorHex(TextColor::DarkViolet);
			break;
		case L'y':
			currColor = GetColorHex(TextColor::DarkGoldenrod);
			break;
		case L'o':
			currColor = GetColorHex(TextColor::DarkOrange);
			break;
		case L'G':
			currColor = GetColorHex(TextColor::DarkGray);
			break;
		default:
			return false;
		}
		break;
	case L'N':
		format = true;
		newFormat = 0x0;
		break;
	case L's':
		format = true;
		newFormat = 0x90;
		break;
	case L'v':
		format = true;
		newFormat = 0x8;
		break;
	case L'w':
		format = true;
		newFormat = 0x10;
		break;
	default:
		return false;
		break;
	}

	if (format)
	{
		switch (input2)
		{
		case L'b':
			currFormat = GetFormatHex(newFormat | 0x1);
			break;
		case L'i':
			currFormat = GetFormatHex(newFormat | 0x2);
			break;
		case L'u':
			currFormat = GetFormatHex(newFormat | 0x4);
			break;
		case L'n':
			currFormat = GetFormatHex(newFormat);
			break;
		default:
			return false;
		}
	}
	return true;
}

wstring FormatString(wstring& text)
{
	wstringstream newString;
	wstring currFormat = L"00";
	wstring currColor = GetColorHex(TextColor::Default);
	for (int i = 0; i < text.size(); ++i)
	{
		wchar_t currChar = text.at(i);
		if (currChar == L'<')
		{
			newString << L"&#60;";
			continue;
		}
		if (currChar == L'>')
		{
			newString << L"&#62;";
			continue;
		}
		if (currChar == L'&')
		{
			newString << L"&#38;";
			continue;
		}
		else if (currChar != L'%')
		{
			newString << currChar;
			continue;
		}
		++i;
		if (i == text.size())
		{
			break;
		}
		currChar = text.at(i);
		if (currChar == L'%')
		{
			newString << L'%';
			continue;
		}
		else if (currChar == L'n')
		{
			newString << L"</TEXT><PARA/><TEXT>";
			continue;
		}
		++i;
		if (i == text.size())
		{
			break;
		}
		wchar_t secondChar = text.at(i);

		if (!SetFormatColor(currFormat, currColor, currChar, secondChar))
		{
			continue;
		}


		newString << L"</TEXT><TRA data=\"0x" << currColor <<
			currFormat << "\" mask=\"-1\"/><TEXT>";

	}

	return newString.str();
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
			playerInfo += FormatString(playerInfoLine);
			playerInfo += L"</TEXT><PARA/><PARA/>";
		}
	}
	playerInfo += L"<POP/></RDL>";

	playerInfoData[clientId].playerInfo = playerInfo;
}

void RecalculateInfoText(uint clientId)
{
	wstring& playerInfo = playerInfoData[clientId].playerInfo;

	playerInfo = L"<RDL><PUSH/>";
	for (wstring& info : playerInfoData[clientId].infoVector)
	{
		playerInfo += L"<TEXT>";
		playerInfo += FormatString(info);
		playerInfo += L"</TEXT><PARA/><PARA/>";
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
	wstring &wscMsg = GetParamToEnd(wscParam, ' ', 2);

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
		else if (iPara == infoVec.size())
		{
			infoVec.emplace_back(L"");
		}
		
		if (!infoVec.empty() && (wscMsg.size() + infoVec[iPara].length()) > MAX_CHARACTERS)
		{
			PrintUserCmdText(iClientID, L"ERR Text will be too long!(including formatting) Current lenght: %u, Max Lenght: %u", playerInfoData[iClientID].playerInfo.length(), MAX_CHARACTERS);
			return false;
		}
		
		infoVec[iPara] += wscMsg;
		
		RecalculateInfoText(iClientID);
		WriteInfoFile(iClientID, scFilePath);
		PropagatePlayerInfo(iClientID);
		PrintUserCmdText(iClientID, L"OK");
	}
	else if (wscCommand == L"d")
	{
		if (infoVec.size() <= iPara)
		{
			PrintUserCmdText(iClientID, L"ERR Incorrect paragraph!");
			return false;
		}
		infoVec.erase(infoVec.begin() + iPara);
		RecalculateInfoText(iClientID);
		WriteInfoFile(iClientID, scFilePath);
		PropagatePlayerInfo(iClientID);
		PrintUserCmdText(iClientID, L"OK");
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

	if (wscParam == L"me")
	{

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

	auto cship = ClientInfo[iClientID].cship;
	if (!cship)
	{
		PrintUserCmdText(iClientID, L"ERR Not in space");
		return true;
	}

	auto targetShip = cship->get_target();
	if (!targetShip || targetShip->cobj->objectClass != CObject::CSHIP_OBJECT)
	{
		PrintUserCmdText(iClientID, L"ERR No player target");
		return true;
	}
	auto targetCShip = reinterpret_cast<CShip*>(targetShip->cobj);
	if (!targetCShip->ownerPlayer)
	{
		PrintUserCmdText(iClientID, L"ERR No player target");
		return true;
	}

	HkChangeIDSString(iClientID, SETINFO_START_INFOCARD, L"Target Information");
	FmtStr caption(0, 0);
	caption.begin_mad_lib(SETINFO_START_INFOCARD);
	caption.end_mad_lib();

	FmtStr message(0, 0);
	message.begin_mad_lib(SETINFO_START_INFOCARD + targetCShip->ownerPlayer);
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