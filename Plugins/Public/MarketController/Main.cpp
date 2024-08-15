// BountyScan Plugin by Alex. Just looks up the target's ID (tractor). For convenience on Discovery.
// 
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

#include "Main.h"
#include "headers/zlib.h"

using st6_malloc_t = void* (*)(size_t);
using st6_free_t = void(*)(void*);
IMPORT st6_malloc_t st6_malloc;
IMPORT st6_free_t st6_free;

PLUGIN_RETURNCODE returncode;

DWORD* dsac_update_infocard_cmd = 0;
DWORD dsac_update_infocard_cmd_len = 0;

DWORD* dsac_update_econ_cmd = 0;
DWORD dsac_update_econ_cmd_len = 0;

struct LootData
{
	uint maxDropPlayer = 5000;
	uint maxDropNPC = 5000;
	float dropChanceUnmounted = 1.0f;
	float dropChanceMounted = 0.0f;
};
unordered_map<uint, LootData> lootData;

unordered_map<uint, pair<float, unordered_map<ushort, float>>> colGrpCargoMap;
float playerCargoCapacity[MAX_CLIENT_ID + 1];

unordered_map<uint, unordered_map<uint, float>> cargoVolumeOverrideMap;

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

void LoadGameData()
{
	colGrpCargoMap.clear();

	INI_Reader ini;

	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string currDir = string(szCurDir);
	string scFreelancerIniFile = currDir + R"(\freelancer.ini)";

	string gameDir = currDir.substr(0, currDir.length() - 4);
	gameDir += string(R"(\DATA\)");

	if (!ini.open(scFreelancerIniFile.c_str(), false))
	{
		return;
	}

	vector<string> shipFiles;
	vector<string> equipFiles;

	while (ini.read_header())
	{
		if (!ini.is_header("Data"))
		{
			continue;
		}
		while (ini.read_value())
		{
			if (ini.is_value("ships"))
			{
				shipFiles.emplace_back(ini.get_value_string());
			}
			else if (ini.is_value("equipment"))
			{
				equipFiles.emplace_back(ini.get_value_string());
			}
		}
	}

	ini.close();

	for (string& shipFile : shipFiles)
	{
		shipFile = gameDir + shipFile;
		if (!ini.open(shipFile.c_str(), false))
		{
			continue;
		}

		uint currNickname = 0;
		ushort currSID = 3;
		float totalColGrpCapacity = 0.0f;
		while (ini.read_header())
		{
			if (ini.is_header("Ship"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						currNickname = CreateID(ini.get_value_string());
						currSID = 3;
						totalColGrpCapacity = 0.0f;
						break;
					}
				}
			}
			else if (ini.is_header("CollisionGroup"))
			{
				currSID++;
				while (ini.read_value())
				{
					if (ini.is_value("cargo_capacity"))
					{
						totalColGrpCapacity += ini.get_value_float(0);
						colGrpCargoMap[currNickname].first = totalColGrpCapacity;
						colGrpCargoMap[currNickname].second[currSID] = ini.get_value_float(0);
						break;
					}
				}
			}
		}
		ini.close();
	}

	for (string& equipFile : equipFiles)
	{
		equipFile = gameDir + equipFile;

		if (!ini.open(equipFile.c_str(), false))
		{
			continue;
		}

		uint currNickname = 0;

		while (ini.read_header())
		{
			LootData ld;
			bool hasValue = false;
			while (ini.read_value())
			{
				if (ini.is_value("nickname"))
				{
					currNickname = CreateID(ini.get_value_string());
				}
				else if (ini.is_value("max_drop_npc"))
				{
					ld.maxDropNPC = ini.get_value_int(0);
					hasValue = true;
				}
				else if (ini.is_value("max_drop_player"))
				{
					ld.maxDropPlayer = ini.get_value_int(0);
					hasValue = true;
				}
				else if (ini.is_value("drop_chance_npc_unmounted"))
				{
					ld.dropChanceUnmounted = ini.get_value_float(0);
					hasValue = true;
				}
				else if (ini.is_value("drop_chance_npc_mounted"))
				{
					ld.dropChanceMounted = ini.get_value_float(0);
					hasValue = true;
				}
				else if (ini.is_value("volume_class_override"))
				{
					cargoVolumeOverrideMap[ini.get_value_int(0)][currNickname] = ini.get_value_float(1);
				}
			}
			if (hasValue)
			{
				lootData[currNickname] = ld;
			}
		}
		ini.close();
	}
}

