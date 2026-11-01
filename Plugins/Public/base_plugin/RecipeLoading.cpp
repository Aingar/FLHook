#include "RecipeLoading.h"
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>

constexpr uint RecipeItemAmountFields = 2;
constexpr uint RecipeFactionItemAmountFields = 3;



static void ValidateRecipeItem(const char* goodName)
{
	const GoodInfo* gi = GoodList_get()->find_by_name(goodName);
	if (!gi)
	{
		throw runtime_error(string("unknown recipe item: ") + goodName);
	}
}

static double ParseRecipeNumber(const std::string& text)
{
	std::istringstream input(text);
	input.imbue(std::locale::classic());
	double value = 0;
	input >> value;
	if (!input || !std::isfinite(value))
	{
		throw std::runtime_error("expected a finite number: " + text);
	}
	input >> std::ws;
	if (!input.eof())
	{
		throw std::runtime_error("expected a finite number: " + text);
	}
	return value;
}

static uint RecipeInteger(INI_Reader& ini, uint index = 0)
{
	if (index >= ini.get_num_parameters())
	{
		throw runtime_error("missing recipe number");
	}
	const double value = ParseRecipeNumber(ini.get_value_string(index));
	if (value < 0 || floor(value) != value || value > (numeric_limits<uint>::max)())
	{
		throw runtime_error("recipe number must be a nonnegative whole uint");
	}
	return static_cast<uint>(value);
}

static float RecipeFloat(INI_Reader& ini, uint index = 0)
{
	if (index >= ini.get_num_parameters())
	{
		throw runtime_error("missing recipe number");
	}
	const double value = ParseRecipeNumber(ini.get_value_string(index));
	if (value < 0 || value > (numeric_limits<float>::max)())
	{
		throw runtime_error("recipe number must be a nonnegative finite float");
	}
	return static_cast<float>(value);
}

static void ValidateRecipeValue(INI_Reader& ini, bool module)
{
	const string key = ini.get_name_ptr();
	const uint count = ini.get_num_parameters();
	if ((key == "consumed" || key == "catalyst" || key == "affiliation_bonus" || (module ? key == "crew" : key == "produced_item")) && count != RecipeItemAmountFields)
	{
		throw runtime_error(key + " needs two parameters");
	}
	if (key == "consumed_dynamic" && (count < RecipeItemAmountFields || count % RecipeItemAmountFields))
	{
		throw runtime_error(key + " needs item/amount pairs");
	}
	if (key == "consumed_dynamic_alt" && count < RecipeItemAmountFields)
	{
		throw runtime_error(key + " needs an amount and items");
	}
	if (!module && (key == "produced_affiliation" || key == "consumed_affiliation") && (count < RecipeItemAmountFields || (count - RecipeItemAmountFields) % RecipeFactionItemAmountFields))
	{
		throw runtime_error(key + " needs a default item/amount and faction/item/amount groups");
	}
}





static void AddFactoryRecipeToMaps(RecipeTables& preparedTables, const RECIPE& recipe)
{
	wstring recipeNameKey = ToLower(recipe.infotext);
	if (recipe.produced_items.empty() && recipe.affiliation_produced_items.empty())
	{
		throw runtime_error("item recipe needs produced items: " + recipe.nicknameString);
	}
	if (recipe.nicknameString.empty() || recipe.infotext.empty() || recipe.cooking_rate <= 0)
	{
		throw runtime_error("recipe needs a nickname, infotext and positive cooking_rate");
	}
	if (!preparedTables.recipeMap.emplace(recipe.nickname, recipe).second)
	{
		throw runtime_error("duplicate recipe nickname or hash: " + recipe.nicknameString);
	}
	preparedTables.recipeCraftTypeNumberMap[recipe.craft_type][recipe.shortcut_number] = recipe;
	preparedTables.recipeCraftTypeNameMap[recipe.craft_type][recipeNameKey] = recipe;
}

