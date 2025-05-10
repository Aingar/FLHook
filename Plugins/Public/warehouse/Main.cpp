// Warehouse Plugin by Aingar. Lets you store items on designated bases for pickup.
// 
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

#include <windows.h>
#include <list>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>

PLUGIN_RETURNCODE returncode;

struct ItemData
{
	uint archId;
	uint count;
};

struct DepositLocker
{
	wstring password;
	int creditBalance;
	vector<ItemData> itemData;
};

unordered_map<uint, unordered_map<wstring, DepositLocker>> itemDatabase;
unordered_map<uint, DepositLocker*> clientLogins;
unordered_set<uint> bannedSystems;
unordered_set<uint> bannedBases;

int depositCost = 0;
int withdrawCost = 0;


bool warehouseUpdate = false;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		HkLoadStringDLLs();
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
		HkUnloadStringDLLs();
	}
	return true;
}

EXPORT PLUGIN_RETURNCODE Get_PluginReturnCode()
{
	return returncode;
}

bool CheckBaseStatus(uint client)
{
	if (!Players[client].iBaseID)
	{
		PrintUserCmdText(client, L"ERR Not on a base");
		return true;
	}
	if (bannedSystems.count(Players[client].iSystemID))
	{
		PrintUserCmdText(client, L"ERR Warehouse storage not available in this system");
		return true;
	}
	if (bannedBases.count(Players[client].iBaseID))
	{
		PrintUserCmdText(client, L"ERR Warehouse storage not available on this base");
		return true;
	}
	CUSTOM_BASE_IS_DOCKED_STRUCT data;
	data.iClientID = client;
	data.iDockedBaseID = 0;
	Plugin_Communication(CUSTOM_BASE_IS_DOCKED, &data);
	if (data.iDockedBaseID)
	{
		PrintUserCmdText(client, L"ERR Warehouse storage not available on POBs");
		return true;
	}
	return false;
}

bool UserCmd_Login(uint client, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	if(CheckBaseStatus(client))
	{
		return true;
	}

	wstring lockerName = GetParam(wscParam, L' ', 0);
	wstring lockerPassword = GetParam(wscParam, L' ', 1);

	if (lockerName.length() < 5)
	{
		PrintUserCmdText(client, L"ERR Locker name too short, at least 5 characters!");
		return true;
	}

	if (lockerPassword.length() < 5)
	{
		PrintUserCmdText(client, L"ERR Locker password too short, at least 5 characters!");
		return true;
	}

	uint baseId = Players[client].iBaseID;
	auto iter1 = itemDatabase.find(baseId);
	if (iter1 == itemDatabase.end())
	{
		PrintUserCmdText(client, L"ERR Invalid locker name or password");
		return true;
	}
	
	auto iter2 = iter1->second.find(lockerPassword);
	if(iter2 == iter1->second.end() || iter2->second.password != lockerPassword)
	{
		PrintUserCmdText(client, L"ERR Invalid locker name or password");
		return true;
	}

	clientLogins[client] = &iter2->second;
	return true;
}

bool UserCmd_Register(uint client, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	if (CheckBaseStatus(client))
	{
		return true;
	}

	wstring lockerName = GetParam(wscParam, L' ', 0);
	if (lockerName.length() < 5)
	{
		PrintUserCmdText(client, L"ERR Locker name too short, at least 5 characters!");
		return true;
	}

	wstring lockerPassword = GetParam(wscParam, L' ', 0);
	if (lockerPassword.length() < 5)
	{
		PrintUserCmdText(client, L"ERR Locker password too short, at least 5 characters!");
		return true;
	}

	uint baseId = Players[client].iBaseID;
	auto iter1 = itemDatabase.find(baseId);
	if (iter1 != itemDatabase.end() && iter1->second.count(lockerName))
	{
		PrintUserCmdText(client, L"ERR This locker name is already taken on this base!");
		return true;
	}

	auto base = Universe::get_base(baseId);
	PrintUserCmdText(client, L"ERR Locker '%s' registered on the base '%s'", lockerName.c_str(), HkGetWStringFromIDS(base->iBaseIDS).c_str());
	itemDatabase[baseId][lockerName] = { lockerPassword, 0, {} };
	return true;
}