void LoadMarketGoodsIni(const string& scPath, map<uint, market_map_t >& mapBaseMarket)
{
	INI_Reader ini;
	if (!ini.open(scPath.c_str(), false))
	{
		return;
	}
	while (ini.read_header())
	{
		if (!ini.is_header("BaseGood"))
		{
			continue;
		}
		if (!ini.read_value() || !ini.is_value("base"))
		{
			continue;
		}
		market_map_t mapMarket;
		uint baseId = CreateID(ini.get_value_string());
		while (ini.read_value())
		{
			if (!ini.is_value("MarketGood"))
			{
				continue;
			}
			MarketGoodInfo mgi;
			mgi.iGoodID = CreateID(ini.get_value_string(0));
			mgi.iMin = ini.get_value_int(3);
			mgi.iQuantity = ini.get_value_int(4);
			mgi.iTransType = (TransactionType)ini.get_value_int(5);
			mgi.fRep = 0.0f;
			mgi.fRank = 0.0f;
			const GoodInfo* gi = GoodList::find_by_id(mgi.iGoodID);
			mgi.fPrice = (gi) ? gi->fPrice * ini.get_value_float(6) : 0.0f;
			mapMarket.insert(market_map_t::value_type(mgi.iGoodID, mgi));
		}
		mapBaseMarket.insert(map<uint, market_map_t >::value_type(baseId, mapMarket));
	}
	ini.close();
}

/// Return a infocard update packet with the size of the packet in the cmdsize parameter.
DWORD* BuildDSACEconUpdateCmd(DWORD* cmdsize, bool bReset, map<uint, market_map_t>& mapMarket)
{
	// Calculate the size of the data to compress and allocate a buffer
	DWORD number_of_updates = 0;
	DWORD srclen = 12; // header
	for (map<uint, market_map_t>::iterator i = mapMarket.begin(); i != mapMarket.end(); ++i)
	{
		srclen += i->second.size() * 20;
		number_of_updates += i->second.size();
	}
	DWORD* src = (DWORD*)new byte[srclen];

	// Build the infocard packet contents
	uint pos = 0;
	src[pos++] = bReset; // reset econ
	src[pos++] = number_of_updates; // number of updates;
	for (map<uint, market_map_t>::const_iterator iterBase = mapMarket.begin();
		iterBase != mapMarket.end(); ++iterBase)
	{
		uint baseId = iterBase->first;
		for (market_map_t::const_iterator iterMarketDelta = iterBase->second.begin();
			iterMarketDelta != iterBase->second.end(); iterMarketDelta++)
		{
			const MarketGoodInfo& mgi = iterMarketDelta->second;
			src[pos++] = baseId;
			src[pos++] = mgi.iGoodID;
			*((float*)&(src[pos++])) = mgi.fPrice;
			src[pos++] = mgi.iMin; // sell price
			src[pos++] = mgi.iTransType;
		}
	}

	// Compress it into a buffer with sufficient room for the packet header
	uint destlen = compressBound(srclen);
	DWORD* dest = (DWORD*)new byte[16 + destlen];
	compress((Bytef*)(&dest[4]), (uLongf*)&destlen, (const Bytef*)src, (uLong)srclen);

	// Add the packet header.
	dest[0] = 0xD5AC;
	dest[1] = 0x02;
	dest[2] = destlen;
	dest[3] = srclen;

	// Clean up and save the buffered infocards.
	delete src;
	*cmdsize = 16 + destlen;
	return dest;
}

void SendD5ACCmd(uint client, DWORD* pData, uint iSize)
{
	HkFMsgSendChat(client, (char*)pData, iSize);
}

