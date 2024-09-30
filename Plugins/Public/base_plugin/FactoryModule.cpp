#include "Main.h"

FactoryModule::FactoryModule(PlayerBase* the_base)
	: Module(Module::TYPE_FACTORY), base(the_base)
{
	amassedCookingRate = 0;
	active_recipe.nickname = 0;
}

// Find the recipe for this building_type and start construction.
FactoryModule::FactoryModule(PlayerBase* the_base, uint nickname)
	: Module(Module::TYPE_FACTORY), factoryNickname(nickname), base(the_base)
{
	amassedCookingRate = 0;
	active_recipe.nickname = 0;
	cargoSpace = recipeMap[nickname].moduleCargoStorage;
	for (wstring& craftType : factoryNicknameToCraftTypeMap[factoryNickname])
	{
		base->availableCraftList.insert(craftType);
		base->craftTypeTofactoryModuleMap[craftType] = this;
	}
}

wstring FactoryModule::GetInfo(bool xml)
{
	wstring info;

	std::wstring Status = L"";
	if (Paused)
	{
		Status = L"(Paused) ";
	}
	else
	{
		Status = L"(Active) ";
	}


	wstring openLine;
	wstring start;
	wstring end;
	if (xml)
	{
		openLine = L"</TEXT><PARA/><TEXT>      ";
		start = L"<TEXT>";
		end = L"</TEXT>";
	}
	else
	{
		openLine = L"\n - ";
		start = L"";
		end = L"";
	}

	info += start + recipeMap[factoryNickname].infotext;

	if (!build_queue.empty())
	{
		info += openLine + L"Pending " + itows(build_queue.size()) + L" items";
	}
	if (!active_recipe.nickname)
	{
		info += end;
		return info;
	}
	if (active_recipe.consumed_items.empty() && active_recipe.dynamic_consumed_items.empty())
	{
		info += openLine + active_recipe.infotext + L": Waiting for free cargo storage" + openLine + L"or available max stock limit to drop off:";
		for (auto& item : active_recipe.produced_items)
		{
			if (!item.second)
			{
				continue;
			}
			uint good = item.first;
			uint quantity = item.second;
			const GoodInfo* gi = GoodList::find_by_id(good);
			info += openLine + L"- " + itows(quantity) + L"x " + HkGetWStringFromIDS(gi->iIDSName);
		}

		info += end;
		return info;
	}

	info += openLine + L"Crafting " + Status + active_recipe.infotext + L". Waiting for:";

	float volumeSum = 0;

	for (auto& i : active_recipe.dynamic_consumed_items)
	{
		bool isFirst = true;
		info += openLine + L"- ";
		float dynamicSum = 0;
		for (auto& j : i)
		{
			if (isFirst)
			{
				isFirst = false;
			}
			else
			{
				info += L" or ";
			}


			uint good = j.first;
			uint quantity = j.second;

			if (!quantity)
			{
				continue;
			}

			auto eq = Archetype::GetEquipment(good);

			if (eq)
			{
				dynamicSum += eq->fVolume * quantity;
			}
			const GoodInfo* gi = GoodList::find_by_id(good);
			if (!gi)
			{
				continue;
			}
			info += itows(quantity) + L"x " + HkGetWStringFromIDS(gi->iIDSName);
		}
		dynamicSum /= i.size();
		volumeSum += dynamicSum;
	}  
	for (auto& i : active_recipe.dynamic_consumed_items_alt)
	{
		bool isFirst = true;
		info += openLine + L"- " + itows(i.sharedAmount) + L"x of ";
		float dynVolumeSum = 0;
		for (uint itemId : i.items)
		{
			if (isFirst)
			{
				isFirst = false;
			}
			else
			{
				info += L", ";
			}

			auto eq = Archetype::GetEquipment(itemId);

			if (eq)
			{
				dynVolumeSum += eq->fVolume;
			}

			const GoodInfo* gi = GoodList::find_by_id(itemId);
			if (!gi)
			{
				continue;
			}
			info += HkGetWStringFromIDS(gi->iIDSName);
		}
		dynVolumeSum /= i.items.size();
		volumeSum += i.sharedAmount * dynVolumeSum;
	}

	for (auto& i : active_recipe.consumed_items)
	{
		uint good = i.first;
		uint quantity = i.second;
		if (!quantity)
		{
			continue;
		}

		const GoodInfo* gi = GoodList::find_by_id(good);
		if (!gi)
		{
			continue;
		}
		info += openLine + L"- " + itows(quantity) + L"x " + HkGetWStringFromIDS(gi->iIDSName);
		if (quantity > 0)
		{
			uint currStock = base->HasMarketItem(good);

			auto eq = Archetype::GetEquipment(good);

			if (eq)
			{
				volumeSum += eq->fVolume * quantity;
			}

			if (!currStock)
			{
				info += L" [Out of stock]";
			}
			else
			{
				info += L" [" + itows(currStock) + L" in stock]";
			}
		}
	}

	if (active_recipe.credit_cost)
	{
		info += openLine + L" - Credits $" + UIntToPrettyStr(active_recipe.credit_cost);
		volumeSum = max(volumeSum, active_recipe.credit_cost / (active_recipe.cooking_rate * 100));
		if (base->money < active_recipe.credit_cost)
		{
			info += L" [Insufficient cash]";
		}
	}
	if (!active_recipe.catalyst_items.empty() && !sufficientCatalysts)
	{
		info += openLine + L"Needed catalysts:";

		for (const auto& catalyst : active_recipe.catalyst_items)
		{
			uint good = catalyst.first;
			uint quantity = catalyst.second;

			const GoodInfo* gi = GoodList::find_by_id(good);
			if (gi)
			{
				info += openLine + L" - " + itows(quantity) + L"x " + HkGetWStringFromIDS(gi->iIDSName);
			}
		}
	}
	if (!active_recipe.catalyst_workforce.empty() && !sufficientCatalysts)
	{
		info += openLine + L"Needed workforce:";
		for (const auto& worker : active_recipe.catalyst_workforce)
		{
			uint good = worker.first;
			uint quantity = worker.second;

			const GoodInfo* gi = GoodList::find_by_id(good);
			if (gi)
			{
				info += openLine + L" - " + itows(quantity) + L"x " + HkGetWStringFromIDS(gi->iIDSName);
			}
		}
	}

	info += openLine + L"Time until completion: " + TimeString(static_cast<uint>(volumeSum / active_recipe.cooking_rate)*60);
	info += end;

	return info;
}

