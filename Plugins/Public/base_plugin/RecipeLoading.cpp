#include "Main.h"

void AddFactoryRecipeToMaps(const RECIPE& recipe)
{
	wstring recipeNameKey = ToLower(recipe.infotext);
	recipeMap[recipe.nickname] = recipe;
	recipeCraftTypeNumberMap[recipe.craft_type][recipe.shortcut_number] = recipe;
	recipeCraftTypeNameMap[recipe.craft_type][recipeNameKey] = recipe;
}

void AddModuleRecipeToMaps(const RECIPE& recipe, const vector<wstring> craft_types, const wstring& build_type, uint recipe_number)
{
	wstring recipeNameKey = ToLower(recipe.infotext);

	for (const wstring& craftType : craft_types)
	{
		factoryNicknameToCraftTypeMap[recipe.nickname].emplace_back(ToLower(craftType));
	}
	recipeMap[recipe.nickname] = recipe;
	moduleNameRecipeMap[recipeNameKey] = recipe;
	if (!build_type.empty())
	{
		craftListNumberModuleMap[build_type][recipe_number] = recipe;
		buildingCraftLists.insert(build_type);
	}
}

void LoadRecipes()
{

	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);

	recipeMap.clear();
	recipeCraftTypeNumberMap.clear();
	recipeCraftTypeNameMap.clear();
	factoryNicknameToCraftTypeMap.clear();
	moduleNameRecipeMap.clear();
	craftListNumberModuleMap.clear();

	string cfg_fileitems = string(szCurDir) + R"(\flhook_plugins\base_recipe_items.cfg)";
	string cfg_filemodules = string(szCurDir) + R"(\flhook_plugins\base_recipe_modules.cfg)";

	INI_Reader ini;

	if (ini.open(cfg_filemodules.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("recipe"))
			{
				RECIPE recipe;
				vector<wstring> craft_types;
				wstring build_type;
				uint recipe_number;
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						recipe.nickname = CreateID(ini.get_value_string(0));
						recipe.nicknameString = ini.get_value_string(0);
					}
					else if (ini.is_value("infotext"))
					{
						recipe.infotext = stows(ini.get_value_string(0));
					}
					else if (ini.is_value("craft_list"))
					{
						craft_types.emplace_back(stows(ToLower(ini.get_value_string(0))));
					}
					else if (ini.is_value("build_type"))
					{
						build_type = stows(ToLower(ini.get_value_string(0)));
					}
					else if (ini.is_value("recipe_number"))
					{
						recipe_number = ini.get_value_int(0);
					}
					else if (ini.is_value("module_class"))
					{
						recipe.shortcut_number = ini.get_value_int(0);
					}
					else if (ini.is_value("cooking_rate"))
					{
						recipe.cooking_rate = ini.get_value_float(0);
					}
					else if (ini.is_value("credit_cost"))
					{
						recipe.credit_cost = ini.get_value_int(0);
					}
					else if (ini.is_value("consumed"))
					{
						ValidateItem(ini.get_value_string(0));
						recipe.consumed_items.emplace_back(make_pair(CreateID(ini.get_value_string(0)), ini.get_value_int(1)));
					}
					else if (ini.is_value("consumed_dynamic"))
					{
						int counter = 0;
						vector<pair<uint, uint>> vector;
						string itemName;
						do
						{
							itemName = ini.get_value_string(counter * 2);
							int amount = ini.get_value_int(counter * 2 + 1);
							if (!itemName.empty())
							{
								ValidateItem(itemName.c_str());
								vector.push_back({ CreateID(itemName.c_str()), amount });
							}
							counter++;
						} while (!itemName.empty());
						recipe.dynamic_consumed_items.push_back(vector);
					}
					else if (ini.is_value("consumed_dynamic_alt"))
					{
						DYNAMIC_ITEM items;
						items.sharedAmount = ini.get_value_int(0);
						string itemName;
						int counter = 1;
						do
						{
							itemName = ini.get_value_string(counter);
							if (!itemName.empty())
							{
								ValidateItem(itemName.c_str());
								items.items.push_back(CreateID(itemName.c_str()));
							}
							counter++;
						} while (!itemName.empty());
						recipe.dynamic_consumed_items_alt.push_back(items);
					}
					else if (ini.is_value("catalyst"))
					{
						ValidateItem(ini.get_value_string(0));
						uint cargoHash = CreateID(ini.get_value_string(0));
						if (humanCargoList.count(cargoHash))
						{
							recipe.catalyst_workforce.emplace_back(make_pair(cargoHash, ini.get_value_int(1)));
						}
						else
						{
							recipe.catalyst_items.emplace_back(make_pair(cargoHash, ini.get_value_int(1)));
						}
					}
					else if (ini.is_value("reqlevel"))
					{
						recipe.reqlevel = ini.get_value_int(0);
					}
					else if (ini.is_value("cargo_storage"))
					{
						recipe.moduleCargoStorage = ini.get_value_int(0);
					}
					else if (ini.is_value("affiliation_bonus"))
					{
						recipe.affiliationBonus[MakeId(ini.get_value_string(0))] = ini.get_value_float(1);
					}
					else if (ini.is_value("restricted"))
					{
						recipe.restricted = ini.get_value_bool(0);
					}
				}
				AddModuleRecipeToMaps(recipe, craft_types, build_type, recipe_number);
			}
		}
		ini.close();
	}

	if (ini.open(cfg_fileitems.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("recipe"))
			{
				RECIPE recipe;
				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						recipe.nickname = CreateID(ini.get_value_string(0));
						recipe.nicknameString = ini.get_value_string(0);
					}
					else if (ini.is_value("produced_item"))
					{
						ValidateItem(ini.get_value_string(0));
						int goodAmount = ini.get_value_int(1);
						if (!goodAmount)
						{
							ConPrint(L"Error: no amount or zero specified in a recipe %ls\n", stows(recipe.nicknameString).c_str());
							continue;
						}
						recipe.produced_items.emplace_back(make_pair(CreateID(ini.get_value_string(0)), ini.get_value_int(1)));
					}
					else if (ini.is_value("produced_affiliation"))
					{
						unordered_map<uint, pair<uint, uint>> itemMap;
						ValidateItem(ini.get_value_string(0));
						itemMap[0] = { CreateID(ini.get_value_string(0)), ini.get_value_int(1) };
						int counter = 0;

						string factionName;
						do
						{
							factionName = ini.get_value_string(counter * 3 + 2);
							string commodityName = ini.get_value_string(counter * 3 + 3);
							int amount = ini.get_value_int(counter * 3 + 4);
							if (!factionName.empty())
							{
								ValidateItem(commodityName.c_str());
								itemMap[MakeId(factionName.c_str())] = { CreateID(commodityName.c_str()), amount };
							}
							counter++;
						} while (!factionName.empty());

						recipe.affiliation_produced_items.push_back(itemMap);
					}
					else if (ini.is_value("loop_production"))
					{
						recipe.loop_production = ini.get_value_int(0);
					}
					else if (ini.is_value("shortcut_number"))
					{
						recipe.shortcut_number = ini.get_value_int(0);
					}
					else if (ini.is_value("craft_type"))
					{
						recipe.craft_type = stows(ToLower(ini.get_value_string(0)));
					}
					else if (ini.is_value("infotext"))
					{
						recipe.infotext = stows(ini.get_value_string());
					}
					else if (ini.is_value("cooking_rate"))
					{
						recipe.cooking_rate = ini.get_value_float(0);
					}
					else if (ini.is_value("credit_cost"))
					{
						recipe.credit_cost = ini.get_value_int(0);
					}
					else if (ini.is_value("consumed"))
					{
						ValidateItem(ini.get_value_string(0));
						recipe.consumed_items.emplace_back(make_pair(CreateID(ini.get_value_string(0)), ini.get_value_int(1)));
					}
					else if (ini.is_value("consumed_dynamic"))
					{
						int counter = 0;
						vector<pair<uint, uint>> vector;
						string itemName;
						do
						{
							itemName = ini.get_value_string(counter * 2);
							int amount = ini.get_value_int(counter * 2 + 1);
							if (!itemName.empty())
							{
								ValidateItem(itemName.c_str());
								vector.push_back({ CreateID(itemName.c_str()), amount });
							}
							counter++;
						} while (!itemName.empty());
						recipe.dynamic_consumed_items.push_back(vector);
					}
					else if (ini.is_value("consumed_dynamic_alt"))
					{
						DYNAMIC_ITEM items;
						items.sharedAmount = ini.get_value_int(0);
						string itemName;
						int counter = 1;
						do
						{
							itemName = ini.get_value_string(counter);
							if (!itemName.empty())
							{
								ValidateItem(itemName.c_str());
								items.items.push_back(CreateID(itemName.c_str()));
							}
							counter++;
						} while (!itemName.empty());
						recipe.dynamic_consumed_items_alt.push_back(items);
					}
					else if (ini.is_value("consumed_affiliation"))
					{
						unordered_map<uint, pair<uint, uint>> itemMap;
						ValidateItem(ini.get_value_string(0));
						itemMap[0] = { CreateID(ini.get_value_string(0)), ini.get_value_int(1) };
						int counter = 0;

						string factionName;
						do
						{
							factionName = ini.get_value_string(counter * 3 + 2);
							string commodityName = ini.get_value_string(counter * 3 + 3);
							int amount = ini.get_value_int(counter * 3 + 4);
							if (!factionName.empty())
							{
								ValidateItem(commodityName.c_str());
								itemMap[MakeId(factionName.c_str())] = { CreateID(commodityName.c_str()), amount };
							}
							counter++;
						} while (!factionName.empty());

						recipe.affiliation_consumed_items.push_back(itemMap);
					}
					else if (ini.is_value("catalyst"))
					{
						ValidateItem(ini.get_value_string(0));
						uint cargoHash = CreateID(ini.get_value_string(0));
						if (humanCargoList.count(cargoHash))
						{
							recipe.catalyst_workforce.emplace_back(make_pair(cargoHash, ini.get_value_int(1)));
						}
						else
						{
							recipe.catalyst_items.emplace_back(make_pair(cargoHash, ini.get_value_int(1)));
						}
					}
					else if (ini.is_value("reqlevel"))
					{
						recipe.reqlevel = ini.get_value_int(0);
					}
					else if (ini.is_value("affiliation_bonus"))
					{
						recipe.affiliationBonus[MakeId(ini.get_value_string(0))] = ini.get_value_float(1);
					}
					else if (ini.is_value("restricted"))
					{
						recipe.restricted = ini.get_value_bool(0);
					}
				}
				AddFactoryRecipeToMaps(recipe);
			}
		}
		ini.close();
	}

	PlayerCommands::PopulateHelpMenus();
}