void LoadMarketOverrides(map<uint, market_map_t>* eventMarketData)
{

	map<uint, market_map_t> mapBaseMarketDelta;

	// Load the default prices for goods on sale at each base.
	map<uint, market_map_t > mapOldBaseMarket;
	LoadMarketGoodsIni("..\\data\\equipment\\market_commodities.ini", mapOldBaseMarket);
	LoadMarketGoodsIni("..\\data\\equipment\\market_misc.ini", mapOldBaseMarket);

	// Read the prices.ini and add all goods in [Price] section in this file to the market.
	// Before this remove all goods in the [NoSale] section from the market.
	INI_Reader ini;
	if (ini.open("flhook_plugins\\prices.cfg", false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("NoSale"))
			{
				while (ini.read_value())
				{
					uint iGoodID = CreateID(ini.get_name_ptr());

					// For each base that sells this good and an item to the new market map to
					// set this item to buy only (buy only by the dealer).
					for (map<uint, market_map_t >::const_iterator iterOldBaseMarket = mapOldBaseMarket.begin();
						iterOldBaseMarket != mapOldBaseMarket.end(); iterOldBaseMarket++)
					{
						const uint& baseId = iterOldBaseMarket->first;
						const market_map_t& mapOldMarket = iterOldBaseMarket->second;
						if (mapOldMarket.find(iGoodID) != mapOldMarket.end())
						{
							const GoodInfo* gi = GoodList::find_by_id(iGoodID);
							if (gi && gi->iType == GOODINFO_TYPE_COMMODITY)
							{
								auto& mapEntry = mapBaseMarketDelta[baseId][iGoodID];
								mapEntry.iGoodID = iGoodID;
								mapEntry.fPrice = gi->fPrice;
								mapEntry.iTransType = TransactionType_Buy;
							}
						}
					}
				}
			}
			else if (ini.is_header("Price"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("MarketGood"))
					{
						uint baseId = CreateID(ini.get_value_string(0));
						uint iGoodID = CreateID(ini.get_value_string(1));
						uint iSellPrice = ini.get_value_int(2);
						float fPrice = ini.get_value_float(3);
						bool bBaseBuys = (ini.get_value_int(4) == 1);

						if (Universe::get_base(baseId) == nullptr)
						{
							ConPrint(L"ERROR: prices.cfg: base %s doesn't exist!\n", stows(ini.get_value_string(0)).c_str());
							continue;
						}
						if (GoodList_get()->find_by_id(iGoodID) == nullptr)
						{
							ConPrint(L"ERROR: prices.cfg: good %s doesn't exist!\n", stows(ini.get_value_string(1)).c_str());
							continue;
						}

						auto& mapEntry = mapBaseMarketDelta[baseId][iGoodID];
						mapEntry.iGoodID = iGoodID;
						mapEntry.fPrice = fPrice;
						mapEntry.iMin = iSellPrice;
						mapEntry.iTransType = (!bBaseBuys) ? TransactionType_Buy : TransactionType_Sell;
					}
				}
			}
		}
		ini.close();
	}


	BaseDataList* baseDataList = BaseDataList_get();
	static map<uint, market_map_t> originalMarketInfo;

	unordered_set<uint> basesAffected;

	//restore
	for (auto& bi : originalMarketInfo)
	{
		basesAffected.insert(bi.first);
		auto baseData = baseDataList->get_base_data(bi.first);
		for (auto& mi : bi.second)
		{
			auto& info = mi.second;
			const GoodInfo* gi = GoodList::find_by_id(info.iGoodID);
			if (!gi)
			{
				ConPrint(L"marketcontroller: %u not found for %u\n", mi.first, bi.first);
				continue;
			}
			baseData->set_market_good(info.iGoodID, info.iMin, info.iStock, info.iTransType, info.fPrice / gi->fPrice, info.fRank, info.fRep);
		}
	}
	originalMarketInfo.clear();
	 
	// Reset the commodities and load the price changes.
	if (eventMarketData)
	{
		for (auto& baseDataNode : *eventMarketData)
		{
			for (auto& marketGoodEntry : baseDataNode.second)
			{
				mapBaseMarketDelta[baseDataNode.first][marketGoodEntry.first] = marketGoodEntry.second;
			}
		}
	}

	for (auto iterBase : mapBaseMarketDelta)
	{
		basesAffected.insert(iterBase.first);
		BaseData* baseData = baseDataList->get_base_data(iterBase.first);
		for (market_map_t::const_iterator iterMarketDelta = iterBase.second.begin();
			iterMarketDelta != iterBase.second.end();
			iterMarketDelta++)
		{
			const MarketGoodInfo& mgi = iterMarketDelta->second;
			const GoodInfo* gi = GoodList::find_by_id(mgi.iGoodID);
			if (!gi)
			{
				continue;
			}
			// The multiplier is the new price / old good (base) price
			float fMultiplier = mgi.fPrice / gi->fPrice;

			auto marketDataIter = baseData->market_map.find(iterMarketDelta->first);
			if (marketDataIter != baseData->market_map.end())
			{
				auto& origMarketNode = originalMarketInfo[iterBase.first];
				origMarketNode[iterMarketDelta->first] = marketDataIter->second;
			}
			else
			{
				MarketGoodInfo marketinfo;

				marketinfo.iGoodID = iterMarketDelta->first;
				marketinfo.iMin = static_cast<int>(gi->fPrice);
				marketinfo.iStock = 0;
				marketinfo.iTransType = TransactionType(1);
				marketinfo.fPrice = gi->fPrice;
				marketinfo.fRank = 0;
				marketinfo.fRep = -1;
				originalMarketInfo[iterBase.first][iterMarketDelta->first] = marketinfo;
			}
			baseData->set_market_good(mgi.iGoodID, mgi.iMin, !mgi.iTransType, (TransactionType)mgi.iTransType, fMultiplier, 0.0f, -1.0f);
		}
	}

	// Build dsace command for version 6 dsace clients
	if (dsac_update_econ_cmd)
		delete dsac_update_econ_cmd;
	dsac_update_econ_cmd = BuildDSACEconUpdateCmd(&dsac_update_econ_cmd_len, 1, mapBaseMarketDelta);

	// For any players in a base, update them.
	struct PlayerData* pPD = 0;
	while (pPD = Players.traverse_active(pPD))
	{
		if (pPD->iBaseID && basesAffected.count(pPD->iBaseID))
		{
			PrintUserCmdText(pPD->iOnlineID, L"Base you are on has just had its economy state updated. You will be kicked to avoid de-sync.");
			HkDelayedKick(pPD->iOnlineID, 3);
		}
		SendD5ACCmd(pPD->iOnlineID, dsac_update_econ_cmd, dsac_update_econ_cmd_len);
	}
}