// Every 10 seconds we consume goods for the active recipe at the cooking rate
// and if every consumed item has been used then declare the the cooking complete
// and convert this module into the specified type.	
bool FactoryModule::Timer(uint time)
{

	if ((time % set_tick_time) != 0)
	{
		return false;
	}

	// Get the next item to make from the build queue.
	if (!active_recipe.nickname && !build_queue.empty())
	{
		SetActiveRecipe(build_queue.front(), true);
		build_queue.pop_front();
	}

	// Nothing to do.
	if (!active_recipe.nickname || (!base->isCrewSupplied && !set_holiday_mode) || Paused)
	{
		return false;
	}

	// Consume goods at the cooking rate.
	for (const auto& catalyst : active_recipe.catalyst_items)
	{
		uint good = catalyst.first;
		int quantityNeeded = catalyst.second;

		int presentAmount = base->HasMarketItem(good);
		if ((presentAmount - base->reservedCatalystMap[good]) < quantityNeeded)
		{
			sufficientCatalysts = false;
			return false;
		}
	}
	for (const auto& workers : active_recipe.catalyst_workforce)
	{
		uint good = workers.first;
		int quantityNeeded = workers.second;

		int presentAmount = base->HasMarketItem(good);
		if ((presentAmount - base->reservedCatalystMap[good]) < quantityNeeded)
		{
			sufficientCatalysts = false;
			return false;
		}
	}

	sufficientCatalysts = true;
	bool consumedAnything = false;
	bool consumedCredits = false;
	
	if (active_recipe.credit_cost)
	{
		uint moneyToRemove = min(static_cast<uint>(active_recipe.cooking_rate) * 100, active_recipe.credit_cost);
		moneyToRemove = min(moneyToRemove, static_cast<uint>(base->money));
		if (moneyToRemove)
		{
			base->money -= moneyToRemove;
			active_recipe.credit_cost -= moneyToRemove;
			consumedCredits = true;
		}
	}

	float volumeToProcess = amassedCookingRate + active_recipe.cooking_rate;

	for (auto& i = active_recipe.consumed_items.begin(); i != active_recipe.consumed_items.end(); i++)
	{
		uint good = i->first;
		auto market_item = base->market_items.find(good);
		if (market_item == base->market_items.end() || !market_item->second.quantity)
		{
			continue;
		}

		auto eq = Archetype::GetEquipment(market_item->first);

		if (!eq)
		{
			continue;
		}

		if (volumeToProcess < eq->fVolume)
		{
			break;
		}

		float origVolumeToProcess = volumeToProcess;

		volumeToProcess = min(volumeToProcess, i->second * eq->fVolume);
		float countToRemove = floorf(volumeToProcess / eq->fVolume);
		uint quantityToConsumeUint = static_cast<uint>(min(countToRemove, market_item->second.quantity));
		i->second -= quantityToConsumeUint;
		amassedCookingRate = origVolumeToProcess - (countToRemove * eq->fVolume);
		
		base->RemoveMarketGood(good, quantityToConsumeUint);

		if (base->pinned_market_items.count(i->first))
		{
			base->pinned_item_updated = true;
		}

		consumedAnything = true;
		if (!i->second)
		{
			active_recipe.consumed_items.erase(i);
		}
		break;
	}

	if (!consumedAnything)
	{
		for (auto& itemGroup = active_recipe.dynamic_consumed_items.begin();
			itemGroup != active_recipe.dynamic_consumed_items.end(); itemGroup++)
		{
			for (auto& item : *itemGroup)
			{
				auto& goodIter = base->market_items.find(item.first);
				if (goodIter == base->market_items.end() || !goodIter->second.quantity)
				{
					continue;
				}

				auto eq = Archetype::GetEquipment(item.first);

				if (!eq)
				{
					continue;
				}

				if (volumeToProcess < eq->fVolume)
				{
					break;
				}

				float origVolumeToProcess = volumeToProcess;

				volumeToProcess = min(volumeToProcess, item.second * eq->fVolume);
				float countToRemove = floorf(volumeToProcess / eq->fVolume);
				uint quantityToConsumeUint = static_cast<uint>(min(countToRemove, goodIter->second.quantity));
				item.second -= quantityToConsumeUint;
				amassedCookingRate = origVolumeToProcess - countToRemove * eq->fVolume;

				base->RemoveMarketGood(goodIter->first, quantityToConsumeUint);

				active_recipe.consumed_items.push_back({ goodIter->first, item.second });
				consumedAnything = true;
				active_recipe.dynamic_consumed_items.erase(itemGroup);
				break;
			}
		}
	}

	if (!consumedAnything)
	{
		for (auto& items = active_recipe.dynamic_consumed_items_alt.begin();
			items != active_recipe.dynamic_consumed_items_alt.end();)
		{
			for (uint itemId : items->items)
			{
				auto& goodIter = base->market_items.find(itemId);
				if (goodIter == base->market_items.end() || !goodIter->second.quantity)
				{
					continue;
				}

				auto eq = Archetype::GetEquipment(itemId);

				if (!eq)
				{
					continue;
				}

				if (volumeToProcess < eq->fVolume)
				{
					break;
				}

				float origVolumeToProcess = volumeToProcess;

				volumeToProcess = min(volumeToProcess, items->sharedAmount * eq->fVolume);
				float countToRemove = floorf(volumeToProcess / eq->fVolume);
				uint quantityToConsumeUint = static_cast<uint>(min(countToRemove, goodIter->second.quantity));
				amassedCookingRate = origVolumeToProcess - countToRemove * eq->fVolume;

				base->RemoveMarketGood(goodIter->first, quantityToConsumeUint);
				consumedAnything = true;
				items->sharedAmount -= quantityToConsumeUint;
				break;
			}
			if (!items->sharedAmount)
			{
				items = active_recipe.dynamic_consumed_items_alt.erase(items);
			}
			else
			{
				items++;
			}
			if (consumedAnything)
			{
				break;
			}
		}
	}

	if (!consumedAnything)
	{
		amassedCookingRate = volumeToProcess;
	}

	if (consumedAnything || consumedCredits)
	{
		for (const auto& catalyst : active_recipe.catalyst_items)
		{
			base->reservedCatalystMap[catalyst.first] += catalyst.second;
		}
		for (const auto& catalyst : active_recipe.catalyst_workforce)
		{
			base->reservedCatalystMap[catalyst.first] += catalyst.second;
		}
	}

	// Do nothing if cooking is not finished
	if (!active_recipe.consumed_items.empty()
		|| !active_recipe.dynamic_consumed_items.empty()
		|| !active_recipe.dynamic_consumed_items_alt.empty()
		|| active_recipe.credit_cost)
	{
		return false;
	}

	// Add the newly produced item to the market. If there is insufficient space
	// to add the item, wait until there is space.
	for (auto& item : active_recipe.produced_items)
	{
		if (!base->AddMarketGood(item.first, item.second))
		{
			return false;
		}
		else
		{
			item.second = 0;
		}
	}

	if (!build_queue.empty())
	{
		// Load next item in the queue
		SetActiveRecipe(build_queue.front(), true);
		build_queue.pop_front();
	}
	else if (active_recipe.loop_production)
	{
		// If recipe is set to automatically loop, refresh the recipe data
		SetActiveRecipe(active_recipe.nickname, false);
	}
	else
	{
		active_recipe.nickname = 0;
	}

	return false;
}

