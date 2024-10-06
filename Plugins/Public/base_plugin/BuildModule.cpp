#include "Main.h"

BuildModule::BuildModule(PlayerBase* the_base)
	: Module(TYPE_BUILD), base(the_base), amassedCookingRate(0)
{
}

// Find the recipe for this building_type and start construction.
BuildModule::BuildModule(PlayerBase* the_base, const RECIPE* module_recipe)
	: Module(TYPE_BUILD), base(the_base), active_recipe(RECIPE(*module_recipe)), amassedCookingRate(0)
{
}

wstring BuildModule::GetInfo(bool xml)
{
	wstring info;
	std::wstring Status;
	if (Paused)	Status = L"(Paused) ";
	else Status = L"(Active) ";
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


	info = start + L"Constructing " + Status + active_recipe.infotext + L". Waiting for:";

	float minutesToCompletion = 0;

	for (auto& i = active_recipe.consumed_items.begin();
		i != active_recipe.consumed_items.end(); ++i)
	{
		uint good = i->first;
		uint quantity = i->second;

		minutesToCompletion += quantity / active_recipe.cooking_rate;

		const GoodInfo* gi = GoodList::find_by_id(good);
		if (!gi)
		{
			continue;
		}
		info += openLine + L"- " + itows(quantity) + L"x " + HkGetWStringFromIDS(gi->iIDSName);
		uint currStock = base->HasMarketItem(good);
		if (!currStock)
		{
			info += L" [Out of stock]";
		}
		else
		{
			info += L" [" + itows(currStock) + L" in stock]";
		}
	}
	if (active_recipe.credit_cost)
	{
		info += openLine + L"- Credits $" + UIntToPrettyStr(active_recipe.credit_cost);
		minutesToCompletion = max(minutesToCompletion, active_recipe.credit_cost / (active_recipe.cooking_rate * 100));
		if (base->money < active_recipe.credit_cost)
		{
			info += L" [Insufficient cash]";
		}
	}
	info += openLine + L"Time until completion: " + TimeString(static_cast<uint>(minutesToCompletion) * 60);
	info += end;
	return info;
}


bool BuildModule::TryConsume(float volumeToProcess)
{
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

		if (!i->second)
		{
			active_recipe.consumed_items.erase(i);
		}
		if (amassedCookingRate > 0.0f)
		{
			TryConsume(amassedCookingRate);
		}
		return true;
	}

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

			if (item.second)
			{
				active_recipe.consumed_items.push_back({ goodIter->first, item.second });
			}
			active_recipe.dynamic_consumed_items.erase(itemGroup);

			if (amassedCookingRate > 0.0f)
			{
				TryConsume(amassedCookingRate);
			}
			return true;
		}
	}

	bool consumedAnything = false;
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
			if (amassedCookingRate > 0.0f)
			{
				TryConsume(amassedCookingRate);
			}
			return true;
		}
	}
	return false;
}