float __fastcall CShipGetCargoRemainingDetour(CShip* ship)
{
	float usedCargo = 0.0f;
	CEquipTraverser tr(Cargo | InternalEquipment | ExternalEquipment);
	CEquip* equip;
	unordered_map<uint, float>* overrideMap = nullptr;

	auto shipClassOverrideIter = cargoVolumeOverrideMap.find(ship->shiparch()->iShipClass);
	if (shipClassOverrideIter != cargoVolumeOverrideMap.end())
	{
		overrideMap = &shipClassOverrideIter->second;
	}

	while (equip = ship->equip_manager.Traverse(tr))
	{
		if (equip->CEquipType == Cargo)
		{
			float volume = equip->archetype->fVolume;
			if (overrideMap)
			{
				auto cargoVolumeOverride = overrideMap->find(equip->archetype->iArchID);
				if (cargoVolumeOverride != overrideMap->end())
				{
					volume = cargoVolumeOverride->second;
				}
			}

			usedCargo += reinterpret_cast<CECargo*>(equip)->count * volume;
		}
		else
		{
			usedCargo += equip->archetype->fVolume;
		}
	}

	float cargoCapacity = ship->shiparch()->fHoldSize;
	auto shipColGrpData = colGrpCargoMap.find(ship->archetype->iArchID);
	if (shipColGrpData != colGrpCargoMap.end())
	{
		float totalColGrpCapacity = shipColGrpData->second.first;
		cargoCapacity -= totalColGrpCapacity;

		auto& capacityPerColGrp = shipColGrpData->second.second;
		CArchGrpTraverser ctr;
		CArchGroup* colGrp;
		while (colGrp = ship->archGroupManager.Traverse(ctr))
		{
			if (colGrp->hitPts <= 0.0f)
			{
				continue;
			}
			auto colGrpData = capacityPerColGrp.find(colGrp->colGrp->id);
			if (colGrpData != capacityPerColGrp.end())
			{
				cargoCapacity += colGrpData->second;
			}
		}
	}

	float finalCapacity = cargoCapacity - usedCargo;

	return max(0.0f, finalCapacity);
}