void FactoryModule::LoadState(INI_Reader& ini)
{
	active_recipe.nickname = 0;
	RECIPE foundRecipe;

	vector<pair<uint, uint>> producedCopy;

	while (ini.read_value())
	{
		if (ini.is_value("type"))
		{
			factoryNickname = CreateID(ini.get_value_string(0));

			cargoSpace = recipeMap[factoryNickname].moduleCargoStorage;
			for (auto& craftType : factoryNicknameToCraftTypeMap[factoryNickname])
			{
				base->availableCraftList.insert(craftType);
				base->craftTypeTofactoryModuleMap[craftType] = this;
			}
		}
		else if (ini.is_value("nickname"))
		{
			uint activeRecipeNickname = ini.get_value_int(0);
			if (activeRecipeNickname)
			{
				SetActiveRecipe(activeRecipeNickname, true);
				active_recipe.consumed_items.clear();
				active_recipe.dynamic_consumed_items.clear();
				active_recipe.dynamic_consumed_items_alt.clear();
				producedCopy = active_recipe.produced_items;
				active_recipe.produced_items.clear();
			}
		}
		else if (ini.is_value("paused"))
		{
			Paused = ini.get_value_bool(0);
		}
		else if (ini.is_value("consumed"))
		{
			uint goodID = ini.get_value_int(0);
			uint amount = ini.get_value_int(1);
			if (active_recipe.nickname && amount)
			{
				active_recipe.consumed_items.emplace_back(make_pair(goodID, amount));
			}
		}
		else if (ini.is_value("consumed_dynamic"))
		{
			vector<pair<uint, uint>> vector;
			int counter = 0;
			int itemId = 0;
			do
			{
				itemId = ini.get_value_int(counter * 2);
				int itemAmount = ini.get_value_int(counter * 2 + 1);
				counter++;
				if (itemId)
				{
					vector.push_back({ itemId, itemAmount });
				}
			} while (itemId);
			if (!vector.empty())
			{
				active_recipe.dynamic_consumed_items.emplace_back(vector);
			}
		}
		else if (ini.is_value("consumed_dynamic_alt"))
		{
			DYNAMIC_ITEM item;
			int counter = 1;
			int itemId = 0;
			item.sharedAmount = ini.get_value_int(0);
			do
			{
				itemId = ini.get_value_int(counter);
				counter++;
				if (itemId)
				{
					item.items.push_back(itemId);
				}
			} while (itemId);
			if (!item.items.empty())
			{
				active_recipe.dynamic_consumed_items_alt.emplace_back(item);
			}
		}
		else if (ini.is_value("credit_cost"))
		{
			if (active_recipe.nickname)
			{
				active_recipe.credit_cost = ini.get_value_int(0);
			}
		}
		else if (ini.is_value("build_queue"))
		{
			if (active_recipe.nickname)
			{
				build_queue.emplace_back(ini.get_value_int(0));
			}
		}
		else if (ini.is_value("produced"))
		{
			if (active_recipe.nickname)
			{
				active_recipe.produced_items.push_back({ ini.get_value_int(0), ini.get_value_int(1) });
			}
		}
	}
	if (active_recipe.produced_items.empty())
	{
		active_recipe.produced_items = producedCopy;
	}
}