bool UserCmd_Deposit(uint client, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	auto data = clientLogins.find(client);
	if (data == clientLogins.end())
	{
		PrintUserCmdText(client, L"ERR Not logged into a storage locker");
	}

	auto itemId = (ushort)ToUInt(GetParam(wscParam, L' ', 0));
	if (!itemId)
	{
		PrintUserCmdText(client, L"ERR Invalid Item ID!");
		return true;
	}
	auto depositCount = ToUInt(GetParam(wscParam, L' ', 1));
	if (!depositCount)
	{
		PrintUserCmdText(client, L"ERR Invalid Item count!");
		return true;
	}
	EquipDesc* equip = nullptr;
	for (auto& eq : Players[client].equipDescList.equip)
	{
		if (eq.sID != itemId)
		{
			continue;
		}

		if (eq.bMounted)
		{
			PrintUserCmdText(client, L"ERR Cannot deposit a mounted item!");
			return true;
		}
		equip = &eq;
		break;
	}

	if (!equip)
	{
		PrintUserCmdText(client, L"ERR Invalid Item ID!");
		return true;
	}

	if (equip->iCount < depositCount)
	{
		PrintUserCmdText(client, L"ERR Attempting to deposit more cargo than you have");
		return true;
	}

	if (depositCost)
	{
		if (Players[client].iInspectCash < depositCost)
		{
			PrintUserCmdText(client, L"ERR Not enough money to cover deposit fee (%u)", depositCost);
			return true;
		}
		pub::Player::AdjustCash(client, -depositCost);
	}

	auto eqArch = Archetype::GetEquipment(equip->iArchID);
	PrintUserCmdText(client, L"Deposited %s x%u", HkGetWStringFromIDS(eqArch->iIdsName).c_str(), depositCount);
	auto& itemData = data->second;
	pub::Player::RemoveCargo(client, equip->sID, depositCount);
	
	warehouseUpdate = true;

	for (auto& item : itemData->itemData)
	{
		if (item.archId == equip->iArchID)
		{
			item.count += depositCount;
			return true;
		}
	}

	itemData->itemData.push_back({ equip->iArchID, depositCount });

	return true;
}

bool UserCmd_Withdraw(uint client, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	auto data = clientLogins.find(client);
	if(data == clientLogins.end())
	{
		PrintUserCmdText(client, L"ERR Not logged into a storage locker");
		return true;
	}

	auto itemNr = ToUInt(GetParam(wscParam, L' ', 0));
	if (!itemNr || data->second->itemData.size() >= itemNr)
	{
		PrintUserCmdText(client, L"ERR invalid item number");
		return true;
	}

	auto& itemData = data->second->itemData.at(++itemNr);
	auto withdrawCount = ToUInt(GetParam(wscParam, L' ', 1));
	if (!withdrawCount)
	{
		PrintUserCmdText(client, L"ERR Invalid withdraw count");
		return true;
	}

	auto itemArch = Archetype::GetEquipment(itemData.archId);
	if (itemArch->fVolume)
	{
		EQUIP_VOLUME_STRUCT data;
		data.equipArch = itemData.archId;
		data.shipArch = Players[client].iShipArchetype;
		Plugin_Communication(CUSTOM_CHECK_EQUIP_VOLUME, &data);

		float remainingCargo;
		pub::Player::GetRemainingHoldSize(client, remainingCargo);

		if (remainingCargo < data.volume * withdrawCount)
		{
			PrintUserCmdText(client, L"ERR Insufficient free cargo volume");
			return true;
		}
	}

	if (withdrawCost)
	{
		if (Players[client].iInspectCash < withdrawCost)
		{
			PrintUserCmdText(client, L"ERR Not enough money to cover withdraw fee (%u)", withdrawCost);
			return true;
		}
		pub::Player::AdjustCash(client, -withdrawCost);
	}

	wstring name = (wchar_t*)Players.GetActiveCharacterName(client);
	HkAddCargo(name, itemData.archId, withdrawCount, false);

	warehouseUpdate = true;

	return true;
}
bool UserCmd_List(uint client, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	PrintUserCmdText(client, L"Items available for storage:");
	for (auto& eq : Players[client].equipDescList.equip)
	{
		if (eq.bMounted)
		{
			continue;
		}

		auto item = Archetype::GetEquipment(eq.iArchID);
		PrintUserCmdText(client, L"#%2u - %s x%u", (uint)eq.sID, HkGetWStringFromIDS(item->iIdsName).c_str(), eq.iCount);
	}
	return true;
}