int __fastcall CShipGetSpaceForCargoType(CShip* ship, void* edx, Archetype::Equipment* equipArch)
{
	if (equipArch->fVolume <= 0.0f || equipArch->get_class_type() != Archetype::COMMODITY)
	{
		return ship->get_space_for_cargo_type(equipArch);
	}

	float volume = equipArch->fVolume;

	auto iter = cargoVolumeOverrideMap.find(ship->shiparch()->iShipClass);
	if (iter != cargoVolumeOverrideMap.end())
	{
		auto iter2 = iter->second.find(equipArch->iArchID);
		if (iter2 != iter->second.end())
		{
			volume = iter2->second;
		}
	}

	if (volume == 0.0f)
	{
		return INT_MAX;
	}

	float freeSpace = CShipGetCargoRemainingDetour(ship);
	return static_cast<int>(freeSpace / volume);
}

float GetEquipDescListVolume(uint shipClass, EquipDescList& eqList)
{
	auto cargoVolumeOverrideIter = cargoVolumeOverrideMap.find(shipClass);
	if (cargoVolumeOverrideIter == cargoVolumeOverrideMap.end())
	{
		return eqList.get_cargo_space_occupied();
	}

	auto& cargoOverride = cargoVolumeOverrideIter->second;

	float cargoUsed = 0.0f;
	for (auto& eq : eqList.equip)
	{
		auto cargoData = cargoOverride.find(eq.iArchID);
		if (cargoData != cargoOverride.end())
		{
			cargoUsed += eq.iCount * cargoData->second;
		}
		else
		{
			auto archData = Archetype::GetEquipment(eq.iArchID);
			cargoUsed += eq.iCount * archData->fVolume;
		}
	}

	return cargoUsed;
}

bool __fastcall ShipCargoSpaceExceededAntiCheat(PlayerData* pd)
{
	Archetype::Ship* ship = Archetype::GetShip(pd->iShipArchetype);
	if (!ship)
	{
		return false;
	}

	return GetEquipDescListVolume(ship->iShipClass, pd->equipDescList) > ship->fHoldSize;
}

__declspec(naked) void ShipCargoSpaceExceededAntiCheatNaked()
{
	__asm {
		sub ecx, 0x278
		call ShipCargoSpaceExceededAntiCheat
		add ecx, 0x278
		ret
	}
}

void Detour(void* pOFunc, void* pHkFunc)
{
	DWORD dwOldProtection = 0; // Create a DWORD for VirtualProtect calls to allow us to write.
	BYTE bPatch[5]; // We need to change 5 bytes and I'm going to use memcpy so this is the simplest way.
	bPatch[0] = 0xE9; // Set the first byte of the byte array to the op code for the JMP instruction.
	VirtualProtect(pOFunc, 5, PAGE_EXECUTE_READWRITE, &dwOldProtection); // Allow us to write to the memory we need to change
	DWORD dwRelativeAddress = (DWORD)pHkFunc - (DWORD)pOFunc - 5; // Calculate the relative JMP address.
	memcpy(&bPatch[1], &dwRelativeAddress, 4); // Copy the relative address to the byte array.
	memcpy(pOFunc, bPatch, 5); // Change the first 5 bytes to the JMP instruction.
	VirtualProtect(pOFunc, 5, dwOldProtection, 0); // Set the protection back to what it was.
}

void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	AlleyMF::LoadSettings();
	LoadGameData();

	HANDLE commonHandle = GetModuleHandleA("Common");
	HANDLE serverHandle = GetModuleHandleA("Server");

	Detour((char*)commonHandle + 0x53040, CShipGetCargoRemainingDetour);
	void* funcAddr = (void*)CShipGetSpaceForCargoType;
	WriteProcMem((char*)serverHandle + 0x84654, &funcAddr, 4);

	Detour((char*)commonHandle + 0xAA8E0, (char*)ShipCargoSpaceExceededAntiCheatNaked);

	LoadMarketOverrides(nullptr);
}