void FactoryModule::SaveState(FILE* file)
{
	fprintf(file, "[FactoryModule]\n");
	fprintf(file, "type = %s\n", recipeMap[factoryNickname].nicknameString.c_str());
	if (active_recipe.nickname)
	{
		fprintf(file, "nickname = %u\n", active_recipe.nickname);
		fprintf(file, "paused = %d\n", Paused);
		if (active_recipe.credit_cost)
			fprintf(file, "credit_cost = %u\n", active_recipe.credit_cost);
		for (auto& i : active_recipe.consumed_items)
		{
			if (i.second)
			{
				fprintf(file, "consumed = %u, %u\n", i.first, i.second);
			}
		}
		for (auto& i : active_recipe.dynamic_consumed_items)
		{
			bool isFirst = true;
			fprintf(file, "consumed_dynamic = ");
			for (auto& j : i)
			{
				if (isFirst)
				{
					isFirst = false;
				}
				else
				{
					fprintf(file, ",");
				}
				fprintf(file, "%u, %u", j.first, j.second);
			}
			fprintf(file, "\n");
		}
		for (auto& i : active_recipe.dynamic_consumed_items_alt)
		{
			fprintf(file, "consumed_dynamic_alt = %u", i.sharedAmount);
			for (uint j : i.items)
			{
				fprintf(file, ", %u", j);
			}
			fprintf(file, "\n");
		}
		for (auto& i : active_recipe.produced_items)
		{
			fprintf(file, "produced = %u, %u\n", i.first, i.second);
		}
	}
	for (uint i : build_queue)
	{
		fprintf(file, "build_queue = %u\n", i);
	}
}

