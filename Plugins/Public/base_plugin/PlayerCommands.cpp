#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <list>
#include <map>
#include <algorithm>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include "Main.h"
#include <functional>
#include <vector>

constexpr uint ITEMS_PER_PAGE = 13;

// Separate base help out into pages. FL seems to have a limit of something like 4k per infocard.
const uint numPages = 4;
const wstring pages[numPages] = {
L"<TRA bold=\"true\"/><TEXT>/base help [page]</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Show this help page. Specify the page number to see the next page.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/base login [password]</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Login as base administrator. The following commands are only available if you are logged in as a base administrator.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/base addpwd [password] [viewshop], /base rmpwd [password], /base lstpwd</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Add, remove and list administrator passwords for the base. Add 'viewshop' to addpwd to only allow the password to view the shop.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/access</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Add, remove and list whitelisted/blacklisted tags, ships and factions for the base.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/base setmasterpwd [old password] [new password]</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Set the master password for the base.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/base rep [clear]</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Set or clear the faction that this base is affiliated with. When setting the affiliation, the affiliation will be that of the player executing the command.</TEXT>",

L"<TRA bold=\"true\"/><TEXT>/bank withdraw [credits], /bank deposit [credits], /bank status</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Withdraw, deposit or check the status of the credits held by the base's bank.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/shop price [item] [buyprice] [sellprice]</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Set the [price] of [item].</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/shop stock [item] [min stock] [max stock]</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>If the current stock is less than [min stock] then the item cannot be bought by docked ships.</TEXT><PARA/>"
L"<TEXT>If the current stock is more or equal to [max stock] then the item cannot be sold to the base by docked ships</TEXT><PARA/>"
L"<TEXT>To prohibit selling to the base of an item by docked ships under all conditions, set [max stock] to 0.</TEXT><PARA/>"
L"<TEXT>To prohibit buying from the base of an item by docked ships under all conditions, set [min stock] to 0.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/shop remove [item]</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Remove the item from the stock list. It cannot be sold to the base by docked ships unless they are base administrators.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/shop pin [item]</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Highlights the selected good, causing its price and stock to be visible on the base infocard between the first and second paragraph of the base's infocard.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/shop unpin [item]</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Removes the selected good from the pinned list.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/shop addcargo [itemNickname]</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Adds cargo with provided internal nickname (such as commodity_gold) to shop.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/shop [page]</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Show the shop stock list for [page]. There are a maximum of 40 items shown per page.</TEXT>",

L"<TRA bold=\"true\"/><TEXT>/base defensemode</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Control the defense mode for the base.</TEXT><PARA/>"
L"<TEXT>Defense Mode 1 - Logic: SRP Whitelist > Blacklist > IFF Standing.</TEXT><PARA/>"
L"<TEXT>Docking Rights: Anyone with good standing.</TEXT><PARA/><PARA/>"
L"<TEXT>Defense Mode 2 - Logic: Whitelist > No Dock.</TEXT><PARA/>"
L"<TEXT>Docking Rights: Whitelisted ships only.</TEXT><PARA/><PARA/>"
L"<TEXT>Defense Mode 3 - Logic: Whitelist > Hostile.</TEXT><PARA/>"
L"<TEXT>Docking Rights: Whitelisted ships only.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/base info</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Set the base's infocard description. First paragraph is a 'header' printed before Pinned Items.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/craft</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Control factory modules to produce various goods and equipment.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/base supplies</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Prints Crew, Food, Water, Oxygen and repair material counts.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/base setfood [nr]</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Sets the selected food item to be eaten by the crew first.</TEXT>",

L"<TRA bold=\"true\"/><TEXT>/base defmod</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Control defense modules.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/base shieldmod</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Control shield modules.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/base addfac [aff tag], /base rmfac [aff tag], /base lstfac, /base myfac</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Add, remove and list ally factions for the base. Show your affiliation ID and all available.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/base addhfac [aff tag], /base rmhfac [aff tag], /base lsthfac</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Add, remove and list hostile factions for the base.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/build</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Control the construction and destruction of base modules and upgrades.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/destroy [moduleIndex]</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Destroy the module at the selected index.</TEXT><PARA/><PARA/>"

L"<TRA bold=\"true\"/><TEXT>/base setshield</TEXT><TRA bold=\"false\"/><PARA/>"
L"<TEXT>Sets the vulnerability window starting hour (server time). To set the vulnerability window start to 15:00 server time input `/base setshield 15`.</TEXT><PARA/>"
L"<TEXT>Can be changed once every 30 days.</TEXT>"
};

namespace PlayerCommands
{
	static map<wstring, vector<wstring>> modules_recipe_map;
	static map<wstring, vector<wstring>> factory_recipe_map;

	//pre-generating crafting lists as they will probably be used quite a bit.
	//paying with memory to save on processing.
	vector<wstring> GenerateModuleHelpMenu(wstring buildType)
	{
		vector<wstring> generatedHelpStringList;
		for (const auto& recipe : craftListNumberModuleMap[buildType])
		{
			wstring currentString = L"|    ";
			currentString += itows(recipe.first);
			currentString += L" = ";
			currentString += recipe.second.infotext.c_str();
			generatedHelpStringList.emplace_back(currentString);
		}
		return generatedHelpStringList;
	}
	vector<wstring> GenerateFactoryHelpMenu(wstring craftType)
	{
		vector<wstring> generatedHelpStringList;
		for (const auto& recipe : recipeCraftTypeNumberMap[craftType])
		{
			wstring currentString = L"|     ";
			currentString += itows(recipe.second.shortcut_number);
			currentString += L" = ";
			currentString += recipe.second.infotext.c_str();
			if (recipe.second.restricted)
			{
				currentString += L" (restricted)";
			}
			generatedHelpStringList.emplace_back(currentString.c_str());
		}
		return generatedHelpStringList;
	}

	void PopulateHelpMenus()
	{
		modules_recipe_map.clear();
		factory_recipe_map.clear();

		for (const auto& buildType : buildingCraftLists)
		{
			modules_recipe_map[buildType] = GenerateModuleHelpMenu(buildType);
		}
		for (const auto& craftType : recipeCraftTypeNameMap)
		{
			factory_recipe_map[craftType.first] = GenerateFactoryHelpMenu(craftType.first);
		}
	}

	bool checkBaseAdminAccess(PlayerBase* base, uint client)
	{
		if (!base)
		{
			PrintUserCmdText(client, L"ERR Not in player base");
			return false;
		}

		if (!clients[client].admin)
		{
			PrintUserCmdText(client, L"ERR Access denied");
			return false;
		}
		return true;
	}