void __stdcall Login(struct SLoginInfo const& li, uint client)
{
	returncode = DEFAULT_RETURNCODE;
	SendD5ACCmd(client, dsac_update_econ_cmd, dsac_update_econ_cmd_len);
}

void __stdcall GFGoodSell(struct SGFGoodSellInfo const& gsi, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;

	AlleyMF::GFGoodSell(gsi, client);

	const GoodInfo* gi = GoodList_get()->find_by_id(gsi.iArchID);
	if (gi->iType != GOODINFO_TYPE_COMMODITY)
	{
		return;
	}

	BaseData* bd = BaseDataList_get()->get_base_data(Players[client].iBaseID);
	auto marketData = bd->market_map.find(gsi.iArchID);
	if (marketData == bd->market_map.end())
	{
		return;
	}
	int sellPrice = marketData->second.iMin;
	int currPrice = static_cast<int>(marketData->second.fPrice);
	pub::Player::AdjustCash(client, gsi.iCount * (sellPrice - currPrice));
}

void __stdcall GFGoodBuy(struct SGFGoodBuyInfo const& gbi, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	if (!CommodityLimit::GFGoodBuy(gbi, client) 
		|| !AlleyMF::GFGoodBuy(gbi, client))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
	}
}

void __stdcall PlayerLaunch(unsigned int iShip, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	AlleyMF::PlayerLaunch(iShip, client);
}

void __stdcall BaseEnter_AFTER(unsigned int baseId, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;
	AlleyMF::BaseEnter_AFTER(baseId, client);
}

const LootData defaults;

void __stdcall ShipDestroyed(IObjRW* ship, bool isKill, uint killerId)
{
	returncode = DEFAULT_RETURNCODE;

	CShip* cship = reinterpret_cast<CShip*>(ship->cobj);
	uint killerClientId = HkGetClientIDByShip(killerId);
	if (killerId && !killerClientId && !cship->ownerPlayer)
	{
		cship->clear_equip_and_cargo();
		return;
	}


	auto goodList = GoodList_get();

	if (!cship->ownerPlayer)
	{
		PlayerData& killerData = Players[killerClientId];
		uint targetAffiliation;
		float attitude;
		Reputation::Vibe::GetAffiliation(cship->repVibe, targetAffiliation, false);
		pub::Reputation::GetGroupFeelingsTowards(killerData.iReputation, targetAffiliation, attitude);

		if (attitude >= 0.0f)
		{
			cship->clear_equip_and_cargo();
			return;
		}
	}
	
	CEquipTraverser tr(Cargo);
	CECargo* cargo = nullptr;
	while (cargo = reinterpret_cast<CECargo*>(cship->equip_manager.Traverse(tr)))
	{
		auto lootIter = lootData.find(cargo->archetype->iArchID);

		const LootData& ld = lootIter == lootData.end() ? defaults : lootIter->second;

		uint amountToDrop;
		if (cship->ownerPlayer)
		{
			amountToDrop = ld.maxDropPlayer;
		}
		else
		{
			amountToDrop = ld.maxDropNPC;
		}

		if (ld.dropChanceUnmounted < 1.0f)
		{
			float roll = static_cast<float>(rand()) / RAND_MAX;
			if (roll > ld.dropChanceUnmounted)
			{
				continue;
			}
		}

		if (!amountToDrop)
		{
			continue;
		}

		Server.MineAsteroid(cship->system, cship->vPos, cargo->archetype->get_loot_appearance()->iArchID,
			cargo->archetype->iArchID, min(cargo->count, amountToDrop), 0);
	}
	
	cship->clear_equip_and_cargo();
}

unordered_map<uint, float> dropMap;