// Every 10 seconds we consume goods for the active recipe at the cooking rate
// and if every consumed item has been used then declare the the cooking complete
// and convert this module into the specified type.	
bool BuildModule::Timer(uint time)
{

	if ((time % set_tick_time) != 0)
		return false;

	if (Paused || (!base->isCrewSupplied && !set_holiday_mode))
		return false;

	if (active_recipe.credit_cost)
	{
		uint moneyToRemove = static_cast<uint>(min(active_recipe.cooking_rate * 100, active_recipe.credit_cost));
		if (base->money >= moneyToRemove)
		{
			base->money -= moneyToRemove;
			active_recipe.credit_cost -= moneyToRemove;
		}
	}

	amassedCookingRate += active_recipe.cooking_rate;

	bool consumedAnything = TryConsume(amassedCookingRate);

	if (!consumedAnything)
	{
		amassedCookingRate -= active_recipe.cooking_rate;
	}

	// Do nothing if cooking is not finished
	if (!active_recipe.consumed_items.empty()
		|| !active_recipe.dynamic_consumed_items.empty()
		|| !active_recipe.dynamic_consumed_items_alt.empty()
		|| active_recipe.credit_cost)
	{
		return false;
	}

	// Once cooked turn this into the build type

	bool builtCore = false;
	for (uint i = 0; i < base->modules.size(); i++)
	{
		if (base->modules[i] == this)
		{
			switch (this->active_recipe.shortcut_number)
			{
			case Module::TYPE_CORE:

				if (base->modules[0])
				{
					// Delete and respawn the old core module
					delete base->modules[0];
					base->modules[0] = nullptr;
					return false;
				}

				base->base_level++;
				if (base->base_level > 4)
					base->base_level = 4;
				base->SetupDefaults();

				// Clear the build module slot.
				builtCore = true;

				base->modules[0] = new CoreModule(base);
				base->modules[0]->Spawn();

				base->modules[i] = nullptr;
				base->RecalculateCargoSpace();
				break;
			case Module::TYPE_STORAGE:
				base->modules[i] = new StorageModule(base);
				base->RecalculateCargoSpace();
				break;
			case Module::TYPE_DEFENSE_1:
				base->modules[i] = new DefenseModule(base, Module::TYPE_DEFENSE_1);
				break;
			case Module::TYPE_DEFENSE_2:
				base->modules[i] = new DefenseModule(base, Module::TYPE_DEFENSE_2);
				break;
			case Module::TYPE_DEFENSE_3:
				base->modules[i] = new DefenseModule(base, Module::TYPE_DEFENSE_3);
				break;
			case Module::TYPE_FACTORY:
				//check if factory
				if (factoryNicknameToCraftTypeMap.count(active_recipe.nickname))
				{
					base->modules[i] = new FactoryModule(base, active_recipe.nickname);
					base->RecalculateCargoSpace();
					break;
				}
				base->modules[i] = nullptr;
				break;
			 default:
				base->modules[i] = nullptr;
			}
			base->Save();
			return false;
		}
	}

	if (builtCore)
	{
		base->modules.resize((base->base_level * 3) + 1);
	}

	return false;
}

void BuildModule::LoadState(INI_Reader& ini)
{
	while (ini.read_value())
	{
		if (ini.is_value("build_type"))
		{
			uint nickname = CreateID(ini.get_value_string());
			if (!recipeMap.count(nickname))
			{
				return;
			}
			active_recipe = recipeMap.at(nickname);
			active_recipe.consumed_items.clear();
			active_recipe.credit_cost = 0;
		}
		else if (ini.is_value("paused"))
		{
			Paused = ini.get_value_bool(0);
		}
		else if (ini.is_value("consumed"))
		{
			uint good = ini.get_value_int(0);
			uint quantity = ini.get_value_int(1);
			if (quantity)
			{
				active_recipe.consumed_items.emplace_back(make_pair(good, quantity));
			}
		}
		else if (ini.is_value("credit_cost"))
		{
			active_recipe.credit_cost = ini.get_value_int(0);
		}
	}
}

void BuildModule::SaveState(FILE* file)
{
	fprintf(file, "[BuildModule]\n");
	fprintf(file, "build_type = %s\n", active_recipe.nicknameString.c_str());
	fprintf(file, "paused = %d\n", Paused);
	for (auto& i = active_recipe.consumed_items.begin();
		i != active_recipe.consumed_items.end(); ++i)
	{
		if (i->second)
		{
			fprintf(file, "consumed = %u, %u\n", i->first, i->second);
		}
	}
	if (active_recipe.credit_cost)
	{
		fprintf(file, "credit_cost = %u", active_recipe.credit_cost);
	}
}

const RECIPE* BuildModule::GetModuleRecipe(wstring& module_name, wstring& build_list)
{
	module_name = ToLower(module_name);
	uint shortcut_number = ToInt(module_name);
	if (craftListNumberModuleMap.count(build_list) && craftListNumberModuleMap[build_list].count(shortcut_number))
	{
		return &craftListNumberModuleMap[build_list][shortcut_number];
	}
	else if (moduleNameRecipeMap.count(module_name))
	{
		return &moduleNameRecipeMap[module_name];
	}
	return 0;
}