	void BaseHelp(uint client, const wstring& args)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);
		if (!base)
		{
			PrintUserCmdText(client, L"ERR Not in player base");
			return;
		}


		uint page = 0;
		wstring pageNum = GetParam(args, ' ', 2);
		if (pageNum.length())
		{
			page = ToUInt(pageNum) - 1;
			if (page < 0 || page > numPages - 1)
			{
				page = 0;
			}
		}

		wstring pagetext = pages[page];

		wchar_t titleBuf[4000];
		_snwprintf(titleBuf, sizeof(titleBuf), L"Base Help : Page %d/%d", page + 1, numPages);

		wchar_t buf[4000];
		_snwprintf(buf, sizeof(buf), L"<RDL><PUSH/>%ls<POP/></RDL>", pagetext.c_str());

		HkChangeIDSString(client, 500000, titleBuf);
		HkChangeIDSString(client, 500001, buf);

		FmtStr caption(0, 0);
		caption.begin_mad_lib(500000);
		caption.end_mad_lib();

		FmtStr message(0, 0);
		message.begin_mad_lib(500001);
		message.end_mad_lib();

		uint buttonsRendered = POPUPDIALOG_BUTTONS_CENTER_OK;
		if (page != 0)
		{
			HkChangeIDSString(client, 1244, L"PREV PAGE");
			buttonsRendered |= POPUPDIALOG_BUTTONS_LEFT_YES;
		}
		if (page < numPages - 1)
		{
			HkChangeIDSString(client, 1570, L"NEXT PAGE");
			buttonsRendered |= POPUPDIALOG_BUTTONS_RIGHT_LATER;
		}

		Plugin_Communication(CUSTOM_POPUP_INIT, &client);
		pub::Player::PopUpDialog(client, caption, message, buttonsRendered);
		auto& clientData = clients[client];
		clientData.lastPopupPage = page + 1;
		clientData.lastPopupWindowType = POPUPWINDOWTYPE::HELP;
	}

	bool RateLimitLogins(uint client, PlayerBase* base, wstring charname)
	{
		uint curr_time = (uint)time(0);
		uint big_penalty_time = 300;
		uint amount_of_attempts_to_reach_penalty = 15;

		//initiate
		if (base->unsuccessful_logins_in_a_row.find(charname) == base->unsuccessful_logins_in_a_row.end())
			base->unsuccessful_logins_in_a_row[charname] = 0;

		if (base->last_login_attempt_time.find(charname) == base->last_login_attempt_time.end())
			base->last_login_attempt_time[charname] = 0;

		//nulify counter if more than N seconds passed.
		if ((curr_time - base->last_login_attempt_time[charname]) > big_penalty_time)
			base->unsuccessful_logins_in_a_row[charname] = 0;

		uint blocktime = 1;
		if (base->unsuccessful_logins_in_a_row[charname] >= amount_of_attempts_to_reach_penalty)
			blocktime = big_penalty_time;

		uint waittime = blocktime - (curr_time - base->last_login_attempt_time[charname]);
		//You are attempting to log in too often
		if ((curr_time - base->last_login_attempt_time[charname]) < blocktime)
		{
			PrintUserCmdText(client, L"ERR You are attempting to log in too often. %d unsuccesful attempts. Wait %d seconds before repeating attempt.", base->unsuccessful_logins_in_a_row[charname], waittime);
			return true;
		}

		if (base->unsuccessful_logins_in_a_row[charname] >= amount_of_attempts_to_reach_penalty)
			base->unsuccessful_logins_in_a_row[charname] = 0;

		return false;
	}

	void BaseLogin(uint client, const wstring& args)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);

		if (!base)
		{
			PrintUserCmdText(client, L"ERR Not in player base");
			return;
		}

		//prevent too often login attempts
		wstring charname = (const wchar_t*)Players.GetActiveCharacterName(client);
		if (RateLimitLogins(client, base, charname))
		{
			return;
		}

		wstring password = GetParam(args, ' ', 2);
		if (!password.length())
		{
			PrintUserCmdText(client, L"ERR No password");
			return;
		}

		//remember last time attempt to login
		base->last_login_attempt_time[charname] = (uint)time(0);

		BasePassword searchBp;
		searchBp.pass = password;
		list<BasePassword>::iterator ret = find(base->passwords.begin(), base->passwords.end(), searchBp);
		if (ret == base->passwords.end())
		{
			base->unsuccessful_logins_in_a_row[charname]++; //count password failures
			PrintUserCmdText(client, L"ERR Access denied");
			return;
		}

		BasePassword foundBp = *ret;
		if (foundBp.admin)
		{
			clients[client].admin = true;
			SendMarketGoodSync(base, client);
			PrintUserCmdText(client, L"OK Access granted");
			PrintUserCmdText(client, L"Welcome administrator, all base command and control functions are available.");
			BaseLogging("Base %s: player %s logged in as an admin", wstos(base->basename).c_str(), wstos(charname).c_str());
			pub::Player::SendNNMessage(client, pub::GetNicknameId("nnv_pob_admin_login"));
		}
		else if (foundBp.viewshop)
		{
			clients[client].viewshop = true;
			PrintUserCmdText(client, L"OK Access granted");
			PrintUserCmdText(client, L"Welcome shop viewer.");
		}

	}

	void BaseAddPwd(uint client, const wstring& args)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);

		if (!checkBaseAdminAccess(base, client))
		{
			return;
		}

		wstring password = GetParam(args, ' ', 2);
		if (!password.length())
		{
			PrintUserCmdText(client, L"ERR No password");
			return;
		}

		BasePassword searchBp;
		searchBp.pass = password;

		if (find(base->passwords.begin(), base->passwords.end(), searchBp) != base->passwords.end())
		{
			PrintUserCmdText(client, L"ERR Password already exists");
			return;
		}

		BasePassword bp;
		bp.pass = password;

		wstring flagsStr = GetParam(args, ' ', 3);
		int flags = 0;
		if (flagsStr.length() && flagsStr == L"viewshop")
		{
			bp.viewshop = true;
		}
		else {
			bp.admin = true;
		}

		base->passwords.emplace_back(bp);
		base->Save();
		PrintUserCmdText(client, L"OK");
	}

	void BaseRmPwd(uint client, const wstring& args)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);

		if (!checkBaseAdminAccess(base, client))
		{
			return;
		}

		wstring password = GetParam(args, ' ', 2);
		if (!password.length())
		{
			PrintUserCmdText(client, L"ERR No password");
		}

		BasePassword searchBp;
		searchBp.pass = password;
		list<BasePassword>::iterator ret = find(base->passwords.begin(), base->passwords.end(), searchBp);
		if (ret != base->passwords.end())
		{
			BasePassword bp = *ret;
			base->passwords.remove(bp);
			base->Save();
			PrintUserCmdText(client, L"OK");
			return;
		}

		PrintUserCmdText(client, L"ERR Password does not exist");
	}

	void BaseSetMasterPwd(uint client, const wstring& args)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);

		if (!checkBaseAdminAccess(base, client))
		{
			return;
		}

		wstring old_password = GetParam(args, ' ', 2);
		if (!old_password.length())
		{
			PrintUserCmdText(client, L"ERR No old password");
			PrintUserCmdText(client, L"/base setmasterpwd <old_password> <new_password>");
			return;
		}

		wstring new_password = GetParam(args, ' ', 3);
		if (!new_password.length())
		{
			PrintUserCmdText(client, L"ERR No new password");
			PrintUserCmdText(client, L"/base setmasterpwd <old_password> <new_password>");
			return;
		}

		BasePassword bp;
		bp.pass = new_password;
		bp.admin = true;

		if (find(base->passwords.begin(), base->passwords.end(), bp) != base->passwords.end())
		{
			PrintUserCmdText(client, L"ERR Password already exists");
			return;
		}

		if (base->passwords.size())
		{
			if (base->passwords.front().pass != old_password)
			{
				PrintUserCmdText(client, L"ERR Incorrect master password");
				PrintUserCmdText(client, L"/base setmasterpwd <old_password> <new_password>");
				return;
			}
		}

		base->passwords.remove(base->passwords.front());
		base->passwords.push_front(bp);
		base->Save();
		PrintUserCmdText(client, L"OK New master password %s", new_password.c_str());
	}

	void BaseLstPwd(uint client, const wstring& cmd)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);

		if (!checkBaseAdminAccess(base, client))
		{
			return;
		}

		// Do not display the first password.
		bool first = true;
		for(auto& bp : base->passwords)
		{
			if (first)
			{
				first = false;
			}
			else
			{
				if (bp.admin)
				{
					PrintUserCmdText(client, L"%s - admin", bp.pass.c_str());
				}
				if (bp.viewshop)
				{
					PrintUserCmdText(client, L"%s - viewshop", bp.pass.c_str());
				}
			}
		}
		PrintUserCmdText(client, L"OK");
	}

	class Affiliations
	{
		class AffCell
		{
		public:
			wstring nickname;
			wstring factionname;
			uint id;
			AffCell(wstring a, wstring b, uint c)
			{
				nickname = a;
				factionname = b;
				id = c;
			}
			uint GetID() { return id; }
		};

		list<AffCell> AffList;

		static bool IDComparision(AffCell& obj, int y)
		{
			if (obj.GetID() == y)
				return true;
			else
				return false;
		}

		unordered_map<string, uint> factions;
		void LoadListOfReps()
		{
			INI_Reader ini;

			string factionpropfile = R"(..\data\initialworld.ini)";
			if (ini.open(factionpropfile.c_str(), false))
			{
				while (ini.read_header())
				{
					if (ini.is_header("Group"))
					{
						uint ids_name;
						string nickname;
						while (ini.read_value())
						{
							if (ini.is_value("nickname"))
							{
								nickname = ini.get_value_string();
							}
							else if (ini.is_value("ids_name"))
							{
								ids_name = ini.get_value_int(0);
							}

						}
						factions[nickname] = ids_name;
					}
				}
				ini.close();
				ConPrint(L"Rep: Loaded %u factions\n", factions.size());
			}
		}

		wstring GetFactionName(int ID)
		{
			try
			{
				wstring theaffiliation = HkGetWStringFromIDS(Reputation::get_name(ID)).c_str();
				if (theaffiliation == L"Object Unknown")
				{
					theaffiliation = L"Unknown Reputation";
				}
				return theaffiliation;
			}
			catch (exception e)
			{
				return L"Unknown Reputation";
			}
		}

		void LoadAffList()
		{
			if (AffList.size() == 0)
			{
				if (factions.size() == 0)
					LoadListOfReps();

				for (auto iter = factions.begin(); iter != factions.end(); iter++)
				{
					string factionnickname = iter->first;
					//MakeID function (in built in Flhook) is the same as mentioned here in C# to CreateFactionID https://github.com/DiscoveryGC/FLHook/blob/master/Plugins/Public/playercntl_plugin/setup_src/FLUtility.cs
					uint ID = MakeId(factionnickname.c_str());
					wstring factionname = GetFactionName(ID);
					AffList.push_front({ stows(factionnickname), factionname, ID });
				}
			}
			ConPrint(L"base: AffList was loaded succesfully.\n");
		}
	public:
		void Init()
		{
			LoadAffList();
		}

		AffCell* GetAffiliation(uint affId)
		{
			auto found = std::find_if(AffList.begin(), AffList.end(), std::bind(IDComparision, std::placeholders::_1, affId));
			if (found != AffList.end())
			{
				return &*found;
			}
			return nullptr;
		}

		void FindAndPrintOneAffiliation(uint client, uint AffiliationID)
		{
			std::list<Affiliations::AffCell>::iterator found;
			found = std::find_if(AffList.begin(), AffList.end(), std::bind(IDComparision, std::placeholders::_1, AffiliationID));
			if (found != AffList.end())
				PrintUserCmdText(client, L"IFF ID: %s, %s", (found->nickname).c_str(), (found->factionname).c_str());
			else
				PrintUserCmdText(client, L"IFF ID: %u, Unknown, Unknown", AffiliationID);
		}
		void PrintAll(uint client)
		{
			for (list<Affiliations::AffCell>::iterator iter = AffList.begin(); iter != AffList.end(); iter++)
			{
				PrintUserCmdText(client, L"IFF ID: %s, %s", (iter->nickname).c_str(), (iter->factionname).c_str());
			}
		}

		const AffCell* GetFirstAffiliationMatch(const wstring& string)
		{
			const wstring loweredCaseName = ToLower(string);
			for (auto& affil : AffList)
			{
				if (ToLower(affil.factionname).find(loweredCaseName) != wstring::npos)
				{
					return &affil;
				}
			}
			return nullptr;
		}
	};
	Affiliations A;
	void Aff_initer() { A.Init(); };

	void BaseViewMyFac(uint client, const wstring& cmd)
	{
		const wstring& secondword = GetParam(cmd, ' ', 1);

		A.PrintAll(client);

		uint aff = GetAffliationFromClient(client);
		auto playerAff = A.GetAffiliation(aff);
		if (!playerAff)
		{
			PrintUserCmdText(client, L"Ship IFF ID: None");
			return;
		}

		PrintUserCmdText(client, L"Ship IFF ID: %s, %s", playerAff->factionname.c_str(), playerAff->nickname.c_str());
	}

	void BaseRep(uint client, const wstring& args)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);
		if (!base)
		{
			PrintUserCmdText(client, L"ERR Not in player base");
			return;
		}

		bool isServerAdmin = false;

		wstring rights;
		if (HkGetAdmin((const wchar_t*)Players.GetActiveCharacterName(client), rights) == HKE_OK && rights.find(L"superadmin") != -1)
		{
			isServerAdmin = true;
		}


		if (!clients[client].admin && !isServerAdmin)
		{
			PrintUserCmdText(client, L"ERR Access denied");
			return;
		}

		wstring arg = GetParam(args, ' ', 2);
		if (arg == L"clear")
		{
			if (isServerAdmin)
			{
				base->affiliation = 0;
				base->Save();
				PrintUserCmdText(client, L"OK cleared base reputation");
			}
			else
			{
				PrintUserCmdText(client, L"ERR Cannot clear affiliation, please contact administration team");
			}
			return;
		}

		if (isServerAdmin || base->affiliation <= 0 || base->affiliation == DEFAULT_AFFILIATION)
		{
			int rep;
			pub::Player::GetRep(client, rep);

			uint affiliation;
			Reputation::Vibe::Verify(rep);
			Reputation::Vibe::GetAffiliation(rep, affiliation, false);
			if (affiliation == -1)
			{
				PrintUserCmdText(client, L"OK Player has no affiliation");
				return;
			}

			pub::SpaceObj::GetRep(base->base, rep);
			pub::Reputation::SetAffiliation(rep, affiliation);

			base->affiliation = affiliation;
			base->Save();
			PrintUserCmdText(client, L"OK Affiliation set to %s", HkGetWStringFromIDS(Reputation::get_name(affiliation)).c_str());
		}
		else
		{
			PrintUserCmdText(client, L"ERR Cannot set affiliation once it's been set, please contact administration team");
		}
	}

	bool ClearAccesses(PlayerBase* base, uint client, const wstring& type)
	{
		unordered_set<wstring>* nameSet = nullptr;
		unordered_set<uint>* factionSet = nullptr;
		list<wstring>* tagList = nullptr;

		if (type == L"srp")
		{
			base->srp_names.clear();
			base->srp_factions.clear();
			base->srp_tags.clear();

		}
		else if (type == L"blacklist")
		{
			base->hostile_names.clear();
			base->hostile_factions.clear();
			base->hostile_tags.clear();
		}
		else if (type == L"whitelist")
		{
			base->ally_names.clear();
			base->ally_factions.clear();
			base->ally_tags.clear();
		}
		else
		{
			PrintUserCmdText(client, L"ERR incorrect parameter!");
			PrintUserCmdText(client, L"usage: /access clear <srp|whitelist|blacklist>");
			return false;
		}

		PrintUserCmdText(client, L"OK!");
		return true;
	}
	void PrintAccesses(PlayerBase* base, uint client, const wstring& type)
	{
		unordered_set<wstring>* nameSet = nullptr;
		unordered_set<uint>* factionSet = nullptr;
		list<wstring>* tagList = nullptr;
		
		if (type == L"srp")
		{
			nameSet = &base->srp_names;
			factionSet = &base->srp_factions;
			tagList = &base->srp_tags;
		}
		else if (type == L"blacklist")
		{
			nameSet = &base->hostile_names;
			factionSet = &base->hostile_factions;
			tagList = &base->hostile_tags;
		}
		else if (type == L"whitelist")
		{
			nameSet = &base->ally_names;
			factionSet = &base->ally_factions;
			tagList = &base->ally_tags;
		}
		else
		{
			PrintUserCmdText(client, L"ERR incorrect parameter!");
			PrintUserCmdText(client, L"usage: /access list <srp|whitelist|blacklist>");
			return;
		}

		PrintUserCmdText(client, L"%ls entries:", type.c_str());
		PrintUserCmdText(client, L"Names:");
		if (nameSet->empty())
		{
			PrintUserCmdText(client, L"None!");
		}
		else
		{
			for (auto& name : *nameSet)
			{
				PrintUserCmdText(client, L"- %ls", name.c_str());
			}
		}

		PrintUserCmdText(client, L"Tags:");
		if (tagList->empty())
		{
			PrintUserCmdText(client, L"None!");
		}
		else
		{
			for (auto& tag : *tagList)
			{
				PrintUserCmdText(client, L"- %ls", tag.c_str());
			}
		}

		PrintUserCmdText(client, L"Factions:");
		if (factionSet->empty())
		{
			PrintUserCmdText(client, L"None!");
		}
		else
		{
			for (auto& faction : *factionSet)
			{
				auto factionData = A.GetAffiliation(faction);
				if (factionData)
				{
					PrintUserCmdText(client, L"- %ls (%ls)", factionData->factionname.c_str(), factionData->nickname.c_str());
				}
			}
		}
	}

	bool AddTagEntry(uint client, list<wstring>& tagList, const wstring& newEntry)
	{
		if (find(tagList.begin(), tagList.end(), newEntry) != tagList.end())
		{
			PrintUserCmdText(client, L"Tag already exists");
			return true;
		}
		PrintUserCmdText(client, L"OK!");
		tagList.push_back(newEntry);
		return false;
	}

	bool RemoveTagEntry(uint client, list<wstring>& tagList, const wstring& newEntry)
	{
		if (find(tagList.begin(), tagList.end(), newEntry) == tagList.end())
		{
			PrintUserCmdText(client, L"ERR No such tag!");
			return false;
		}
		PrintUserCmdText(client, L"OK!");
		tagList.remove(newEntry);
		return true;
	}

	bool AddAccess(PlayerBase* base, uint client, const wstring& entryType, const wstring& type, const wstring& entry)
	{
		if (entryType == L"tag")
		{
			if (type == L"srp")
			{
				wstring rights;
				if (!(HkGetAdmin((const wchar_t*)Players.GetActiveCharacterName(client), rights) == HKE_OK && rights.find(L"superadmin") != -1))
				{
					PrintUserCmdText(client, L"ERR: SRP accesses are only editable by admins!");
					return false;
				}

				base->hostile_tags.remove(entry);
				return AddTagEntry(client, base->srp_tags, entry);
			}
			else if (type == L"blacklist")
			{
				if (base->hostile_tags.size() >= base_access_entry_limit)
				{
					PrintUserCmdText(client, L"ERR: Unable to add entry, max entries: %u, current entries: %u",
						base_access_entry_limit, base->hostile_tags.size());
					return false;
				}
				base->ally_tags.remove(entry);
				AddTagEntry(client, base->hostile_tags, entry);
				return true;
			}
			else if (type == L"whitelist")
			{
				if (base->ally_tags.size() >= base_access_entry_limit)
				{
					PrintUserCmdText(client, L"ERR: Unable to add entry, max entries: %u, current entries: %u",
						base_access_entry_limit, base->ally_tags.size());
					return false;
				}
				base->hostile_tags.remove(entry);
				return AddTagEntry(client, base->ally_tags, entry);
			}
		}
		else if (entryType == L"name")
		{
			if (type == L"srp")
			{
				wstring rights;
				if (!(HkGetAdmin((const wchar_t*)Players.GetActiveCharacterName(client), rights) == HKE_OK && rights.find(L"superadmin") != -1))
				{
					PrintUserCmdText(client, L"ERR: SRP accesses are only editable by admins!");
					return false;
				}
				base->hostile_names.erase(entry);
				base->srp_names.insert(entry);
				PrintUserCmdText(client, L"OK!");
				return true;
			}
			else if (type == L"blacklist")
			{
				if (base->hostile_names.size() >= base_access_entry_limit)
				{
					PrintUserCmdText(client, L"ERR: Unable to add entry, max entries: %u, current entries: %u",
						base_access_entry_limit, base->hostile_names.size());
					return false;
				}
				base->ally_names.erase(entry);
				base->hostile_names.insert(entry);
				PrintUserCmdText(client, L"OK!");
				return true;
			}
			else if (type == L"whitelist")
			{
				if (base->ally_names.size() >= base_access_entry_limit)
				{
					PrintUserCmdText(client, L"ERR: Unable to add entry, max entries: %u, current entries: %u",
						base_access_entry_limit, base->ally_names.size());
					return false;
				}
				base->hostile_names.erase(entry);
				base->ally_names.insert(entry);
				PrintUserCmdText(client, L"OK!");
				return true;
			}
		}
		else if (entryType == L"faction")
		{
			auto affil = A.GetFirstAffiliationMatch(entry);
			if (!affil)
			{
				PrintUserCmdText(client, L"ERR invalid faction nickname!");
				return false;
			}

			if (type == L"srp")
			{
				wstring rights;
				if (!(HkGetAdmin((const wchar_t*)Players.GetActiveCharacterName(client), rights) == HKE_OK && rights.find(L"superadmin") != -1))
				{
					PrintUserCmdText(client, L"ERR: SRP accesses are only editable by admins!");
					return false;
				}
				base->hostile_factions.erase(affil->id);
				base->srp_factions.insert(affil->id);
				PrintUserCmdText(client, L"OK added %ls", affil->factionname.c_str());
				return true;
			}
			else if (type == L"blacklist")
			{
				if (base->hostile_factions.size() >= base_access_entry_limit)
				{
					PrintUserCmdText(client, L"ERR: Unable to add entry, max entries: %u, current entries: %u",
						base_access_entry_limit, base->hostile_factions.size());
					return false;
				}
				base->ally_factions.erase(affil->id);
				base->hostile_factions.insert(affil->id);
				PrintUserCmdText(client, L"OK added %ls", affil->factionname.c_str());
				return true;
			}
			else if (type == L"whitelist")
			{
				if (base->ally_factions.size() >= base_access_entry_limit)
				{
					PrintUserCmdText(client, L"ERR: Unable to add entry, max entries: %u, current entries: %u",
						base_access_entry_limit, base->ally_factions.size());
					return false;
				}
				base->hostile_factions.erase(affil->id);
				base->ally_factions.insert(affil->id);
				PrintUserCmdText(client, L"OK added %ls", affil->factionname.c_str());
				return true;
			}
		}

		PrintUserCmdText(client, L"ERR incorrect parameters!");
		PrintUserCmdText(client, L"usage: /access add <tag|name|faction> <srp|whitelist|blacklist> <entry>");

		return false;
	}

	bool RemoveAccess(PlayerBase* base, uint client, const wstring& entryType, const wstring& type, const wstring& entry)
	{
		if (entryType == L"tag")
		{
			if (type == L"srp")
			{
				wstring rights;
				if (!(HkGetAdmin((const wchar_t*)Players.GetActiveCharacterName(client), rights) == HKE_OK && rights.find(L"superadmin") != -1))
				{
					PrintUserCmdText(client, L"ERR: SRP accesses are only editable by admins!");
					return false;
				}

				return RemoveTagEntry(client, base->srp_tags, entry);
			}
			else if (type == L"blacklist")
			{
				return RemoveTagEntry(client, base->hostile_tags, entry);
			}
			else if (type == L"whitelist")
			{
				return RemoveTagEntry(client, base->ally_tags, entry);
			}
		}
		else if (entryType == L"name")
		{
			if (type == L"srp")
			{
				wstring rights;
				if (!(HkGetAdmin((const wchar_t*)Players.GetActiveCharacterName(client), rights) == HKE_OK && rights.find(L"superadmin") != -1))
				{
					PrintUserCmdText(client, L"ERR: SRP accesses are only editable by admins!");
					return false;
				}
				if (!base->srp_names.count(entry))
				{
					PrintUserCmdText(client, L"ERR No such name!");
					return false;
				}
				base->srp_names.erase(entry);
				PrintUserCmdText(client, L"OK!");
				return true;
			}
			else if (type == L"blacklist")
			{
				if (!base->hostile_names.count(entry))
				{
					PrintUserCmdText(client, L"ERR No such name!");
					return false;
				}
				base->hostile_names.erase(entry);
				PrintUserCmdText(client, L"OK!");
				return true;
			}
			else if (type == L"whitelist")
			{
				if (!base->ally_names.count(entry))
				{
					PrintUserCmdText(client, L"ERR No such name!");
					return false;
				}
				base->ally_names.erase(entry);
				PrintUserCmdText(client, L"OK!");
				return true;
			}
		}
		else if (entryType == L"faction")
		{
			auto affil = A.GetFirstAffiliationMatch(entry);
			if (!affil)
			{
				PrintUserCmdText(client, L"ERR invalid faction nickname!");
				return false;
			}

			if (type == L"srp")
			{
				wstring rights;
				if (!(HkGetAdmin((const wchar_t*)Players.GetActiveCharacterName(client), rights) == HKE_OK && rights.find(L"superadmin") != -1))
				{
					PrintUserCmdText(client, L"ERR: SRP accesses are only editable by admins!");
					return false;
				}
				if (!base->srp_factions.count(affil->id))
				{
					PrintUserCmdText(client, L"ERR No such faction!");
					return false;
				}
				base->srp_factions.erase(affil->id);
				PrintUserCmdText(client, L"OK removed %ls!", affil->factionname.c_str());
				return true;
			}
			else if (type == L"blacklist")
			{
				if (!base->hostile_factions.count(affil->id))
				{
					PrintUserCmdText(client, L"ERR No such faction!");
					return false;
				}
				base->hostile_factions.erase(affil->id);
				PrintUserCmdText(client, L"OK removed %ls!", affil->factionname.c_str());
				return true;
			}
			else if (type == L"whitelist")
			{
				if (!base->ally_factions.count(affil->id))
				{
					PrintUserCmdText(client, L"ERR No such faction!");
					return false;
				}
				base->ally_factions.erase(affil->id);
				PrintUserCmdText(client, L"OK removed %ls!", affil->factionname.c_str());
				return true;
			}
		}

		PrintUserCmdText(client, L"ERR incorrect parameters!");
		PrintUserCmdText(client, L"usage: /access add <tag|name|faction> <srp|whitelist|blacklist> <entry>");

		return false;
	}

	void BaseAccess(uint client, const wstring& params)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);

		if (!checkBaseAdminAccess(base, client))
		{
			return;
		}

		wstring cmd = ToLower(GetParam(params, ' ', 1));
		wstring param1 = ToLower(GetParam(params, ' ', 2));
		wstring param2 = ToLower(GetParam(params, ' ', 3));
		wstring param3 = GetParamToEnd(params, ' ', 4);

		if (cmd == L"list")
		{
			PrintAccesses(base, client, param1);
			return;
		}
		
		if (cmd == L"add")
		{
			if (AddAccess(base, client, param1, param2, param3))
			{
				base->SyncReputationForBase();
			}
			return;
		}
		
		if (cmd == L"remove")
		{
			if (RemoveAccess(base, client, param1, param2, param3))
			{
				base->SyncReputationForBase();
			}
			return;
		}

		if (cmd == L"clear")
		{
			if (ClearAccesses(base, client, param1))
			{
				base->SyncReputationForBase();
			}
			return;
		}

		PrintUserCmdText(client, L"ERR Invalid parameters");
		PrintUserCmdText(client, L"usage: /access <list|add|remove|clear>");
		return;
	}

	void BaseInfo(uint client, const wstring& args)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);

		if (!checkBaseAdminAccess(base, client))
		{
			return;
		}

		uint iPara = ToInt(GetParam(args, ' ', 2));
		const wstring& cmd = GetParam(args, ' ', 3);
		const wstring& msg = GetParamToEnd(args, ' ', 4);

		if (iPara > 0 && iPara <= MAX_PARAGRAPHS && cmd == L"a")
		{
			int length = base->infocard_para[iPara].length() + msg.length();
			if (length > MAX_CHARACTERS)
			{
				PrintUserCmdText(client, L"ERR Too many characters. Limit is %d", MAX_CHARACTERS);
				return;
			}

			base->infocard_para[iPara] += XMLText(msg);
			PrintUserCmdText(client, L"OK %d/%d characters used", length, MAX_CHARACTERS);

			// Update the infocard text.

			base->infocardHeader.clear();

			if (!base->infocard_para[1].empty())
			{
				base->infocardHeader = L"<TEXT>" + ReplaceStr(base->infocard_para[1], L"\n", L"</TEXT><PARA/><TEXT>") + L"</TEXT><PARA/><PARA/>";
			}

			base->infocard.clear();
			for (int i = 2; i <= MAX_PARAGRAPHS; i++)
			{
				wstring& wscXML = base->infocard_para[i];
				if (wscXML.length())
					base->infocard += L"<TEXT>" + ReplaceStr(wscXML, L"\n", L"</TEXT><PARA/><TEXT>") + L"</TEXT><PARA/><PARA/>";
			}

			base->Save();
			base->UpdateBaseInfoText();
		}
		else if (iPara > 0 && iPara <= MAX_PARAGRAPHS && cmd == L"d")
		{
			base->infocard_para[iPara] = L"";
			PrintUserCmdText(client, L"OK");

			base->infocardHeader.clear();

			if (!base->infocard_para[1].empty())
			{
				base->infocardHeader = L"<TEXT>" + ReplaceStr(base->infocard_para[1], L"\n", L"</TEXT><PARA/><TEXT>") + L"</TEXT><PARA/><PARA/>";
			}
			// Update the infocard text.
			base->infocard.clear();
			for (int i = 1; i <= MAX_PARAGRAPHS; i++)
			{
				wstring& wscXML = base->infocard_para[i];
				if (wscXML.length())
					base->infocard += L"<TEXT>" + ReplaceStr(wscXML, L"\n", L"</TEXT><PARA/><TEXT>") + L"</TEXT><PARA/><PARA/>";
			}

			base->Save();
			base->UpdateBaseInfoText();
		}
		else
		{
			PrintUserCmdText(client, L"ERR Invalid parameters");
			PrintUserCmdText(client, L"/base info <paragraph> <command> <text>");
			PrintUserCmdText(client, L"|  <paragraph> The paragraph number in the range 1-%d", MAX_PARAGRAPHS);
			PrintUserCmdText(client, L"|  <command> The command to perform on the paragraph, 'a' for append, 'd' for delete");
		}
	}

	void BaseDefenseMode(uint client, const wstring& args)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);

		if (!checkBaseAdminAccess(base, client))
		{
			return;
		}

		wstring wscMode = GetParam(args, ' ', 2);
		if (wscMode == L"1")
		{
			base->defense_mode = PlayerBase::DEFENSE_MODE::IFF;
		}
		else if (wscMode == L"2")
		{
			base->defense_mode = PlayerBase::DEFENSE_MODE::NODOCK_NEUTRAL;
		}
		else if (wscMode == L"3")
		{
			base->defense_mode = PlayerBase::DEFENSE_MODE::NODOCK_HOSTILE;
		}
		else
		{
			PrintUserCmdText(client, L"/base defensemode <mode>");
			PrintUserCmdText(client, L"|  <mode> = 1 - Logic: SRP Whitelist > Blacklist > IFF Standing. | Docking Rights: Anyone with good standing.");
			PrintUserCmdText(client, L"|  <mode> = 2 - Logic: Whitelist > No Dock. | Docking Rights: Whitelisted ships only.");
			PrintUserCmdText(client, L"|  <mode> = 3 - Logic: Whitelist > Hostile. | Docking Rights: Whitelisted ships only.");
			PrintUserCmdText(client, L"defensemode = %u", base->defense_mode);
			return;
		}

		PrintUserCmdText(client, L"OK defensemode = %u", base->defense_mode);

		base->Save();

		base->SyncReputationForBase();
	}

	void BaseBuildMod(uint client, const wstring& args)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);

		if (!checkBaseAdminAccess(base, client))
		{
			return;
		}

		wstring& cmd = GetParam(args, ' ', 1);
		wstring& moduleListArg = GetParam(args, ' ', 2);
		wstring& moduleNameNr = GetParamToEnd(args, ' ', 3);
		if (cmd.empty() || cmd == L"help")
		{
			PrintUserCmdText(client, L"/build list - lists available module lists");
			PrintUserCmdText(client, L"/build list <moduleList> - lists modules available on the selected module list");
			PrintUserCmdText(client, L"/build start <moduleList> <moduleName/Nr> - starts constructon of selected module");
			PrintUserCmdText(client, L"/build resume <moduleList> <moduleName/Nr> - resumes selected module construction");
			PrintUserCmdText(client, L"/build pause <moduleList> <moduleName/Nr> - pauses selected module construction");
			PrintUserCmdText(client, L"/build info <moduleList> <moduleName/Nr> - provides construction material info for selected module");
			PrintUserCmdText(client, L"For example, to build a Core Upgrade module, which is on the 'basic' build list");
			PrintUserCmdText(client, L"type: '/build start basic 1' or '/build start basic Core Upgrade'");
		}

		auto moduleList = buildingCraftLists.find(moduleListArg);

		if (cmd == L"list")
		{
			if (moduleList == buildingCraftLists.end())
			{
				if (!moduleListArg.empty())
				{
					PrintUserCmdText(client, L"ERR invalid module list selected, try one of the ones below.");
				}
				PrintUserCmdText(client, L"Available building lists:");
				for (const auto& buildType : buildingCraftLists)
				{
					PrintUserCmdText(client, L"|   %ls", buildType.c_str());
				}
			}
			else
			{
				PrintUserCmdText(client, L"Modules available in %ls category:", cmd.c_str());
				for (const auto& infoString : modules_recipe_map[*moduleList])
				{
					PrintUserCmdText(client, infoString);
				}
			}
			return;
		}
		else if (moduleList == buildingCraftLists.end())
		{
			PrintUserCmdText(client, L"ERR Invalid module list name, to get available module lists type '/build list', or for general help, '/build help'");
			return;
		}

		const RECIPE* buildRecipe = BuildModule::GetModuleRecipe(moduleNameNr, moduleListArg);
		if (!buildRecipe)
		{
			PrintUserCmdText(client, L"ERR Invalid module name or number, to get available modules in this list type /build list %ls", moduleListArg.c_str());
			return;
		}

		if (buildRecipe->shortcut_number == Module::TYPE_CORE)
		{
			buildRecipe = &recipeMap[core_upgrade_recipes[base->base_level]];
		}

		if (cmd == L"start")
		{
			if (buildRecipe->reqlevel > base->base_level)
			{
				PrintUserCmdText(client, L"ERR Module only available on bases of level %u and above.", buildRecipe->reqlevel);
				return;
			}
			if (buildRecipe->shortcut_number == Module::TYPE_CORE)
			{
				if (set_holiday_mode)
				{
					PrintUserCmdText(client, L"ERR Cannot upgrade base's core during holiday mode!");
					return;
				}
				if (!base->affiliation)
				{
					PrintUserCmdText(client, L"ERR Base needs to have a defined affiliation to upgrade its core!");
					return;
				}
				if (base->base_level >= 4)
				{
					PrintUserCmdText(client, L"ERR Upgrade not available");
					return;
				}
			}
			for (const auto& module : base->modules)
			{
				BuildModule* buildmod = dynamic_cast<BuildModule*>(module);
				if (buildmod && buildmod->active_recipe.nickname == buildRecipe->nickname && factoryNicknameToCraftTypeMap.count(buildmod->active_recipe.nickname))
				{
					PrintUserCmdText(client, L"ERR Only one factory of a given type per station allowed");
					return;
				}

				FactoryModule* facmod = dynamic_cast<FactoryModule*>(module);
				if (facmod && facmod->factoryNickname == buildRecipe->nickname)
				{
					PrintUserCmdText(client, L"ERR Only one factory of a given type per station allowed");
					return;
				}
			}

			if (buildRecipe->shortcut_number == Module::TYPE_CORE)
			{
				if (base->base_level >= 4)
				{
					PrintUserCmdText(client, L"ERR Upgrade not available");
					return;
				}
				if (base->modules.size() > (base->base_level * 3 + 1))
				{
					PrintUserCmdText(client, L"ERR Core upgrade already ongoing!");
					return;
				}
				PrintUserCmdText(client, L"Core upgrade started");
				base->modules.emplace_back(new BuildModule(base, buildRecipe));
				base->Save();
				return;
			}

			for (auto& modSlot : base->modules)
			{
				if (modSlot == nullptr)
				{
					modSlot = new BuildModule(base, buildRecipe);
					base->Save();
					PrintUserCmdText(client, L"Construction started");
					return;
				}
			}
			PrintUserCmdText(client, L"ERR No free module slots!");
		}
		else if (cmd == L"pause")
		{
			for (auto& iter = base->modules.begin(); iter != base->modules.end(); iter++)
			{
				BuildModule* buildmod = dynamic_cast<BuildModule*>(*iter);
				if (buildmod && buildmod->active_recipe.nickname == buildRecipe->nickname)
				{
					if (!buildmod->Paused)
					{
						buildmod->Paused = true;
						PrintUserCmdText(client, L"Module construction paused");
						base->Save();
					}
					else
					{
						PrintUserCmdText(client, L"ERR Module construction already paused");
					}
					return;
				}
			}
			PrintUserCmdText(client, L"ERR Selected module is not being built");
		}
		else if (cmd == L"resume")
		{
			for (auto& iter = base->modules.begin(); iter != base->modules.end(); iter++)
			{
				BuildModule* buildmod = dynamic_cast<BuildModule*>(*iter);
				if (buildmod && buildmod->active_recipe.nickname == buildRecipe->nickname)
				{
					if (buildmod->Paused)
					{
						buildmod->Paused = false;
						PrintUserCmdText(client, L"Module construction resumed");
						base->Save();
					}
					else
					{
						PrintUserCmdText(client, L"ERR Module construction already ongoing");
					}
					return;
				}
			}
			PrintUserCmdText(client, L"ERR Selected module is not being built");
		}
		else if (cmd == L"info")
		{
			PrintUserCmdText(client, L"Construction materials for %ls", buildRecipe->infotext.c_str());
			for (const auto& material : buildRecipe->consumed_items)
			{
				const GoodInfo* gi = GoodList::find_by_id(material.first);
				PrintUserCmdText(client, L"|   %ls x%u", HkGetWStringFromIDS(gi->iIDSName).c_str(), material.second);
			}
			if (buildRecipe->credit_cost)
			{
				PrintUserCmdText(client, L"|   $%u credits", buildRecipe->credit_cost);
			}
		}
		else
		{
			PrintUserCmdText(client, L"ERR Invalid module list name, for more information use /build help");
		}
	}

	void BaseSwapModule(uint client, const wstring& args)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);

		if (!checkBaseAdminAccess(base, client))
		{
			return;
		}

		const uint index1 = ToUInt(GetParam(args, ' ', 2));
		const uint index2 = ToUInt(GetParam(args, ' ', 3));
		if (index1 == 0 || index2 == 0
			|| index1 >= base->modules.size()
			|| index2 >= base->modules.size())
		{
			PrintUserCmdText(client, L"ERR Invalid module indexes");
			return;
		}
		if (index1 == index2)
		{
			PrintUserCmdText(client, L"ERR Can't swap a module with itself");
			return;
		}
		const uint coreUpgradeIndex = (base->base_level * 3) + 1;
		if (index1 == coreUpgradeIndex || index2 == coreUpgradeIndex)
		{
			PrintUserCmdText(client, L"ERR Can't swap core upgrade");
			return;
		}

		Module* tempModulePtr = base->modules[index1];
		base->modules[index1] = base->modules[index2];
		base->modules[index2] = tempModulePtr;
		base->Save();
	}

	void BaseBuildModDestroy(uint client, const wstring& args)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);

		if (!checkBaseAdminAccess(base, client))
		{
			return;
		}

		uint index = ToInt(GetParam(args, ' ', 1));
		if (index < 1 || index >= base->modules.size() || !base->modules[index])
		{
			PrintUserCmdText(client, L"ERR Module not found");
			return;
		}

		if (base->GetRemainingCargoSpace() < base->modules[index]->cargoSpace)
		{
			PrintUserCmdText(client, L"ERR Need %d free space to destroy this module", base->modules[index]->cargoSpace);
			return;
		}

		if (base->modules[index]->type == Module::TYPE_FACTORY)
		{
			FactoryModule* facMod = dynamic_cast<FactoryModule*>(base->modules[index]);
			for (auto& craftType : factoryNicknameToCraftTypeMap[facMod->factoryNickname])
			{
				base->availableCraftList.erase(craftType);
				base->craftTypeTofactoryModuleMap.erase(craftType);
			}
			delete base->modules[index];
			base->modules[index] = nullptr;
		}
		else if (base->modules[index]->type == Module::TYPE_BUILD)
		{
			BuildModule* bm = dynamic_cast<BuildModule*>(base->modules[index]);
			if (!bm)
			{
				PrintUserCmdText(client, L"ERR Impossible destroy error, contact staff!");
				return;
			}
			if (bm->active_recipe.shortcut_number == Module::TYPE_CORE)
			{
				delete base->modules[index];
				base->modules[index] = nullptr;
				base->modules.resize(base->base_level * 3 + 1);
			}
			else
			{
				delete base->modules[index];
				base->modules[index] = nullptr;
			}
		}
		else
		{
			delete base->modules[index];
			base->modules[index] = nullptr;
		}
		base->RecalculateCargoSpace();
		base->Save();
		PrintUserCmdText(client, L"OK Module destroyed");
	}

	void PrintCraftHelpMenu(uint client)
	{
		PrintUserCmdText(client, L"/craft stopall - stops all production on the base");
		PrintUserCmdText(client, L"/craft clearall - clears all production queues on the base");
		PrintUserCmdText(client, L"/craft list - show all available craft lists");
		PrintUserCmdText(client, L"/craft list <craftList/Nr> - list item recipes available for this crafting list");
		PrintUserCmdText(client, L"/craft start <craftList/Nr> <name/itemNr> - adds selected item into the crafting queue");
		PrintUserCmdText(client, L"/craft stop <craftList/Nr> <name/itemNr> - stops crafting of selected item");
		PrintUserCmdText(client, L"/craft pause <craftList/Nr> <name/itemNr> - pauses crafting of selected item");
		PrintUserCmdText(client, L"/craft resume <craftList/Nr> <name/itemNr> - resumes crafting of selected item");
		PrintUserCmdText(client, L"/craft info <craftList/Nr> <name/itemNr> - list materials necessary for selected item");
		PrintUserCmdText(client, L"For example, to craft a Docking Module, which is the first item on a 'dockmodule' craft list");
		PrintUserCmdText(client, L"type: '/craft start dockmodule 1' or '/craft start dockmodule Docking Module'");
	}

	void BaseFacMod(uint client, const wstring& args)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);

		if (!checkBaseAdminAccess(base, client))
		{
			return;
		}

		if (base->availableCraftList.empty())
		{
			PrintUserCmdText(client, L"ERR no factories found");
			return;
		}

		wstring& cmd = GetParam(args, ' ', 1);
		wstring& craftList = GetParam(args, ' ', 2);
		wstring& craftNameNr = GetParamToEnd(args, ' ', 3);

		if (cmd.empty() || cmd == L"help")
		{
			PrintCraftHelpMenu(client);
			return;
		}
		if (cmd == L"stopall")
		{
			FactoryModule::StopAllProduction(base);
			PrintUserCmdText(client, L"OK Factories stopped");
			return;
		}
		if (cmd == L"clearall")
		{
			FactoryModule::ClearAllProductionQueues(base);
			PrintUserCmdText(client, L"OK Craft queues cleared");
			return;
		}

		uint craftTypeNumber = ToUInt(craftList);
		if (craftTypeNumber && base->availableCraftList.size() >= craftTypeNumber)
		{
			craftList = *next(base->availableCraftList.begin(), craftTypeNumber - 1);
		}

		if (cmd == L"list")
		{
			if (!base->availableCraftList.count(craftList))
			{
				if (!craftList.empty())
				{
					PrintUserCmdText(client, L"ERR Invalid craft list selected, use one of the below:");
				}
				PrintUserCmdText(client, L"Available crafting lists:");
				uint counter = 1;
				for (const wstring& craftTypeName : base->availableCraftList)
				{
					PrintUserCmdText(client, L"%u. %ls", counter, craftTypeName.c_str());
					counter++;
				}
			}
			else
			{
				PrintUserCmdText(client, L"Available recipes for %ls crafting list:", craftList.c_str());
				for (wstring& infoLine : factory_recipe_map[craftList])
				{
					PrintUserCmdText(client, infoLine);
				}
			}
			return;
		}
		
		bool selectedValidCraftList = base->availableCraftList.count(craftList);
		const RECIPE* recipe = FactoryModule::GetFactoryProductRecipe(craftList, craftNameNr);
		
		if (cmd != L"stop" && cmd != L"start" && cmd != L"pause" && cmd != L"resume" && cmd != L"info")
		{
			PrintUserCmdText(client, L"ERR Incorrect command, use '/craft help' for more information.");
			return;
		}

		if (!selectedValidCraftList)
		{
			PrintUserCmdText(client, L"ERR Invalid or unavailable craft list, for a list of valid craft lists use '/craft list'");
			return;
		}
		else if (!recipe)
		{
			PrintUserCmdText(client, L"ERR Invalid recipe selected, for a list of valid recipes in selected craft list, use '/craft list %ls'", craftList.c_str());
			return;
		}

		if (cmd == L"info")
		{
			PrintUserCmdText(client, L"Construction materials for %ls:", recipe->infotext.c_str());
			for (const auto& item : recipe->consumed_items)
			{
				const GoodInfo* gi = GoodList::find_by_id(item.first);
				PrintUserCmdText(client, L"|   %ls x%u", HkGetWStringFromIDS(gi->iIDSName).c_str(), item.second);
			}
			for (const auto& materialList : recipe->dynamic_consumed_items)
			{
				bool isFirst = true;
				for (const auto& material : materialList)
				{
					const GoodInfo* gi = GoodList::find_by_id(material.first);
					if (isFirst)
					{
						isFirst = false;
						PrintUserCmdText(client, L"|   %ls x%u", HkGetWStringFromIDS(gi->iIDSName).c_str(), material.second);
					}
					else
					{
						PrintUserCmdText(client, L"|   or %ls x%u", HkGetWStringFromIDS(gi->iIDSName).c_str(), material.second);
					}
				}
			}
			for (const auto& materialList : recipe->dynamic_consumed_items_alt)
			{
				PrintUserCmdText(client, L"|   x%u of either:", materialList.sharedAmount);
				for (const auto material : materialList.items)
				{
					const GoodInfo* gi = GoodList::find_by_id(material);
					PrintUserCmdText(client, L"||   %ls", HkGetWStringFromIDS(gi->iIDSName).c_str());
				}
			}
			if (recipe->credit_cost)
			{
				PrintUserCmdText(client, L"|   $%u credits", recipe->credit_cost);
			}
			PrintUserCmdText(client, L"Produced goods:");
			for (const auto& product : recipe->produced_items)
			{
				const GoodInfo* gi = GoodList::find_by_id(product.first);
				if (gi->iType == GOODINFO_TYPE_SHIP)
				{
					gi = GoodList::find_by_id(gi->iHullGoodID);
				}
				PrintUserCmdText(client, L"|   %ls x%u", HkGetWStringFromIDS(gi->iIDSName).c_str(), product.second);
			}
			for (const auto& affiliation_product : recipe->affiliation_produced_items)
			{
				auto& affilIter = affiliation_product.find(base->affiliation);
				if (affilIter == affiliation_product.end())
				{
					continue;
				}

				const GoodInfo* gi = GoodList::find_by_id(affilIter->second.first);
				PrintUserCmdText(client, L"|   %ls x%u", HkGetWStringFromIDS(gi->iIDSName).c_str(), affilIter->second.second);
			}
			if (!recipe->catalyst_items.empty())
			{
				PrintUserCmdText(client, L"Production catalysts:");
				for (const auto& catalyst : recipe->catalyst_items)
				{
					const GoodInfo* gi = GoodList::find_by_id(catalyst.first);
					PrintUserCmdText(client, L"|   %ls x%u", HkGetWStringFromIDS(gi->iIDSName).c_str(), catalyst.second);
				}
			}
			if (!recipe->catalyst_workforce.empty())
			{
				PrintUserCmdText(client, L"Workers:");
				for (const auto& workforce : recipe->catalyst_workforce)
				{
					const GoodInfo* gi = GoodList::find_by_id(workforce.first);
					PrintUserCmdText(client, L"|   %ls x%u", HkGetWStringFromIDS(gi->iIDSName).c_str(), workforce.second);
				}
			}
			if (!recipe->affiliationBonus.empty())
			{
				if (!recipe->restricted)
				{
					PrintUserCmdText(client, L"IFF bonuses:");
					for (const auto& rep : recipe->affiliationBonus)
					{
					    if (rep.second <= 1.0f)
					    {
					    	PrintUserCmdText(client, L"|   %ls - %0.f%% construction materials discount",
							HkGetWStringFromIDS(Reputation::get_short_name(rep.first)).c_str(), (1.0f - rep.second) * 100);
					    }
					    else
					    {
                            PrintUserCmdText(client, L"|   %ls - %0.f%% construction materials penalty",
							HkGetWStringFromIDS(Reputation::get_short_name(rep.first)).c_str(), (rep.second - 1.0f) * 100);

                        }
					}
				}
				else
				{
					PrintUserCmdText(client, L"Available to:");
					for (const auto& rep : recipe->affiliationBonus)
					{
						if (rep.second != 1.0f)
						{
							PrintUserCmdText(client, L"|   %ls - %0.f%% construction materials discount",
								HkGetWStringFromIDS(Reputation::get_short_name(rep.first)).c_str(), (1.0f - rep.second) * 100);
						}
						else
						{
							PrintUserCmdText(client, L"|   %ls", HkGetWStringFromIDS(Reputation::get_short_name(rep.first)).c_str());
						}
					}
				}
			}
			return;
		}

		if (cmd == L"start")
		{
			if (recipe->reqlevel > base->base_level)
			{
				PrintUserCmdText(client, L"ERR core level %u required for this recipe", recipe->reqlevel);
				return;
			}
			if (!base->availableCraftList.count(recipe->craft_type))
			{
				PrintUserCmdText(client, L"ERR incorrect craftlist, for more information use /craft help");
				return;
			}
			if (recipe->restricted && !recipe->affiliationBonus.count(base->affiliation))
			{
				PrintUserCmdText(client, L"ERR This recipe is not available for this base's affiliation");
				return;
			}
			FactoryModule* factory = base->craftTypeTofactoryModuleMap[recipe->craft_type];
			if (!factory)
			{
				PrintUserCmdText(client, L"ERR Impossible factory error, contact staff");
				return;
			}
			if (factory->AddToQueue(recipe->nickname))
			{
				PrintUserCmdText(client, L"OK Item added to build queue");
				base->Save();
			}
			else
			{
				PrintUserCmdText(client, L"ERR This auto-looping recipe is already active");
			}
			return;
		}

		FactoryModule* factory;
		factory = FactoryModule::FindModuleByProductInProduction(base, recipe->nickname);
		if (!factory)
		{
			PrintUserCmdText(client, L"ERR item is not being produced");
			return;
		}

		if (cmd == L"stop")
		{
			factory->ClearQueue();
			factory->ClearRecipe();
			PrintUserCmdText(client, L"OK Factory stopped");
		}
		else if (cmd == L"pause")
		{
			if (factory->ToggleQueuePaused(true))
				PrintUserCmdText(client, L"OK Build queue paused");
			else
			{
				PrintUserCmdText(client, L"ERR Build queue is already paused");
				return;
			}
		}
		else if (cmd == L"resume")
		{
			if (factory->ToggleQueuePaused(false))
				PrintUserCmdText(client, L"OK Build queue resumed");
			else
			{
				PrintUserCmdText(client, L"ERR Build queue is already ongoing");
				return;
			}
		}
		base->Save();
	}

	void BaseDefMod(uint client, const wstring& args)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);

		if (!checkBaseAdminAccess(base, client))
		{
			return;
		}

		const wstring& cmd = GetParam(args, ' ', 2);
		if (cmd == L"list")
		{
			PrintUserCmdText(client, L"Defense Modules:");
			for (uint index = 0; index < base->modules.size(); index++)
			{
				Matrix transposedRotation = TransposeMatrix(base->rotation);
				DefenseModule* mod = dynamic_cast<DefenseModule*>(base->modules[index]);
				if (mod)
				{
					Vector relativePos = mod->pos;
					relativePos.x -= base->position.x;
					relativePos.y -= base->position.y;
					relativePos.z -= base->position.z;
					relativePos = VectorMatrixMultiply(relativePos, transposedRotation);

					PrintUserCmdText(client, L"Module %u: Position %0.0f %0.0f %0.0f",
						index, relativePos.x, relativePos.y, relativePos.z);
				}
			}
			PrintUserCmdText(client, L"OK");
		}
		else if (cmd == L"set")
		{
			uint index = ToInt(GetParam(args, ' ', 3));
			float x = (float)ToInt(GetParam(args, ' ', 4));
			float y = (float)ToInt(GetParam(args, ' ', 5));
			float z = (float)ToInt(GetParam(args, ' ', 6));
			if (index < base->modules.size() && base->modules[index])
			{
				DefenseModule* mod = dynamic_cast<DefenseModule*>(base->modules[index]);
				if (mod)
				{
					// Distance from base is limited to 5km
					Vector new_pos = base->position;
					TranslateX(new_pos, base->rotation, x);
					TranslateY(new_pos, base->rotation, y);
					TranslateZ(new_pos, base->rotation, z);
					if (HkDistance3D(new_pos, base->position) > 5000)
					{
						PrintUserCmdText(client, L"ERR Out of range");
						return;
					}

					mod->pos = new_pos;

					PrintUserCmdText(client, L"OK Module %u: Position %0.0f %0.0f %0.0f",
						index, x, y, z);
					base->Save();
					mod->Reset();
				}
				else
				{
					PrintUserCmdText(client, L"ERR Module not found");
				}
			}
			else
			{
				PrintUserCmdText(client, L"ERR Module not found");
			}
		}
		else
		{
			PrintUserCmdText(client, L"ERR Invalid parameters");
			PrintUserCmdText(client, L"/base defmod [list|set]");
			PrintUserCmdText(client, L"|  list - show position and orientations of this bases weapons platform");
			PrintUserCmdText(client, L"|  set - <index> <x> <y> <z> - set the position and orientation of the <index> weapons platform, where x,y,z is the relative position to the base");
		}
	}

	void Bank(uint client, const wstring& args)
	{
		PlayerBase *base = GetPlayerBaseForClient(client);

		if (!base)
		{
			PrintUserCmdText(client, L"ERR Not in player base");
			return;
		}

		const wstring& cmd = GetParam(args, ' ', 1);
		wstring& moneyStr = GetParam(args, ' ', 2);
		moneyStr = ReplaceStr(moneyStr, L".", L"");
		moneyStr = ReplaceStr(moneyStr, L",", L"");
		moneyStr = ReplaceStr(moneyStr, L"$", L"");
		int money = ToInt(moneyStr);

		wstring charname = (const wchar_t*)Players.GetActiveCharacterName(client);

		if (cmd == L"withdraw")
		{
			if (!clients[client].admin)
			{
				PrintUserCmdText(client, L"ERR Access denied");
				return;
			}

			float fValue;
			pub::Player::GetAssetValue(client, fValue);

			int iCurrMoney;
			pub::Player::InspectCash(client, iCurrMoney);

			if (fValue + money > 2100000000 || iCurrMoney + money > 2100000000)
			{
				PrintUserCmdText(client, L"ERR Ship asset value will be exceeded");
				return;
			}

			if (money > base->money || money < 0)
			{
				PrintUserCmdText(client, L"ERR Not enough or invalid credits");
				return;
			}

			pub::Player::AdjustCash(client, money);
			base->money -= money;
			base->Save();

			AddLog("NOTICE: Bank withdraw new_balance=%I64d money=%d base=%s charname=%s (%s)",
				base->money, money,
				wstos(base->basename).c_str(),
				wstos(charname).c_str(),
				wstos(HkGetAccountID(HkGetAccountByCharname(charname))).c_str());

			PrintUserCmdText(client, L"OK %u credits withdrawn", money);
		}
		else if (cmd == L"deposit")
		{
			int iCurrMoney;
			pub::Player::InspectCash(client, iCurrMoney);

			if (money > iCurrMoney || money < 0)
			{
				PrintUserCmdText(client, L"ERR Not enough or invalid credits");
				return;
			}

			pub::Player::AdjustCash(client, 0 - money);
			base->money += money;
			base->Save();

			AddLog("NOTICE: Bank deposit money=%d new_balance=%I64d base=%s charname=%s (%s)",
				money, base->money,
				wstos(base->basename).c_str(),
				wstos(charname).c_str(),
				wstos(HkGetAccountID(HkGetAccountByCharname(charname))).c_str());

			PrintUserCmdText(client, L"OK %u credits deposited", money);
		}
		else if (cmd == L"status")
		{
			PrintUserCmdText(client, L"OK current balance %I64d credits", base->money);
		}
		else
		{
			PrintUserCmdText(client, L"ERR Invalid parameters");
			PrintUserCmdText(client, L"/bank [deposit|withdraw|status] [credits]");
		}
	}

	void ShowShopHelp(uint client)
	{
		auto& cd = clients[client];
		wstring status = L"<RDL><PUSH/>";
		status += L"<TEXT>Available commands:</TEXT><PARA/>";
		if (cd.admin)
		{
			status += L"<TEXT>  /shop price [item] [buyprice] [sellprice]</TEXT><PARA/>";
			status += L"<TEXT>  /shop stock [item] [min stock] [max stock]</TEXT><PARA/>";
			status += L"<TEXT>  /shop remove [item]</TEXT><PARA/>";
			status += L"<TEXT>  /shop pin [item]</TEXT><PARA/>";
			status += L"<TEXT>  /shop unpin [item]</TEXT><PARA/>";
			status += L"<TEXT>  /shop addcargo [itemNickname]</TEXT><PARA/>";
		}
		status += L"<TEXT>  /shop [page]</TEXT><PARA/><TEXT>  /shop filter [substring] [page]</TEXT><PARA/><PARA/>";
		status += L"<POP/></RDL>";

		HkChangeIDSString(client, 500000, L"Shop Help");
		HkChangeIDSString(client, 500001, status);

		FmtStr caption(0, 0);
		caption.begin_mad_lib(500000);
		caption.end_mad_lib();

		FmtStr message(0, 0);
		message.begin_mad_lib(500001);
		message.end_mad_lib();

		Plugin_Communication(CUSTOM_POPUP_INIT, &client);

		cd.lastPopupWindowType = POPUPWINDOWTYPE::SHOP_HELP;

		HkChangeIDSString(client, 1244, L"BACK");
		HkChangeIDSString(client, 1245, L"CLOSE");
		pub::Player::PopUpDialog(client, caption, message, POPUPDIALOG_BUTTONS_LEFT_YES | POPUPDIALOG_BUTTONS_CENTER_OK);
	}

	void ShowShopStatus(uint client, PlayerBase* base, wstring substring, int page)
	{
		int matchingItems = 0;
		for (auto& i : base->market_items)
		{
			const GoodInfo* gi = GoodList::find_by_id(i.first);
			if (!gi)
				continue;

			wstring name = HkGetWStringFromIDS(gi->iIDSName);
			if (ToLower(name).find(substring) != std::wstring::npos)
			{
				matchingItems++;
			}
		}

		int pages = (matchingItems / ITEMS_PER_PAGE) + 1;
		if (page > pages)
		{
			page = pages;
		}
		else if (page < 1)
		{
			page = 1;
		}
		wchar_t buf[1000];
		_snwprintf(buf, sizeof(buf), L"Shop Management : Page %d/%d", page, pages);
		wstring title = buf;

		int start_item = ((page - 1) * ITEMS_PER_PAGE) + 1;
		int end_item = page * ITEMS_PER_PAGE;

		wstring status = L"<RDL><PUSH/>";
		int item = 1;
		int globalItem = 0;

		for (auto& i : base->market_items)
		{
			++globalItem;
			if (item > end_item)
				break;

			const GoodInfo* gi = GoodList::find_by_id(i.first);
			if (!gi)
			{
				item++;
				continue;
			}
			if (gi->iType == GOODINFO_TYPE_SHIP)
			{
				gi = GoodList::find_by_id(gi->iHullGoodID);
			}

			wstring name = HkGetWStringFromIDS(gi->iIDSName);
			if (ToLower(name).find(substring) != std::wstring::npos)
			{
				if (item < start_item)
				{
					item++;
					continue;
				}
				wchar_t buf[1000];
				_snwprintf(buf, sizeof(buf), L"<TEXT>  %02u:  %ux %s, buy $%u sell $%u, limits: %u/%u</TEXT><PARA/>",
					globalItem, i.second.quantity, HtmlEncode(name).c_str(),
					i.second.sellPrice, i.second.price, i.second.min_stock, i.second.max_stock);
				status += buf;
				item++;
			}
		}
		status += L"<POP/></RDL>";

		HkChangeIDSString(client, 500000, title);
		HkChangeIDSString(client, 500001, status);

		FmtStr caption(0, 0);
		caption.begin_mad_lib(500000);
		caption.end_mad_lib();

		FmtStr message(0, 0);
		message.begin_mad_lib(500001);
		message.end_mad_lib();

		uint renderedButtons = POPUPDIALOG_BUTTONS_CENTER_NO;
		HkChangeIDSString(client, 1245, L"CLOSE/HELP");
		if (page > 1)
		{
			HkChangeIDSString(client, 1244, L"PREV PAGE");
			renderedButtons |= POPUPDIALOG_BUTTONS_LEFT_YES;
		}
		if (page < pages)
		{
			HkChangeIDSString(client, 1570, L"NEXT PAGE");
			renderedButtons |= POPUPDIALOG_BUTTONS_RIGHT_LATER;
		}

		Plugin_Communication(CUSTOM_POPUP_INIT, &client);
		pub::Player::PopUpDialog(client, caption, message, renderedButtons);
		auto& clientData = clients[client];
		clientData.lastPopupPage = page;
		clientData.lastPopupWindowType = POPUPWINDOWTYPE::SHOP;
		clientData.lastShopFilterKeyword = substring;
	}

	void Shop(uint client, const wstring& args)
	{
		// Check that this player is in a player controlled base
		PlayerBase* base = GetPlayerBaseForClient(client);
		if (!base)
		{
			PrintUserCmdText(client, L"ERR Not in player base");
			return;
		}

		const wstring& cmd = GetParam(args, ' ', 1);

		auto& cd = clients[client];
		if (!cd.admin && (!cd.viewshop || (cmd == L"addcargo" || cmd == L"price" || cmd == L"pin" || cmd == L"unpin" || cmd == L"stock" || cmd == L"remove" || cmd == L"public" || cmd == L"private")))
		{
			PrintUserCmdText(client, L"ERROR: Access denied");
			return;
		}

		if (cmd == L"price")
		{
			int item = ToInt(GetParam(args, ' ', 2));
			int sellPrice = ToInt(GetParam(args, ' ', 3));
			int buyPrice = ToInt(GetParam(args, ' ', 4));

			if (sellPrice < 1 || sellPrice > 1'000'000'000
				|| buyPrice < 1 || buyPrice > 1'000'000'000)
			{
				PrintUserCmdText(client, L"ERR Prices not valid");
				return;
			}

			if (sellPrice > buyPrice)
			{
				PrintUserCmdText(client, L"ERR Base sell price must be greater or equal to buy price!");
				return;
			}

			int curr_item = 0;
			for (auto& i : base->market_items)
			{
				++curr_item;
				if (curr_item == item)
				{
					i.second.price = buyPrice;
					i.second.sellPrice = sellPrice;
					SendMarketGoodUpdated(base, i.first, i.second);
					base->Save();

					int page = ((curr_item + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE);
					ShowShopStatus(client, base, L"", page);
					PrintUserCmdText(client, L"OK");

					wstring charname = (const wchar_t*)Players.GetActiveCharacterName(client);
					const GoodInfo* gi = GoodList::find_by_id(i.first);
					BaseLogging("Base %s: player %s changed prices of %s to %d/%d", wstos(base->basename).c_str(), wstos(charname).c_str(), wstos(HkGetWStringFromIDS(gi->iIDSName)).c_str(), sellPrice, buyPrice);
					return;
				}
			}
			PrintUserCmdText(client, L"ERR Commodity does not exist");
		}
		else if (cmd == L"stock")
		{
			uint item = ToUInt(GetParam(args, ' ', 2));
			uint min_stock = ToUInt(GetParam(args, ' ', 3));
			uint max_stock = ToUInt(GetParam(args, ' ', 4));

			uint curr_item = 0;
			if (item == 0 || item > base->market_items.size())
			{
				PrintUserCmdText(client, L"ERR incorrect input! Provide id number of desired commodity!");
			}
			for (auto& i : base->market_items)
			{
				++curr_item;
				if (curr_item != item)
				{
					continue;
				}
				i.second.min_stock = min_stock;
				i.second.max_stock = max_stock;
				SendMarketGoodUpdated(base, i.first, i.second);
				base->Save();

				int page = ((curr_item + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE);
				ShowShopStatus(client, base, L"", page);
				PrintUserCmdText(client, L"OK");

				wstring charname = (const wchar_t*)Players.GetActiveCharacterName(client);
				const GoodInfo* gi = GoodList::find_by_id(i.first);
				BaseLogging("Base %s: player %s changed stock of %s to min:%u max:%u", wstos(base->basename).c_str(), wstos(charname).c_str(), wstos(HkGetWStringFromIDS(gi->iIDSName)).c_str(), min_stock, max_stock);
				return;
			}
			PrintUserCmdText(client, L"ERR Commodity does not exist");
		}
		else if (cmd == L"remove")
		{
			uint item = ToUInt(GetParam(args, ' ', 2));

			uint curr_item = 0;
			if (item == 0 || item > base->market_items.size())
			{
				PrintUserCmdText(client, L"ERR incorrect input! Provide id number of desired commodity!");
			}
			for (auto& i : base->market_items)
			{
				++curr_item;
				if (curr_item != item)
				{
					continue;
				}
				i.second.price = 0;
				i.second.quantity = 0;
				i.second.min_stock = 0;
				i.second.max_stock = 0;
				SendMarketGoodUpdated(base, i.first, i.second);
				if (base->pinned_market_items.count(i.first))
				{
					base->pinned_market_items.erase(i.first);
					base->pinned_item_updated = true;
				}
				base->market_items.erase(i.first);
				base->Save();

				int page = ((curr_item + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE);
				ShowShopStatus(client, base, L"", page);
				PrintUserCmdText(client, L"OK");
				return;
			}
			PrintUserCmdText(client, L"ERR Commodity does not exist");
		}
		else if (cmd == L"public" || cmd == L"private")
		{
			uint item = ToUInt(GetParam(args, ' ', 2));

			if (item < 1 || item > base->market_items.size())
			{
				PrintUserCmdText(client, L"ERR Commodity does not exist");
				return;
			}

			auto i = std::next(base->market_items.begin(), item - 1);

			if (cmd == L"public")
				i->second.is_public = true;
			else
				i->second.is_public = false;
			base->Save();

			int page = ((item + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE);
			ShowShopStatus(client, base, L"", page);
			PrintUserCmdText(client, L"OK");

		}
		else if (cmd == L"filter")
		{
			wstring substring = GetParam(args, ' ', 2);
			int page = ToInt(GetParam(args, ' ', 3));
			ShowShopStatus(client, base, ToLower(substring), page);
			PrintUserCmdText(client, L"OK");
		}
		else if (cmd == L"pin")
		{
			uint item = ToUInt(GetParam(args, ' ', 2));

			uint curr_item = 0;
			if (item == 0 || item > base->market_items.size())
			{
				PrintUserCmdText(client, L"ERR incorrect input! Provide id number of desired commodity!");
			}
			for (auto& i : base->market_items)
			{
				++curr_item;
				if (curr_item != item)
				{
					continue;
				}
				if (i.second.is_pinned)
				{
					PrintUserCmdText(client, L"Item already pinned!");
					return;
				}

				if (base->pinned_market_items.size() >= MAX_PINNED_ITEMS)
				{
					PrintUserCmdText(client, L"ERR Already at the limit of pinned items!");
					return;
				}
				i.second.is_pinned = true;
				base->pinned_market_items.insert(i.first);
				PrintUserCmdText(client, L"Item pinned!");
				base->Save();
				base->UpdateBaseInfoText();
				return;
			}
		}
		else if (cmd == L"unpin")
		{

			uint item = ToUInt(GetParam(args, ' ', 2));

			uint curr_item = 0;
			if (item == 0 || item > base->market_items.size())
			{
				PrintUserCmdText(client, L"ERR incorrect input! Provide id number of desired commodity!");
			}
			for (auto& i : base->market_items)
			{
				++curr_item;
				if (curr_item != item)
				{
					continue;
				}
				if (!i.second.is_pinned)
				{
					PrintUserCmdText(client, L"Item is not pinned!");
					return;
				}
				base->pinned_market_items.erase(i.first);
				i.second.is_pinned = false;
				PrintUserCmdText(client, L"Item unpinned!");
				base->Save();
				base->UpdateBaseInfoText();
				return;
			}
		}
		else if (cmd == L"addcargo")
		{
			uint goodId = CreateID(wstos(GetParam(args, ' ', 2)).c_str());
			auto gi = GoodList_get()->find_by_id(goodId);
			if (!gi)
			{
				PrintUserCmdText(client, L"ERROR: invalid commodity nickname");
				return;
			}
			base->market_items[goodId];
			ShowShopStatus(client, base, L"", 0);
			PrintUserCmdText(client, L"OK");
		}
		else
		{
			int page = ToInt(GetParam(args, ' ', 1));
			ShowShopStatus(client, base, L"", page);
			PrintUserCmdText(client, L"OK");
		}
	}

	void GetNecessitiesStatus(uint client, const wstring& args)
	{
		// Check that this player is in a player controlled base
		PlayerBase* base = GetPlayerBaseForClient(client);
		if (!base)
		{
			PrintUserCmdText(client, L"ERR Not in player base");
			return;
		}

		auto& cd = clients[client];
		if (!cd.admin && !cd.viewshop)
		{
			PrintUserCmdText(client, L"ERR Access denied");
			return;
		}

		uint crewItemCount = base->HasMarketItem(set_base_crew_type);
		uint crewItemNeed = base->base_level * 200;
		if (crewItemCount < crewItemNeed)
		{
			PrintUserCmdText(client, L"WARNING, CREW COUNT TOO LOW");
		}
		PrintUserCmdText(client, L"Crew: %u onboard", crewItemCount);

		uint populationCount = 0;
		for (uint hash : humanCargoList)
		{
			populationCount += base->HasMarketItem(hash);
		}
		PrintUserCmdText(client, L"Total population: %u onboard", populationCount);

		PrintUserCmdText(client, L"Crew supplies:");
		for (uint item : set_base_crew_consumption_items)
		{
			const GoodInfo* gi = GoodList::find_by_id(item);
			if (!gi)
			{
				continue;
			}
			if (base->market_items.count(item))
			{
				PrintUserCmdText(client, L"|    %s: %u/%u", HkGetWStringFromIDS(gi->iIDSName).c_str(), base->HasMarketItem(item), base->market_items[item].max_stock);
			}
			else
			{
				PrintUserCmdText(client, L"|    %s: %u/0", HkGetWStringFromIDS(gi->iIDSName).c_str(), base->HasMarketItem(item));
			}
		}

		uint foodCount = 0;
		uint maxFoodCount = 0;
		for (uint item : set_base_crew_food_items)
		{
			foodCount += base->HasMarketItem(item);
			if (base->market_items.count(item))
				maxFoodCount += base->market_items[item].max_stock;
		}
		PrintUserCmdText(client, L"|    Food: %u/%u", foodCount, maxFoodCount);

		PrintUserCmdText(client, L"Repair materials:");
		for (auto& i : set_base_repair_items)
		{
			const GoodInfo* gi = GoodList::find_by_id(i.good);
			if (!gi)
			{
				continue;
			}
			if (base->market_items.count(i.good))
			{
				PrintUserCmdText(client, L"|    %s: %u/%u", HkGetWStringFromIDS(gi->iIDSName).c_str(), base->HasMarketItem(i.good), base->market_items[i.good].max_stock);
			}
			else
			{
				PrintUserCmdText(client, L"|    %s: %u/0", HkGetWStringFromIDS(gi->iIDSName).c_str(), base->HasMarketItem(i.good));
			}
		}
	}

	bool CheckSolarDistances(uint client, uint systemID, Vector pos)
	{
		// Other POB Check
		if (minOtherPOBDistance > 0)
		{
			for (const auto& base : player_bases)
			{
				// do not check POBs in a different system
				if (base.second->system != systemID
					|| (base.second->position.x == pos.x
						&& base.second->position.y == pos.y
						&& base.second->position.z == pos.z))
				{
					continue;
				}

				float distance = HkDistance3D(pos, base.second->position);
				if (distance < minOtherPOBDistance)
				{
					if (client)
					{
						PrintUserCmdText(client, L"%ls is too close! Current: %um, Minimum: %um", base.second->basename.c_str(), static_cast<uint>(distance), static_cast<uint>(minOtherPOBDistance));
					}
					else
					{
						ConPrint(L"Base is too close to another Player Base, distance %um, min %um, name %ls", static_cast<uint>(distance), static_cast<uint>(minOtherPOBDistance), base.second->basename.c_str());
					}
					return false;
				}
			}
		}

		// Mining Zone Check
		CmnAsteroid::CAsteroidSystem* asteroidSystem = CmnAsteroid::Find(systemID);
		if (asteroidSystem && minMiningDistance > 0)
		{
			for (CmnAsteroid::CAsteroidField* cfield = asteroidSystem->FindFirst(); cfield; cfield = asteroidSystem->FindNext())
			{
				auto& zone = cfield->zone;
				if (!zone->lootableZone)
				{
					continue;
				}

				if (lowTierMiningCommoditiesSet.count(zone->lootableZone->dynamic_loot_commodity))
				{
					continue;
				}

				float distance = pub::Zone::GetDistance(zone->iZoneID, pos); // returns distance from the nearest point at the edge of the zone, value is negative if you're within the zone.

				if (distance <= 0)
				{
					if (client)
					{
						PrintUserCmdText(client, L"You can't deploy inside a mining field!");
					}
					else
					{
						if (zone->idsName)
						{
							ConPrint(L"Base is within the %ls mining zone", HkGetWStringFromIDS(zone->idsName).c_str());
						}
						else
						{
							const GoodInfo* gi = GoodList::find_by_id(zone->lootableZone->dynamic_loot_commodity);
							ConPrint(L"Base is within the unnamed %ls mining zone", HkGetWStringFromIDS(gi->iIDSName).c_str());
						}
					}
					return false;
				}
				else if (distance < minMiningDistance)
				{
					if (zone->idsName)
					{
						if (client)
						{
							PrintUserCmdText(client, L"Distance to %ls too close, Current: %um, Minimum: %um.", HkGetWStringFromIDS(zone->idsName).c_str(), static_cast<uint>(distance), static_cast<uint>(minMiningDistance));
						}
						else
						{
							ConPrint(L"Base is too close to %ls, distance: %um.", HkGetWStringFromIDS(zone->idsName).c_str(), static_cast<uint>(distance));
						}
					}
					else
					{
						const GoodInfo* gi = GoodList::find_by_id(zone->lootableZone->dynamic_loot_commodity);
						if (client)
						{
							PrintUserCmdText(client, L"Distance to unnamed %ls field too close, minimum distance: %um.", HkGetWStringFromIDS(gi->iIDSName).c_str(), static_cast<uint>(minMiningDistance));
						}
						else
						{
							ConPrint(L"Base is too close to unnamed %ls field, distance: %um.", HkGetWStringFromIDS(gi->iIDSName).c_str(), static_cast<uint>(distance));
						}
					}
					return false;
				}
			}
		}

		// Solars
		bool foundSystemMatch = false;
		for (CSolar* solar = dynamic_cast<CSolar*>(CObject::FindFirst(CObject::CSOLAR_OBJECT)); solar;
			solar = dynamic_cast<CSolar*>(CObject::FindNext()))
		{
			//solars are iterated on per system, we can stop once we're done scanning the last solar in the system we're looking for.
			if (solar->system != systemID)
			{
				if (foundSystemMatch)
					break;
				continue;
			}
			else
			{
				foundSystemMatch = true;
			}

			float distance = HkDistance3D(solar->get_position(), pos);
			switch (solar->type)
			{
				case Planet:
				case Moon:
				{
					if (distance < (minPlanetDistance + solar->get_radius())) // In case of planets, we only care about distance from actual surface, since it can vary wildly
					{
						uint idsName = solar->get_name();
						if (!idsName) idsName = solar->get_archetype()->iIdsName;
						if (client)
						{
							PrintUserCmdText(client, L"%ls too close. Current: %um, Minimum distance: %um", HkGetWStringFromIDS(idsName).c_str(), static_cast<uint>(distance - solar->get_radius()), static_cast<uint>(minPlanetDistance));
						}
						else
						{
							ConPrint(L"Base too close to %ls, distance: %um", HkGetWStringFromIDS(idsName).c_str(), static_cast<uint>(distance - solar->get_radius()));
						}
						return false;
					}
					break;
				}
				case DockingRing:
				case Station:
				{
					if (distance < minStationDistance)
					{
						uint idsName = solar->get_name();
						if (!idsName) idsName = solar->get_archetype()->iIdsName;
						if (client)
						{
							PrintUserCmdText(client, L"%ls too close. Current: %um, Minimum distance: %um", HkGetWStringFromIDS(idsName).c_str(), static_cast<uint>(distance), static_cast<uint>(minStationDistance));
						}
						else
						{
							ConPrint(L"Base too close to %ls, Current: %um", HkGetWStringFromIDS(idsName).c_str(), static_cast<uint>(distance));
						}
						return false;
					}
					break;
				}
				case TradelaneRing:
				{
					if (distance < minLaneDistance)
					{
						if (client)
						{
							PrintUserCmdText(client, L"Trade Lane Ring is too close. Current: %um, Minimum distance: %um", static_cast<uint>(distance), static_cast<uint>(minLaneDistance));
						}
						else
						{
							ConPrint(L"Trade Lane too close, distance: %um", static_cast<uint>(distance));
						}
						return false;
					}
					break;
				}
				case JumpGate:
				case JumpHole:
				{
					if (distance < minJumpDistance)
					{
						uint idsName = solar->get_name();
						if (!idsName) idsName = solar->get_archetype()->iIdsName;

						if (client)
						{
							PrintUserCmdText(client, L"%ls too close. Current: %um, Minimum distance: %um", HkGetWStringFromIDS(idsName).c_str(), static_cast<uint>(distance), static_cast<uint>(minJumpDistance));
						}
						else
						{
							ConPrint(L"Base too close to %ls, distance: %um", HkGetWStringFromIDS(idsName).c_str(), static_cast<uint>(distance));
						}
						return false;
					}
					break;
				}
				case Satellite:
				case WeaponPlatform:
				case DestructibleDepot:
				case MissionSatellite:
				{
					if (distance < minDistanceMisc)
					{
						uint idsName = solar->get_name();
						if (!idsName) idsName = solar->get_archetype()->iIdsName;
						if (client)
						{
							PrintUserCmdText(client, L"%ls too close. Current: %um, Minimum distance: %um", HkGetWStringFromIDS(idsName).c_str(), static_cast<uint>(distance), static_cast<uint>(minDistanceMisc));
						}
						else
						{
							ConPrint(L"Base too close to %ls, distance: %um", HkGetWStringFromIDS(idsName).c_str(), static_cast<uint>(distance));
						}
						return false;
					}
					break;
				}
				case NonTargetable:
				{

					if (distance < minDistanceMisc)
					{
						uint idsName = solar->get_name();
						if (!idsName) idsName = solar->get_archetype()->iIdsName;
						if (client)
						{
							PrintUserCmdText(client, L"Untargetable object too close. Current: %um, Minimum distance: %um", static_cast<uint>(distance), static_cast<uint>(minDistanceMisc));
						}
						else
						{
							ConPrint(L"Base too close to an untargetable object, distance: %um", static_cast<uint>(distance));
						}
						return false;
					}
					break;
				}
			}
		}

		return true;
	}

	void BaseTestDeploy(uint client, const wstring& args)
	{
		if (!enableDistanceCheck)
		{
			PrintUserCmdText(client, L"Bases can be deployed anywhere!");
			return;
		}

		uint systemId;
		pub::Player::GetSystem(client, systemId);
		if (bannedSystemList.count(systemId))
		{
			PrintUserCmdText(client, L"ERR Deploying base in this system is not possible");
			return;
		}

		uint ship;
		pub::Player::GetShip(client, ship);
		if (!ship)
		{
			PrintUserCmdText(client, L"ERR Not in space");
			return;
		}

		// If the ship is moving, abort the processing.
		Vector dir1;
		Vector dir2;
		pub::SpaceObj::GetMotion(ship, dir1, dir2);
		if (dir1.x > 5 || dir1.y > 5 || dir1.z > 5)
		{
			PrintUserCmdText(client, L"ERR Ship is moving");
			return;
		}

		Vector position;
		Matrix rotation;
		pub::SpaceObj::GetLocation(ship, position, rotation);
		Rotate180(rotation);
		TranslateX(position, rotation, 1000);
		auto& cooldown = deploymentCooldownMap.find(client);

		uint currTime = static_cast<uint>(time(0));

		if (cooldown != deploymentCooldownMap.end() && currTime < cooldown->second)
		{
			PrintUserCmdText(client, L"Command still on cooldown, %us remaining.", cooldown->second - currTime);
			return;
		}
		else
		{
			deploymentCooldownMap[client] = (uint)time(0) + deploymentCooldownDuration;
		}

		if (!CheckSolarDistances(client, systemId, position))
		{
			PrintUserCmdText(client, L"Base cannot be deployed here");
		}
		else
		{
			PrintUserCmdText(client, L"Base can be deployed at current location. Use /pos to record it for later use.");
		}
	}

	void BaseDeploy(uint client, const wstring& args)
	{
		if (set_holiday_mode)
		{
			PrintUserCmdText(client, L"ERR Cannot create bases when holiday mode is active");
			return;
		}


		// Abort processing if this is not a "heavy lifter"
		uint shiparch;
		pub::Player::GetShipID(client, shiparch);
		if (set_construction_shiparch != 0 && shiparch != set_construction_shiparch)
		{
			PrintUserCmdText(client, L"ERR Need construction ship");
			return;
		}

		uint systemId;
		pub::Player::GetSystem(client, systemId);
		if (bannedSystemList.count(systemId))
		{
			PrintUserCmdText(client, L"ERR Deploying base in this system is not possible");
			return;
		}

		uint ship;
		pub::Player::GetShip(client, ship);
		if (!ship)
		{
			PrintUserCmdText(client, L"ERR Not in space");
			return;
		}

		// If the ship is moving, abort the processing.
		Vector dir1;
		Vector dir2;
		pub::SpaceObj::GetMotion(ship, dir1, dir2);
		if (dir1.x > 5 || dir1.y > 5 || dir1.z > 5)
		{
			PrintUserCmdText(client, L"ERR Ship is moving");
			return;
		}

		wstring password = GetParam(args, ' ', 2);
		if (!password.length())
		{
			PrintUserCmdText(client, L"ERR No password");
			PrintUserCmdText(client, L"Usage: /base deploy <password> <name>");
			return;
		}
		wstring basename = GetParamToEnd(args, ' ', 3);
		if (!basename.length())
		{
			PrintUserCmdText(client, L"ERR No base name");
			PrintUserCmdText(client, L"Usage: /base deploy <password> <name>");
			return;
		}

		// Check for conflicting base name
		if (GetPlayerBase(CreateID(PlayerBase::CreateBaseNickname(wstos(basename)).c_str())))
		{
			PrintUserCmdText(client, L"ERR Base name already exists");
			return;
		}

		// Check that the ship has the requires commodities and credits.
		if (construction_credit_cost)
		{
			int cash;
			pub::Player::InspectCash(client, cash);
			if (cash < construction_credit_cost)
			{
				PrintUserCmdText(client, L"ERR Insufficient money, %u needed", construction_credit_cost);
				return;
			}
		}

		int hold_size;
		list<CARGO_INFO> cargo;
		HkEnumCargo((const wchar_t*)Players.GetActiveCharacterName(client), cargo, hold_size);
		for (auto& i : construction_items)
		{
			bool material_available = false;
			uint good = i.first;
			uint quantity = i.second;
			for (CARGO_INFO& ci : cargo)
			{
				if (ci.iArchID == good && ci.iCount >= static_cast<int>(quantity))
				{
					material_available = true;
					break;
				}
			}
			if (material_available == false)
			{
				PrintUserCmdText(client, L"ERR Construction failed due to insufficient raw material.");
				for (auto& i : construction_items)
				{
					const GoodInfo* gi = GoodList::find_by_id(i.first);
					if (gi)
					{
						PrintUserCmdText(client, L"|  %ux %s", i.second, HkGetWStringFromIDS(gi->iIDSName).c_str());
					}
				}
				return;
			}
		}
		//passed cargo check, now make the distance check

		Vector position;
		Matrix rotation;
		pub::SpaceObj::GetLocation(ship, position, rotation);
		Rotate180(rotation);
		TranslateZ(position, rotation, 1000);
		if (enableDistanceCheck)
		{
			auto& cooldown = deploymentCooldownMap.find(client);
			uint currTime = static_cast<uint>(time(0));
			if (cooldown != deploymentCooldownMap.end() && currTime < cooldown->second)
			{
				PrintUserCmdText(client, L"Command still on cooldown, %us remaining.", cooldown->second - currTime);
				return;
			}
			else
			{
				deploymentCooldownMap[client] = currTime + deploymentCooldownDuration;
			}

			if (!CheckSolarDistances(client, systemId, position))
			{
				PrintUserCmdText(client, L"ERR Deployment failed.");
				return;
			}
		}

		//actually remove the cargo and credits.
		for (auto& i : construction_items)
		{
			uint good = i.first;
			uint quantity = i.second;
			for (auto& ci : cargo)
			{
				if (ci.iArchID == good)
				{
					pub::Player::RemoveCargo(client, ci.iID, quantity);
					break;
				}
			}
		}

		pub::Player::AdjustCash(client, -construction_credit_cost);

		wstring charname = (const wchar_t*)Players.GetActiveCharacterName(client);
		AddLog("NOTICE: Base created %s by %s (%s)",
			wstos(basename).c_str(),
			wstos(charname).c_str(),
			wstos(HkGetAccountID(HkGetAccountByCharname(charname))).c_str());

		PlayerBase* newbase = new PlayerBase(client, password, basename);
		player_bases[newbase->base] = newbase;
		newbase->basetype = "legacy";
		newbase->archetype = &mapArchs[newbase->basetype];
		newbase->basesolar = "legacy";
		newbase->baseloadout = "legacy";
		newbase->defense_mode = PlayerBase::DEFENSE_MODE::NODOCK_NEUTRAL;
		newbase->isCrewSupplied = true;

		newbase->invulnerable = newbase->archetype->invulnerable;
		newbase->logic = newbase->archetype->logic;

		newbase->Spawn();
		newbase->Save();

		PrintUserCmdText(client, L"OK: Base deployed");
		PrintUserCmdText(client, L"Default administration password is %s", password.c_str());
	}

	void SetPrefFood(uint client, const wstring& cmd)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);
		if (!base)
		{
			PrintUserCmdText(client, L"ERR Not in player base");
			return;
		}

		if (!checkBaseAdminAccess(base, client))
		{
			return;
		}

		uint input = ToUInt(GetParam(cmd, ' ', 2));

		if (!input || set_base_crew_food_items.size() < input)
		{
			PrintUserCmdText(client, L"ERR invalid input. Command usage: /base setfood <nr>");
			if (base->preferred_food)
			{
				const GoodInfo* gi = GoodList::find_by_id(base->preferred_food);
				PrintUserCmdText(client, L"Current selection: %ls", HkGetWStringFromIDS(gi->iIDSName).c_str());
			}
			else
			{
				PrintUserCmdText(client, L"Current selection: none");
			}
			PrintUserCmdText(client, L"Available food types:");
			int counter = 1;
			for (auto foodID : set_base_crew_food_items)
			{
				const GoodInfo* gi = GoodList::find_by_id(foodID);
				PrintUserCmdText(client, L"%d - %ls", counter, HkGetWStringFromIDS(gi->iIDSName).c_str());
				counter++;
			}
		}
		else
		{
			base->preferred_food = set_base_crew_food_items.at(input-1);

			const GoodInfo* gi = GoodList::find_by_id(base->preferred_food);
			PrintUserCmdText(client, L"Preferred food set to %ls!", HkGetWStringFromIDS(gi->iIDSName).c_str());
			base->Save();
		}
	}

	void BaseSetVulnerabilityWindow(uint client, const wstring& cmd)
	{
		PlayerBase* base = GetPlayerBaseForClient(client);
		if (!base)
		{
			PrintUserCmdText(client, L"ERR Not in player base");
			return;
		}

		if (!checkBaseAdminAccess(base, client))
		{
			return;
		}

		uint currTime = (uint)time(nullptr);

		if (base->lastVulnerabilityWindowChange + vulnerability_window_change_cooldown > currTime )
		{
			PrintUserCmdText(client, L"ERR Can only change vulnerability windows once every %u days, %u days left", vulnerability_window_change_cooldown / (3600 * 24), 1 + ((base->lastVulnerabilityWindowChange + vulnerability_window_change_cooldown - currTime) / (3600 * 24)));
			return;
		}
		wstring param1Str = GetParam(cmd, ' ', 2);
		wstring param2Str = GetParam(cmd, ' ', 3);

		if (param1Str.empty() || (!single_vulnerability_window && param2Str.empty()))
		{
			PrintUserCmdText(client, L"ERR No parameter(s) set");
			return;
		}

		int param1 = ToInt(param1Str);
		int param2 = ToInt(param2Str);

		if (itows(param1) != param1Str
			|| (!single_vulnerability_window && itows(param2) != param2Str))
		{
			PrintUserCmdText(client, L"ERR Provided parameter is not a number!");
			if (single_vulnerability_window)
			{
				PrintUserCmdText(client, L"Example input: /base setshield 15");
			}
			else
			{
				PrintUserCmdText(client, L"Example input: /base setshield 15 23");
			}
			return;
		}

		if (param1 < 0 || param1 > 23
			|| (!single_vulnerability_window && (param2 < 0 || param2 > 23)))
		{
			PrintUserCmdText(client, L"ERR Vulnerability windows can only be set to full hour values between 0 and 23");
			return;
		}

		if (param1 < 9 && static_cast<float>(param1) + (vulnerability_window_length / 60.0f) > 9.0f)
		{
			PrintUserCmdText(client, L"ERR You can't set the vulnerability window crossing the server restart!");
			return;
		}

		int vulnerabilityWindowOneStart = param1 * 60; // minutes
		int vulnerabilityWindowOneEnd = (vulnerabilityWindowOneStart + vulnerability_window_length) % (60 * 24);
		int vulnerabilityWindowTwoStart = param2 * 60;
		int vulnerabilityWindowTwoEnd = (vulnerabilityWindowTwoStart + vulnerability_window_length) % (60 * 24);

		if (single_vulnerability_window)
		{
			base->vulnerabilityWindow1 = { vulnerabilityWindowOneStart, vulnerabilityWindowOneEnd };
			base->lastVulnerabilityWindowChange = currTime;
			PrintUserCmdText(client, L"OK Vulnerability window set.");
			base->UpdateBaseInfoText();
			return;
		}

		if ((vulnerabilityWindowOneStart < vulnerabilityWindowTwoStart && abs(vulnerabilityWindowOneEnd - vulnerabilityWindowTwoStart) < vulnerability_window_minimal_spread)
			|| (vulnerabilityWindowOneStart > vulnerabilityWindowTwoStart && abs(vulnerabilityWindowOneStart - vulnerabilityWindowTwoEnd) < vulnerability_window_minimal_spread))
		{
			PrintUserCmdText(client, L"ERR Vulnerability windows must be at least %u hours apart!", vulnerability_window_minimal_spread / 60);
			return;
		}

		base->vulnerabilityWindow1 = { vulnerabilityWindowOneStart, vulnerabilityWindowOneEnd % (60 * 24)};
		if (!single_vulnerability_window)
		{
			base->vulnerabilityWindow2 = { vulnerabilityWindowTwoStart, vulnerabilityWindowTwoEnd % (60 * 24) };
		}
		base->lastVulnerabilityWindowChange = currTime;

		base->UpdateBaseInfoText();
		PrintUserCmdText(client, L"OK Vulnerability window set.");
	}
}