void Timer()
{
	returncode = DEFAULT_RETURNCODE;

	for (auto& item : dropMap)
	{
		uint shipId = item.first;
		IObjInspectImpl* iobj1;
		uint dummy;
		GetShipInspect(shipId, iobj1, dummy);

		if (!iobj1)
		{
			continue;
		}

		IObjRW* iobj = reinterpret_cast<IObjRW*>(iobj1);

		CShip* cship = reinterpret_cast<CShip*>(iobj->cobj);

		if (cship->hitPoints <= 1.0f)
		{
			continue;
		}

		float totalCargoToJettison = item.second;
		CEquipTraverser tr(Cargo);
		CEquipManager& eqManager = cship->equip_manager;

		CECargo* cargo = nullptr;
		while (cargo = reinterpret_cast<CECargo*>(eqManager.Traverse(tr)))
		{
			if (cargo->archetype->fVolume == 0.0f)
			{
				continue;
			}
			bool flag = false;
			pub::IsCommodity(cargo->archetype->iArchID, flag);
			if (!flag)
			{
				continue;
			}

			float amountToJettison = min(cargo->count, ceilf(totalCargoToJettison / cargo->archetype->fVolume));
			iobj->jettison_cargo(cargo->iSubObjId, static_cast<ushort>(amountToJettison), cship->ownerPlayer);

			totalCargoToJettison -= amountToJettison * cargo->archetype->fVolume;
			if (totalCargoToJettison <= 0.0f)
			{
				break;
			}
		}
	}

	dropMap.clear();
}

void ShipColGrpDestroyed(IObjRW* iobj, CArchGroup* colGrp, DamageEntry::SubObjFate fate, DamageList* dmgList)
{
	returncode = DEFAULT_RETURNCODE;

	CShip* cship = reinterpret_cast<CShip*>(iobj->cobj);
	const auto shipColGrpInfo = colGrpCargoMap.find(cship->archetype->iArchID);
	if (shipColGrpInfo == colGrpCargoMap.end())
	{
		return;
	}

	const auto colGrpInfo = shipColGrpInfo->second.second.find(colGrp->colGrp->id);
	if (colGrpInfo == shipColGrpInfo->second.second.end())
	{
		return;
	}

	uint id = iobj->get_id();
	dropMap[id] += dropMap[id] + colGrpInfo->second;
}

void Plugin_Communication_CallBack(PLUGIN_MESSAGE msg, void* data)
{
	returncode = DEFAULT_RETURNCODE;
	if (msg == CUSTOM_EVENT_ECON_UPDATE)
	{
		returncode = SKIPPLUGINS;
		map<uint, market_map_t>* info = reinterpret_cast<map<uint, market_map_t>*>(data);
		LoadMarketOverrides(info);
	}
}

#define IS_CMD(a) !args.compare(L##a)
#define RIGHT_CHECK(a) if(!(cmd->rights & a)) { cmd->Print(L"ERR No permission\n"); return true; }
bool ExecuteCommandString_Callback(CCmds* cmd, const wstring& args)
{
	returncode = DEFAULT_RETURNCODE;

	if (IS_CMD("pricereload"))
	{
		LoadSettings();
		cmd->Print(L"Price data reloaded\n");
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		return true;
	}

	return false;
}

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "MarketController by Aingar";
	p_PI->sShortName = "MarketController";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 1));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Login, PLUGIN_HkIServerImpl_Login, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&GFGoodSell, PLUGIN_HkIServerImpl_GFGoodSell, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&GFGoodBuy, PLUGIN_HkIServerImpl_GFGoodBuy, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch, PLUGIN_HkIServerImpl_PlayerLaunch, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseEnter_AFTER, PLUGIN_HkIServerImpl_BaseEnter_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipColGrpDestroyed, PLUGIN_ShipColGrpDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Timer, PLUGIN_HkTimerCheckKick, 0));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CommodityLimit::ClearClientInfo, PLUGIN_ClearClientInfo, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CommodityLimit::ReqAddItem, PLUGIN_HkIServerImpl_ReqAddItem, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CommodityLimit::ReqChangeCash, PLUGIN_HkIServerImpl_ReqChangeCash, 0));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipDestroyed, PLUGIN_ShipDestroyed, 0));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Plugin_Communication_CallBack, PLUGIN_Plugin_Communication, 0));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));

	return p_PI;
}
