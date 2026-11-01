#pragma once

#include "Main.h"

struct RecipeTables
{
	unordered_map<uint, RECIPE> recipeMap;
	unordered_map<wstring, map<uint, RECIPE>> recipeCraftTypeNumberMap;
	unordered_map<wstring, unordered_map<wstring, RECIPE>> recipeCraftTypeNameMap;
	unordered_map<uint, vector<wstring>> factoryNicknameToCraftTypeMap;
	unordered_map<wstring, RECIPE> moduleNameRecipeMap;
	unordered_map<wstring, map<uint, RECIPE>> craftListNumberModuleMap;
	unordered_set<wstring> buildingCraftLists;
	unordered_set<uint> moduleIds;

	void ExchangeWithLiveTables()
	{
		recipeMap.swap(::recipeMap);
		recipeCraftTypeNumberMap.swap(::recipeCraftTypeNumberMap);
		recipeCraftTypeNameMap.swap(::recipeCraftTypeNameMap);
		factoryNicknameToCraftTypeMap.swap(::factoryNicknameToCraftTypeMap);
		moduleNameRecipeMap.swap(::moduleNameRecipeMap);
		craftListNumberModuleMap.swap(::craftListNumberModuleMap);
		buildingCraftLists.swap(::buildingCraftLists);
	}

	const RECIPE& RequireRecipe(uint nickname, bool module) const
	{
		auto found = recipeMap.find(nickname);
		if (found == recipeMap.end() || (moduleIds.count(nickname) != 0) != module)
		{
			throw runtime_error("missing or incompatible referenced recipe: " + to_string(nickname));
		}
		return found->second;
	}
};

struct PreparedModuleProperties
{
	Module* module;
	int cargoSpace;
	unordered_map<uint, uint> crew;
};

struct PreparedBaseRecipes
{
	PlayerBase* base;
	vector<PreparedModuleProperties> properties;
	set<wstring> lists;
	unordered_map<wstring, FactoryModule*> factories;

	void ApplyToBase()
	{
		for (auto& next : properties)
		{
			next.module->cargoSpace = next.cargoSpace;
			next.module->crew.swap(next.crew);
		}
		base->availableCraftList.swap(lists);
		base->craftTypeTofactoryModuleMap.swap(factories);
		base->RecalculateCargoSpace();
	}
};

vector<PreparedBaseRecipes> PrepareRecipeReload(const RecipeTables& preparedTables);
