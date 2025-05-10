// BountyScan Plugin by Alex. Just looks up the target's ID (tractor). For convenience on Discovery.
// 
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

#include "Main.h"
#include "headers/zlib.h"

PLUGIN_RETURNCODE returncode;

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

unordered_map<uint, float> unstableJumpObjMap;

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
	unstableJumpObjMap.clear();

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
	vector<string> solarFiles;

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
			else if (ini.is_value("solar"))
			{
				solarFiles.emplace_back(ini.get_value_string());
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

	for (string& solarFile : solarFiles)
	{
		solarFile = gameDir + solarFile;

		if (!ini.open(solarFile.c_str(), false))
		{
			continue;
		}

		uint currNickname = 0;

		while (ini.read_header())
		{
			if (!ini.is_header("Solar"))
			{
				continue;
			}
			while (ini.read_value())
			{
				if (ini.is_value("nickname"))
				{
					currNickname = CreateID(ini.get_value_string());
				}
				else if (ini.is_value("cargo_limit"))
				{
					unstableJumpObjMap[currNickname] = ini.get_value_float(0);
					break;
				}
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

						if (iSellPrice <= 0 || fPrice <= 0)
						{
							ConPrint(L"ERROR: Can't set price lower or equal 0\n");
						}

						if (iSellPrice > fPrice)
						{
							ConPrint(L"ERROR: Infinite money printer, buy for %u, sell for %u\n", static_cast<uint>(fPrice), iSellPrice);
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

uint GetSpaceForCargoTypeRetAddr = 0x62B32E8;
__declspec(naked) int __fastcall CallOriginalGetSpaceForCargoType(CShip* ship, void* edx, Archetype::Equipment*)
{
	__asm
	{
		sub esp, 0x1C
		push ebx
		mov ebx, [esp+0x24]
		jmp GetSpaceForCargoTypeRetAddr
	}
}

float GetVolumeForShipArch(uint shipArch, uint equipArch)
{
	auto equip = Archetype::GetEquipment(equipArch);
	if (!equip)
	{
		return 0;
	}

	auto ship = Archetype::GetShip(shipArch);
	float volume = equip->fVolume;

	auto iter = cargoVolumeOverrideMap.find(ship->iShipClass);
	if (iter != cargoVolumeOverrideMap.end())
	{
		auto iter2 = iter->second.find(equip->iArchID);
		if (iter2 != iter->second.end())
		{
			volume = iter2->second;
		}
	}

	return volume;
}

int __fastcall CShipGetSpaceForCargoType(CShip* ship, void* edx, Archetype::Equipment* equipArch)
{
	if (equipArch->fVolume <= 0.0f || equipArch->get_class_type() != Archetype::COMMODITY)
	{
		return CallOriginalGetSpaceForCargoType(ship, edx, equipArch);
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

float __fastcall EquipDescListVolume(EquipDescList& eq)
{
	PlayerData* pd = reinterpret_cast<PlayerData*>(DWORD(&eq) - 0x278);
	Archetype::Ship* ship = Archetype::GetShip(pd->iShipArchetype);
	if (!ship)
	{
		return 0.0f;
	}

	auto cargoVolumeOverrideIter = cargoVolumeOverrideMap.find(ship->iShipClass);
	unordered_map<uint, float>* cargoOverrideMap = nullptr;
	if (cargoVolumeOverrideIter != cargoVolumeOverrideMap.end())
	{
		cargoOverrideMap = &cargoVolumeOverrideIter->second;
	}

	float cargoUsed = 0.0f;
	for (auto& eq : eq.equip)
	{
		if (cargoOverrideMap)
		{
			auto cargoData = cargoOverrideMap->find(eq.iArchID);
			if (cargoData != cargoOverrideMap->end())
			{
				cargoUsed += eq.iCount * cargoData->second;
				continue;
			}
		}
		auto archData = Archetype::GetEquipment(eq.iArchID);
		cargoUsed += eq.iCount * archData->fVolume;
		
	}

	return cargoUsed;
}

float EquipDescCommodityVolume(uint clientId, uint shipArch)
{
	Archetype::Ship* ship = Archetype::GetShip(shipArch);
	if (!ship)
	{
		return 0.0f;
	}

	auto cargoVolumeOverrideIter = cargoVolumeOverrideMap.find(ship->iShipClass);
	unordered_map<uint, float>* cargoOverrideMap = nullptr;
	if (cargoVolumeOverrideIter != cargoVolumeOverrideMap.end())
	{
		cargoOverrideMap = &cargoVolumeOverrideIter->second;
	}

	float cargoUsed = 0.0f;
	for (auto& eq : Players[clientId].equipDescList.equip)
	{
		if (eq.bMounted)
		{
			continue;
		}
		if (cargoOverrideMap)
		{
			auto cargoData = cargoOverrideMap->find(eq.iArchID);
			if (cargoData != cargoOverrideMap->end())
			{
				cargoUsed += eq.iCount * cargoData->second;
				continue;
			}
		}
		auto archData = Archetype::GetEquipment(eq.iArchID);
		cargoUsed += eq.iCount * archData->fVolume;
		
	}

	return cargoUsed;
}

float EquipDescCommodityVolume(uint clientId)
{
	return EquipDescCommodityVolume(clientId, Players[clientId].iShipArchetype);
}


float GetShipCapacity(uint client)
{
	auto cargoCapacity = Archetype::GetShip(Players[client].iShipArchetype)->fHoldSize;
	auto shipColGrpData = colGrpCargoMap.find(Players[client].iShipArchetype);
	if (shipColGrpData != colGrpCargoMap.end())
	{
		float totalColGrpCapacity = shipColGrpData->second.first;
		cargoCapacity -= totalColGrpCapacity;

		auto& capacityPerColGrp = shipColGrpData->second.second;

		for(auto& colGrp : Players[client].collisionGroupDesc)
		{
			if (colGrp.health <= 0.0f)
			{
				continue;
			}
			auto colGrpData = capacityPerColGrp.find(colGrp.id);
			if (colGrpData != capacityPerColGrp.end())
			{
				cargoCapacity += colGrpData->second;
			}
		}
	}

	return cargoCapacity;
}

int __cdecl PubPlayerGetRemainingHoldSize(uint& client, float* holdSize)
{
	float cargoCapacity = GetShipCapacity(client);
	float cargoOccupied = Players[client].equipDescList.get_cargo_space_occupied();
	*holdSize = cargoCapacity - cargoOccupied;

	return 0;
}

int __fastcall VerifyTargetDetour(CETractor* tractor, void* edx, CLoot* loot)
{
	int retVal = tractor->VerifyTarget(loot);
	if (retVal == 3)
	{
		CEqObj* owner = tractor->owner;
		if (owner->objectClass != CObject::CSHIP_OBJECT)
		{
			return retVal;
		}

		CShip* cship = reinterpret_cast<CShip*>(owner);
		uint shipClass = cship->shiparch()->iShipClass;
		auto cargoOverrideEntry = cargoVolumeOverrideMap.find(shipClass);
		if (cargoOverrideEntry == cargoVolumeOverrideMap.end())
		{
			return retVal;
		}

		auto volumeOverride = cargoOverrideEntry->second.find(loot->contents_arch()->iArchID);
		if (volumeOverride == cargoOverrideEntry->second.end())
		{
			return retVal;
		}

		float remainingHold = cship->get_cargo_hold_remaining();
		if (remainingHold >= volumeOverride->second * loot->get_units())
		{
			return 0;
		}
	}
	else if (retVal == 0)
	{
		uint client = tractor->owner->ownerPlayer;
		if (client && Players[client].equipDescList.equip.size() >= 120)
		{
			PrintUserCmdText(client, L"ERR Tractor cannot proceed, too many items in hold");
			return 1;
		}
	}

	return retVal;
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
	Detour((char*)commonHandle + 0x532E0, CShipGetSpaceForCargoType);

	Detour((char*)commonHandle + 0xAA8E0, (char*)EquipDescListVolume);
	Detour((char*)serverHandle + 0x74DB0, (char*)PubPlayerGetRemainingHoldSize);

	PatchCallAddr((char*)commonHandle, 0x3E292, (char*)VerifyTargetDetour);
	PatchCallAddr((char*)commonHandle, 0x3DC6C, (char*)VerifyTargetDetour);

	float** CLOOT_UNSEEN_RADIUS = (float**)0x6D64420;
	**CLOOT_UNSEEN_RADIUS = 15000.f;

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

void __stdcall GFGoodBuyAFTER(struct SGFGoodBuyInfo const& gbi, unsigned int client)
{
	returncode = DEFAULT_RETURNCODE;

	const GoodInfo* gi = GoodList_get()->find_by_id(gbi.iGoodID);

	if (!gi || gi->iType != GOODINFO_TYPE_SHIP)
	{
		return;
	}
	const GoodInfo* shipGoodInfo = GoodList_get()->find_by_id(gi->iHullGoodID);

	auto shipArch = Archetype::GetShip(shipGoodInfo->iShipGoodID);
	float usedCargoSpace = EquipDescCommodityVolume(client, shipArch->iArchID);
	float missingCargoSpace = usedCargoSpace - shipArch->fHoldSize;
	if (missingCargoSpace > 0.0f)
	{
		PrintUserCmdText(client, L"WARNING: Your new ship cannot fit all the cargo it currently holds. Sell it or you will be kicked upon undocking.");
		PrintUserCmdText(client, L"Cargo hold is exceeded by %0.0f units", missingCargoSpace);
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
	if (!cship->ownerPlayer)
	{
		auto npcKillerData = npcToDropLoot.find(ship->cobj->id);
		if (npcKillerData == npcToDropLoot.end())
		{
			cship->clear_equip_and_cargo();
			return;
		}
		PlayerData& killerData = Players[npcKillerData->second];
		uint targetAffiliation;
		float attitude;
		Reputation::Vibe::GetAffiliation(cship->repVibe, targetAffiliation, false);
		pub::Reputation::GetGroupFeelingsTowards(killerData.iReputation, targetAffiliation, attitude);

		if (attitude > 0.0f)
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

		Vector dropPos = cship->vPos;
		Vector randomVector = RandomVector(static_cast<float>(rand() % 60) + 20.f);
		dropPos.x += randomVector.x;
		dropPos.y += randomVector.y;
		dropPos.z += randomVector.z;

		CreateLootSimple(cship->system, cship->id, cargo->archetype->iArchID, min(cargo->count, amountToDrop), dropPos, false);
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
		IObjRW* iobj;
		StarSystem* dummy;
		GetShipInspect(shipId, iobj, dummy);

		if (!iobj)
		{
			continue;
		}

		CShip* cship = reinterpret_cast<CShip*>(iobj->cobj);

		if (cship->hitPoints <= 1.0f)
		{
			continue;
		}

		float totalCargoToJettison = item.second;
		if (totalCargoToJettison <= 0.0f)
		{
			continue;
		}

		unordered_map<uint, float>* overrideMap = nullptr;
		auto volumeOverrideIter = cargoVolumeOverrideMap.find(cship->shiparch()->iShipClass);
		if (volumeOverrideIter != cargoVolumeOverrideMap.end())
		{
			overrideMap = &volumeOverrideIter->second;
		}

		CEquipTraverser tr(Cargo);
		CEquipManager& eqManager = cship->equip_manager;

		CECargo* cargo = nullptr;
		while (cargo = reinterpret_cast<CECargo*>(eqManager.Traverse(tr)))
		{
			float volume = cargo->archetype->fVolume;

			if (overrideMap)
			{
				auto overrideIter = overrideMap->find(cargo->archetype->iArchID);
				if (overrideIter != overrideMap->end())
				{
					volume = overrideIter->second;
				}
			}

			if (volume == 0.0f)
			{
				continue;
			}
			bool flag = false;
			pub::IsCommodity(cargo->archetype->iArchID, flag);
			if (!flag)
			{
				continue;
			}

			float amountToJettison = min(cargo->count, ceilf(totalCargoToJettison / volume));
			iobj->jettison_cargo(cargo->iSubObjId, static_cast<ushort>(amountToJettison), cship->ownerPlayer);

			totalCargoToJettison -= amountToJettison * volume;
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

	float cargoRemaining = cship->get_cargo_hold_remaining();

	if (cargoRemaining > colGrpInfo->second)
	{
		return;
	}

	uint id = iobj->get_id();
	auto dropIter = dropMap.find(id);
	if (dropIter == dropMap.end())
	{
		dropMap[id] = colGrpInfo->second - cargoRemaining;
	}
	else
	{
		dropIter->second += colGrpInfo->second - cargoRemaining;
	}
}

int __cdecl Dock_Call(unsigned int const& iShip, unsigned int const& iDockTarget, int& iCancel, enum DOCK_HOST_RESPONSE& response)
{
	returncode = DEFAULT_RETURNCODE;

	uint client = HkGetClientIDByShip(iShip);
	if (!client)
	{
		return 0;
	}

	if ((response == PROCEED_DOCK || response == DOCK) && iCancel != -1)
	{
		uint iSolarArchetypeID;
		pub::SpaceObj::GetSolarArchetypeID(iDockTarget, iSolarArchetypeID);

		auto unstableJumpObjInfo = unstableJumpObjMap.find(iSolarArchetypeID);
		if (unstableJumpObjInfo != unstableJumpObjMap.end() && EquipDescCommodityVolume(client) > unstableJumpObjInfo->second)
		{
			returncode = SKIPPLUGINS;
			PrintUserCmdText(client, L"ERR This jumphole is unstable, can't take more than %0.0f cargo through!", unstableJumpObjInfo->second);
			response = ACCESS_DENIED;
			iCancel = -1;
			return 0;
		}
	}

	return 0;
}

void __stdcall SystemSwitchOut(uint iClientID, FLPACKET_SYSTEM_SWITCH_OUT& switchOutPacket)
{
	returncode = DEFAULT_RETURNCODE;
	uint jumpClient = HkGetClientIDByShip(switchOutPacket.shipId);
	if (iClientID != jumpClient)
	{
		return;
	}
	uint spaceArchId;
	pub::SpaceObj::GetSolarArchetypeID(switchOutPacket.jumpObjectId, spaceArchId);
	auto jumpCargoLimit = unstableJumpObjMap.find(spaceArchId);
	if (jumpCargoLimit == unstableJumpObjMap.end())
	{
		return;
	}
	if (EquipDescCommodityVolume(iClientID) <= jumpCargoLimit->second)
	{
		return;
	}

	vector<pair<ushort, int>> itemsToRemove;
	for (auto& eq : Players[iClientID].equipDescList.equip)
	{
		bool isCommodity;
		pub::IsCommodity(eq.iArchID, isCommodity);
		if (isCommodity)
		{
			itemsToRemove.push_back({ eq.sID, eq.iCount });
		}
	}

	for (auto& item : itemsToRemove)
	{
		pub::Player::RemoveCargo(iClientID, item.first, item.second);
	}
}

void __stdcall AcceptTrade(unsigned int client, bool newTradeState)
{
	returncode = DEFAULT_RETURNCODE;

	auto* tradeOffer = Players[client].tradeOffer;
	if (newTradeState && tradeOffer && tradeOffer->equipOffer.equip.size() + Players[tradeOffer->targetClient].equipDescList.equip.size() >= 127)
	{
		PrintUserCmdText(client, L"ERR Target player holds too many items, trade cannot proceed");
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
	}
}

void Plugin_Communication_CallBack(PLUGIN_MESSAGE msg, void* data)
{
	returncode = DEFAULT_RETURNCODE;
	if (msg == CUSTOM_EVENT_ECON_UPDATE)
	{
		returncode = SKIPPLUGINS;
		map<uint, market_map_t>* info = reinterpret_cast<map<uint, market_map_t>*>(data);
		LoadMarketOverrides(info);
		return;
	}
	else if (msg == CUSTOM_CHECK_EQUIP_VOLUME)
	{
		returncode = SKIPPLUGINS;

		EQUIP_VOLUME_STRUCT* volumeRequest = (EQUIP_VOLUME_STRUCT*)data;
		volumeRequest->volume = GetVolumeForShipArch(volumeRequest->shipArch, volumeRequest->equipArch);
		return;
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
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&GFGoodBuyAFTER, PLUGIN_HkIServerImpl_GFGoodBuy_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch, PLUGIN_HkIServerImpl_PlayerLaunch, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseEnter_AFTER, PLUGIN_HkIServerImpl_BaseEnter_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipColGrpDestroyed, PLUGIN_ShipColGrpDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Timer, PLUGIN_HkTimerCheckKick, 0));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Dock_Call, PLUGIN_HkCb_Dock_Call, 12));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&SystemSwitchOut, PLUGIN_HkIClientImpl_Send_FLPACKET_SERVER_SYSTEM_SWITCH_OUT, 12));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CommodityLimit::ClearClientInfo, PLUGIN_ClearClientInfo, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CommodityLimit::ReqAddItem, PLUGIN_HkIServerImpl_ReqAddItem, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CommodityLimit::ReqChangeCash, PLUGIN_HkIServerImpl_ReqChangeCash, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&AcceptTrade, PLUGIN_HkIServerImpl_AcceptTrade, 0));


	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipDestroyed, PLUGIN_ShipDestroyed, 0));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Plugin_Communication_CallBack, PLUGIN_Plugin_Communication, 0));

	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));

	return p_PI;
}