void FactoryModule::SetActiveRecipe(uint product, bool resetAmassedCookingRate)
{
	if (resetAmassedCookingRate)
	{
		amassedCookingRate = 0;
	}

	active_recipe = RECIPE(recipeMap[product]);
	if (active_recipe.affiliationBonus.count(base->affiliation))
	{
		float productionModifier = active_recipe.affiliationBonus.at(base->affiliation);
		active_recipe.credit_cost = static_cast<uint>(ceilf(static_cast<float>(active_recipe.credit_cost) * productionModifier));
		active_recipe.cooking_rate = active_recipe.cooking_rate * productionModifier;
		for (auto& item : active_recipe.consumed_items)
		{
			item.second = static_cast<uint>(ceil(static_cast<float>(item.second) * productionModifier));
		}
	}
	for (auto& variable : active_recipe.affiliation_consumed_items)
	{
		auto material = variable.find(base->affiliation);
		if (material != variable.end())
		{
			active_recipe.consumed_items.push_back(material->second);
		}
		else if (variable.count(0))
		{
			active_recipe.consumed_items.push_back(variable.at(0));
		}
	}

	for (auto& variable : active_recipe.affiliation_produced_items)
	{
		auto material = variable.find(base->affiliation);
		if (material != variable.end())
		{
			active_recipe.produced_items.push_back(material->second);
		}
		else if (variable.count(0))
		{
			active_recipe.produced_items.push_back(variable.at(0));
		}
	}
}

bool FactoryModule::AddToQueue(uint product)
{
	if (!active_recipe.nickname)
	{
		SetActiveRecipe(product, true);
		return true;
	}
	else if(active_recipe.loop_production && active_recipe.nickname == product)
	{
		return false;
	}
	else
	{
		build_queue.emplace_back(product);
		return true;
	}
}

bool FactoryModule::ClearQueue()
{
	build_queue.clear();
	return true;
}

void FactoryModule::ClearRecipe()
{
	active_recipe.nickname = 0;
}

bool FactoryModule::ToggleQueuePaused(bool NewState)
{
	bool RememberState = Paused;
	Paused = NewState;
	//return true if value changed
	return RememberState != NewState;
}

FactoryModule* FactoryModule::FindModuleByProductInProduction(PlayerBase* pb, uint searchedProduct)
{
	for (auto& module : pb->modules)
	{
		FactoryModule* facModPtr = dynamic_cast<FactoryModule*>(module);
		if (facModPtr && facModPtr->active_recipe.nickname == searchedProduct)
		{
			return facModPtr;
		}
	}
	return nullptr;
}

void FactoryModule::ClearAllProductionQueues(PlayerBase* pb)
{
	for (auto& i : pb->modules)
	{
		FactoryModule* facModPtr = dynamic_cast<FactoryModule*>(i);
		if (facModPtr)
		{
			facModPtr->ClearQueue();
		}
	}
}

void FactoryModule::StopAllProduction(PlayerBase* pb)
{
	for (auto& i : pb->modules)
	{
		FactoryModule* facModPtr = dynamic_cast<FactoryModule*>(i);
		if (facModPtr)
		{
			facModPtr->ClearQueue();
			facModPtr->ClearRecipe();
		}
	}
}

bool FactoryModule::IsFactoryModule(Module* module)
{
	return module->type == Module::TYPE_FACTORY;
}

const RECIPE* FactoryModule::GetFactoryProductRecipe(wstring& craftType, wstring& product)
{
	product = ToLower(product);
	int shortcut_number = ToInt(product);
	if (recipeCraftTypeNumberMap[craftType].count(shortcut_number))
	{
		return &recipeCraftTypeNumberMap[craftType][shortcut_number];
	}
	else if (recipeCraftTypeNameMap[craftType].count(product))
	{
		return &recipeCraftTypeNameMap[craftType][product];
	}
	return nullptr;
}