typedef bool(*_UserCmdProc)(uint, const wstring&, const wstring&, const wchar_t*);

struct USERCMD
{
	wchar_t* wszCmd;
	_UserCmdProc proc;
	wchar_t* usage;
};

USERCMD UserCmds[] =
{
	{ L"/wl", UserCmd_Login, L"Usage: /warehouselogin or /wl <login> <password>" },
	{ L"/warehouselogin", UserCmd_Login, L"Usage: /warehouselogin or /wl <login> <password>" },
	{ L"/wlist", UserCmd_List, L"Usage: /wlist or /warehouselist" },
	{ L"/warehouselist", UserCmd_List, L"Usage: /wlist or /warehouselist" },
	{ L"/deposit", UserCmd_Deposit, L"Usage: /deposit <itemNr> [amount]" },
	{ L"/withdraw", UserCmd_Withdraw, L"Usage: /withdraw <itemNr> [amount]" },
	{ L"/warehouseregister", UserCmd_Register, L"Usage: /wregister <login> <password>" },
	{ L"/wregister", UserCmd_Register, L"Usage: /wregister <login> <password>" },
};

bool UserCmd_Process(uint iClientID, const wstring& wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	wstring wscCmdLineLower = ToLower(wscCmd);

	for (uint i = 0; (i < sizeof(UserCmds) / sizeof(USERCMD)); i++)
	{

		if (wscCmdLineLower.find(UserCmds[i].wszCmd) == 0)
		{
			wstring wscParam = L"";
			if (wscCmd.length() > wcslen(UserCmds[i].wszCmd))
			{
				if (wscCmd[wcslen(UserCmds[i].wszCmd)] != ' ')
					continue;
				wscParam = wscCmd.substr(wcslen(UserCmds[i].wszCmd) + 1);
			}

			if (UserCmds[i].proc(iClientID, wscCmd, wscParam, UserCmds[i].usage))
			{
				returncode = SKIPPLUGINS_NOFUNCTIONCALL;
				return true;
			}
		}
	}
	return false;
}

void __stdcall CharacterSelect(struct CHARACTER_ID const& cId, uint client)
{
	returncode = DEFAULT_RETURNCODE;
	clientLogins.erase(client);
}

void __stdcall BaseExit(uint baseID, uint client)
{
	returncode = DEFAULT_RETURNCODE;
	clientLogins.erase(client);
}

void PluginComm(PLUGIN_MESSAGE msg, void* data)
{
	returncode = DEFAULT_RETURNCODE;
}