static void AddModuleRecipeToMaps(RecipeTables& preparedTables, const RECIPE& recipe, const vector<wstring> craft_types, const wstring& build_type, uint recipe_number)
{
	wstring recipeNameKey = ToLower(recipe.infotext);
	preparedTables.moduleIds.insert(recipe.nickname);

	if (recipe.shortcut_number != Module::TYPE_CORE && recipe.shortcut_number != Module::TYPE_FACTORY
		&& recipe.shortcut_number != Module::TYPE_STORAGE && recipe.shortcut_number != Module::TYPE_STORAGE_CUSTOM
		&& recipe.shortcut_number != Module::TYPE_DEFENSE_1 && recipe.shortcut_number != Module::TYPE_DEFENSE_2
		&& recipe.shortcut_number != Module::TYPE_DEFENSE_3)
	{
		throw runtime_error("unsupported module class: " + recipe.nicknameString);
	}
	if (recipe.shortcut_number == Module::TYPE_FACTORY)
	{
		preparedTables.factoryNicknameToCraftTypeMap[recipe.nickname];
	}

	for (const wstring& craftType : craft_types)
	{
		if (recipe.shortcut_number != Module::TYPE_STORAGE_CUSTOM)
		{
			preparedTables.factoryNicknameToCraftTypeMap[recipe.nickname].emplace_back(ToLower(craftType));
		}
	}
	if (recipe.nicknameString.empty() || recipe.infotext.empty() || recipe.cooking_rate <= 0)
	{
		throw runtime_error("recipe needs a nickname, infotext and positive cooking_rate");
	}
	if (!preparedTables.recipeMap.emplace(recipe.nickname, recipe).second)
	{
		throw runtime_error("duplicate recipe nickname or hash: " + recipe.nicknameString);
	}
	preparedTables.moduleNameRecipeMap[recipeNameKey] = recipe;
	if (!build_type.empty())
	{
		preparedTables.craftListNumberModuleMap[build_type][recipe_number] = recipe;
		preparedTables.buildingCraftLists.insert(build_type);
	}
}

static bool ReadCommonRecipeValue(INI_Reader& ini, RECIPE& recipe, bool module)
{
	if (ini.is_value("nickname"))
	{
		recipe.nickname = CreateID(ini.get_value_string(0));
		recipe.nicknameString = ini.get_value_string(0);
	}
	else if (ini.is_value("infotext"))
	{
		recipe.infotext = stows(module ? ini.get_value_string(0) : ini.get_value_string());
	}
	else if (ini.is_value("cooking_rate"))
	{
		recipe.cooking_rate = RecipeFloat(ini, 0);
	}
	else if (ini.is_value("credit_cost"))
	{
		recipe.credit_cost = RecipeInteger(ini, 0);
	}
	else if (ini.is_value("consumed"))
	{
		ValidateRecipeItem(ini.get_value_string(0));
		recipe.consumed_items.emplace_back(make_pair(CreateID(ini.get_value_string(0)), RecipeInteger(ini, 1)));
	}
	else if (ini.is_value("consumed_dynamic"))
	{
		int counter = 0;
		vector<pair<uint, uint>> vector;
		string itemName;
		do
		{
			itemName = ini.get_value_string(counter * RecipeItemAmountFields);
			if (!itemName.empty())
			{
				const uint amount = RecipeInteger(ini, counter * RecipeItemAmountFields + 1);
				ValidateRecipeItem(itemName.c_str());
				vector.push_back({ CreateID(itemName.c_str()), amount });
			}
			counter++;
		} while (!itemName.empty());
		recipe.dynamic_consumed_items.push_back(vector);
	}
	else if (ini.is_value("consumed_dynamic_alt"))
	{
		DYNAMIC_ITEM items;
		items.sharedAmount = RecipeInteger(ini, 0);
		string itemName;
		int counter = 1;
		do
		{
			itemName = ini.get_value_string(counter);
			if (!itemName.empty())
			{
				ValidateRecipeItem(itemName.c_str());
				items.items.push_back(CreateID(itemName.c_str()));
			}
			counter++;
		} while (!itemName.empty());
		recipe.dynamic_consumed_items_alt.push_back(items);
	}
	else if (ini.is_value("catalyst"))
	{
		ValidateRecipeItem(ini.get_value_string(0));
		uint cargoHash = CreateID(ini.get_value_string(0));
		if (humanCargoList.count(cargoHash))
		{
			recipe.catalyst_workforce.emplace_back(make_pair(cargoHash, RecipeInteger(ini, 1)));
		}
		else
		{
			recipe.catalyst_items.emplace_back(make_pair(cargoHash, RecipeInteger(ini, 1)));
		}
	}
	else if (ini.is_value("reqlevel"))
	{
		recipe.reqlevel = RecipeInteger(ini, 0);
	}
	else if (ini.is_value("affiliation_bonus"))
	{
		recipe.affiliationBonus[MakeId(ini.get_value_string(0))] = RecipeFloat(ini, 1);
	}
	else if (ini.is_value("restricted"))
	{
		recipe.restricted = ini.get_value_bool(0);
	}
	else
	{
		return false;
	}
	return true;
}