void LoadWarehouseData()
{
	itemDatabase.clear();

	INI_Reader ini;
	if (!ini.open("\\flhook_plugins\\warehouse.cfg", false))
	{
		return;
	}
	while (ini.read_header())
	{
		if (!ini.is_header("settings"))
		{
			continue;
		}

		while (ini.read_value())
		{
			if (ini.is_value("bannedSystem"))
			{
				bannedSystems.insert(CreateID(ini.get_value_string(0)));
			}
			else if (ini.is_value("bannedBase"))
			{
				bannedBases.insert(CreateID(ini.get_value_string(0)));
			}
			else if (ini.is_value("depositCost"))
			{
				depositCost = ini.get_value_int(0);
			}
			else if (ini.is_value("withdrawCost"))
			{
				withdrawCost = ini.get_value_int(0);
			}
		}
	}
	ini.close();

	// The path to the data file.
	char datapath[MAX_PATH];
	GetUserDataPath(datapath);

	// Create the directory if it doesn't exist
	string moddir = string(datapath) + "\\Accts\\MultiPlayer\\warehouse\\";
	CreateDirectoryA(moddir.c_str(), 0);

	string warehouseFile = moddir + "warehouseData.ini";

	if (!ini.open(warehouseFile.c_str(), false))
	{
		return;
	}
	
	while (ini.read_header())
	{
		if (!ini.is_header("locker"))
		{
			continue;
		}
		DepositLocker dl;
		uint baseId;
		wstring lockerName;
		while (ini.read_value())
		{
			if(ini.is_value("base"))
			{
				baseId = ini.get_value_int(0);
			}
			else if (ini.is_value("lockerName"))
			{
				lockerName = stows(ini.get_value_string(0));
			}
			else if (ini.is_value("balance"))
			{
				dl.creditBalance = ini.get_value_int(0);
			}
			else if (ini.is_value("password"))
			{
				dl.password = stows(ini.get_value_string(0));
			}
			else if (ini.is_value("item"))
			{
				dl.itemData.push_back({ (uint)ini.get_value_int(0), (uint)ini.get_value_int(1) });
			}
		}

		itemDatabase[baseId][lockerName] = dl;
	}
	ini.close();
}

void SaveWarehouseData()
{
	// The path to the data file.
	char datapath[MAX_PATH];
	GetUserDataPath(datapath);

	// Create the directory if it doesn't exist
	string moddir = string(datapath) + "\\Accts\\MultiPlayer\\warehouse\\";
	CreateDirectoryA(moddir.c_str(), 0);

	string warehouseFile = moddir + "warehouseData.ini";

	FILE* file = fopen(warehouseFile.c_str(), "w");
	if (!file)
	{
		return;
	}
	
	for (auto& baseData : itemDatabase)
	{
		for (auto& locker : baseData.second)
		{
			if (locker.second.creditBalance == 0 && locker.second.itemData.empty())
			{
				continue;
			}
			fprintf(file, "[locker]\n");
			fprintf(file, "base = %u\n", baseData.first);
			fprintf(file, "lockerName = %s\n", wstos(locker.first).c_str());
			fprintf(file, "balance = %u\n", locker.second.creditBalance);
			fprintf(file, "password = %s\n", wstos(locker.second.password).c_str());
			for (auto& item : locker.second.itemData)
			{
				fprintf(file, "item = %u, %u\n", item.archId, item.count);
			}
		}
	}

	fclose(file);
}

void Timer()
{
	returncode = DEFAULT_RETURNCODE;
	if (warehouseUpdate 
		//&& (time(0) % 300) == 0
		)
	{
		warehouseUpdate = false;
		SaveWarehouseData();
	}
}

void ServerCrash()
{
	returncode = DEFAULT_RETURNCODE;
	SaveWarehouseData();
}

void Shutdown()
{
	returncode = DEFAULT_RETURNCODE;
	SaveWarehouseData();
}

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Warehouse by Aingar";
	p_PI->sShortName = "warehouse";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PluginComm, PLUGIN_Plugin_Communication, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Shutdown, PLUGIN_HkIServerImpl_Shutdown, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ServerCrash, PLUGIN_ServerCrash, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseExit, PLUGIN_HkIServerImpl_BaseExit, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect, PLUGIN_HkIServerImpl_CharacterSelect, 0));
	

	return p_PI;
}