bool LoadRecipes(string& error, bool live)
{

	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);

	RecipeTables preparedTables;

	string cfg_fileitems = string(szCurDir) + R"(\flhook_plugins\base_recipe_items.cfg)";
	string cfg_filemodules = string(szCurDir) + R"(\flhook_plugins\base_recipe_modules.cfg)";

	INI_Reader ini;
	string file = cfg_filemodules;
	try
	{
		if (!ini.open(file.c_str(), false))
		{
			throw runtime_error("cannot open recipe file");
		}
		while (ini.read_header())
		{
			if (!ini.is_header("recipe"))
			{
				continue;
			}
			RECIPE recipe;
			vector<wstring> craft_types;
			wstring build_type;
			uint recipe_number = 0;
			while (ini.read_value())
			{
				ValidateRecipeValue(ini, true);
				if (ReadCommonRecipeValue(ini, recipe, true))
				{
					continue;
				}
				if (ini.is_value("craft_list"))
				{
					craft_types.emplace_back(stows(ToLower(ini.get_value_string(0))));
				}
				else if (ini.is_value("build_type"))
				{
					build_type = stows(ToLower(ini.get_value_string(0)));
				}
				else if (ini.is_value("recipe_number"))
				{
					recipe_number = RecipeInteger(ini, 0);
				}
				else if (ini.is_value("module_class"))
				{
					recipe.shortcut_number = RecipeInteger(ini, 0);
				}
				else if (ini.is_value("cargo_storage"))
				{
					recipe.moduleCargoStorage = RecipeInteger(ini, 0);
				}
				else if (ini.is_value("crew"))
				{
					const uint good = CreateID(ini.get_value_string(0));
					const uint amount = RecipeInteger(ini, 1);
					if (!GoodList_get()->find_by_name(ini.get_value_string(0)) || !humanCargoList.count(good)
						|| !amount || amount > (numeric_limits<uint>::max)() - recipe.moduleCrew[good])
					{
						throw runtime_error("crew must be a configured human cargo item with a positive whole amount");
					}
					recipe.moduleCrew[good] += amount;
				}
			}

			if (recipe.shortcut_number != Module::TYPE_FACTORY && recipe.shortcut_number != Module::TYPE_STORAGE_CUSTOM)
			{
				recipe.moduleCrew.clear();
			}
			if ((recipe.shortcut_number == Module::TYPE_STORAGE_CUSTOM || recipe.shortcut_number == Module::TYPE_FACTORY) && recipe.moduleCargoStorage > static_cast<uint>((numeric_limits<int>::max)()))
			{
				throw runtime_error("invalid cargo capacity for module: " + recipe.nicknameString);
			}
			AddModuleRecipeToMaps(preparedTables, recipe, craft_types, build_type, recipe_number);

		}
		ini.close();


		file = cfg_fileitems;
		if (preparedTables.moduleIds.empty())
		{
			throw runtime_error("module recipe catalog is empty");
		}
		if (!ini.open(file.c_str(), false))
		{
			throw runtime_error("cannot open recipe file");
		}
		while (ini.read_header())
		{
			if (!ini.is_header("recipe"))
			{
				continue;
			}
			RECIPE recipe;
			while (ini.read_value())
			{
				ValidateRecipeValue(ini, false);
				if (ReadCommonRecipeValue(ini, recipe, false))
				{
					continue;
				}
				if (ini.is_value("produced_item"))
				{
					ValidateRecipeItem(ini.get_value_string(0));
					uint goodAmount = RecipeInteger(ini, 1);
					if (!goodAmount)
					{
						throw runtime_error("produced_item needs a positive amount: " + recipe.nicknameString);
					}
					recipe.produced_items.emplace_back(make_pair(CreateID(ini.get_value_string(0)), RecipeInteger(ini, 1)));
				}
				else if (ini.is_value("produced_affiliation"))
				{
					unordered_map<uint, pair<uint, uint>> itemMap;
					ValidateRecipeItem(ini.get_value_string(0));
					itemMap[0] = { CreateID(ini.get_value_string(0)), RecipeInteger(ini, 1) };
					int counter = 0;

					string factionName;
					do
					{
						factionName = ini.get_value_string(counter * RecipeFactionItemAmountFields + RecipeItemAmountFields);
						string commodityName = ini.get_value_string(counter * RecipeFactionItemAmountFields + RecipeItemAmountFields + 1);
						if (!factionName.empty())
						{
							const uint amount = RecipeInteger(ini, counter * RecipeFactionItemAmountFields + RecipeItemAmountFields + 2);
							ValidateRecipeItem(commodityName.c_str());
							itemMap[MakeId(factionName.c_str())] = { CreateID(commodityName.c_str()), amount };
						}
						counter++;
					} while (!factionName.empty());

					recipe.affiliation_produced_items.push_back(itemMap);
				}
				else if (ini.is_value("loop_production"))
				{
					recipe.loop_production = RecipeInteger(ini, 0);
				}
				else if (ini.is_value("shortcut_number"))
				{
					recipe.shortcut_number = RecipeInteger(ini, 0);
				}
				else if (ini.is_value("craft_type"))
				{
					recipe.craft_type = stows(ToLower(ini.get_value_string(0)));
				}
				else if (ini.is_value("consumed_affiliation"))
				{
					unordered_map<uint, pair<uint, uint>> itemMap;
					ValidateRecipeItem(ini.get_value_string(0));
					itemMap[0] = { CreateID(ini.get_value_string(0)), RecipeInteger(ini, 1) };
					int counter = 0;

					string factionName;
					do
					{
						factionName = ini.get_value_string(counter * RecipeFactionItemAmountFields + RecipeItemAmountFields);
						string commodityName = ini.get_value_string(counter * RecipeFactionItemAmountFields + RecipeItemAmountFields + 1);
						if (!factionName.empty())
						{
							const uint amount = RecipeInteger(ini, counter * RecipeFactionItemAmountFields + RecipeItemAmountFields + 2);
							ValidateRecipeItem(commodityName.c_str());
							itemMap[MakeId(factionName.c_str())] = { CreateID(commodityName.c_str()), amount };
						}
						counter++;
					} while (!factionName.empty());

					recipe.affiliation_consumed_items.push_back(itemMap);
				}
			}
			AddFactoryRecipeToMaps(preparedTables, recipe);

		}
		ini.close();


		if (preparedTables.recipeCraftTypeNameMap.empty())
		{
			throw runtime_error("item recipe catalog is empty");
		}
		vector<PreparedBaseRecipes> access;
		if (live)
		{
			access = PrepareRecipeReload(preparedTables);
		}
		preparedTables.ExchangeWithLiveTables();
		try
		{
			PlayerCommands::PopulateHelpMenus();
		}
		catch (...)
		{
			preparedTables.ExchangeWithLiveTables();
			throw;
		}
		for (auto& entry : access)
		{
			entry.ApplyToBase();
		}
		error.clear();
		return true;
	}
	catch (const exception& e)
	{
		error = file + ": " + e.what();
		return false;
	}
}
