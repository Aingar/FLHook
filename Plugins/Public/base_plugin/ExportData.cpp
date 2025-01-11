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
#include <sstream>
#include <fstream>
#include "Main.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>
#include "minijson_writer.hpp"

namespace pt = boost::posix_time;

void ExportData::ToHTML()
{
	FILE* file = fopen(set_status_path_html.c_str(), "w");
	if (file)
	{
		fprintf(file, "<html>\n<head><title>Player Base Status</title><style type=text/css>\n");
		fprintf(file, ".ColumnH {FONT-FAMILY: Tahoma; FONT-SIZE: 10pt;  TEXT-ALIGN: left; COLOR: #000000; BACKGROUND: #ECE9D8;}\n");
		fprintf(file, ".Column0 {FONT-FAMILY: Tahoma; FONT-SIZE: 10pt;  TEXT-ALIGN: left; COLOR: #000000; BACKGROUND: #FFFFFF;}\n");
		fprintf(file, "</style></head><body>\n\n");

		fprintf(file, "<table width=\"90%%\" border=\"1\" cellspacing=\"0\" cellpadding=\"2\">\n");

		fprintf(file, "<tr>");
		fprintf(file, "<th class=\"ColumnH\">Base Name</th>");
		fprintf(file, "<th class=\"ColumnH\">Base Affiliation</th>");
		fprintf(file, "<th class=\"ColumnH\">Health (%%)</th>");
		fprintf(file, "<th class=\"ColumnH\">Shield Status</th>");
		fprintf(file, "<th class=\"ColumnH\">Money</th>");
		fprintf(file, "<th class=\"ColumnH\">Description</th>");
		fprintf(file, "<th class=\"ColumnH\">Core Level</th>");
		fprintf(file, "<th class=\"ColumnH\">Defense Mode</th>");
		fprintf(file, "<th class=\"ColumnH\">System</th>");
		fprintf(file, "<th class=\"ColumnH\">Position</th>");
		fprintf(file, "<th class=\"ColumnH\">Whitelisted Tags</th>");
		fprintf(file, "<th class=\"ColumnH\">Blacklisted Tags</th>");
		fprintf(file, "</tr>\n\n");

		for (auto& iter : player_bases)
		{
			PlayerBase* base = iter.second;

			//do nothing if it's something we don't care about
			if (!base->archetype || base->archetype->display == false)
			{
				continue;
			}
			
			wstring theaffiliation = HtmlEncode(HkGetWStringFromIDS(Reputation::get_name(base->affiliation)));

			fprintf(file, "<tr>");
			fprintf(file, "<td class=\"column0\">%s</td>", wstos(HtmlEncode(base->basename)).c_str());
			fprintf(file, "<td class=\"column0\">%s</td>", wstos(HtmlEncode(theaffiliation)).c_str());
			fprintf(file, "<td class=\"column0\">%0.0f</td>", 100 * (base->base_health / base->max_base_health));
			fprintf(file, "<td class=\"column0\">%s</td>", base->isShieldOn ? "On" : "Off");
			fprintf(file, "<td class=\"column0\">%I64d</td>", base->money);


			string desc;
			for (int i = 1; i <= MAX_PARAGRAPHS; i++)
			{
				desc += "<p>";
				desc += wstos(HtmlEncode(base->infocard_para[i]));
				desc += "</p>";
			}
			fprintf(file, "<td class=\"column0\">%s</td>", desc.c_str());

			// the new fields begin here
			fprintf(file, "<td class=\"column0\">%d</td>", base->base_level);
			fprintf(file, "<td class=\"column0\">%d</td>", (int)base->defense_mode);

			const Universe::ISystem* iSys = Universe::get_system(base->system);
			wstring wscSysName = HkGetWStringFromIDS(iSys->strid_name);
			fprintf(file, "<td class=\"column0\">%s</td>", wstos(wscSysName).c_str());
			fprintf(file, "<td class=\"column0\">%0.0f %0.0f %0.0f</td>", base->position.x, base->position.y, base->position.z);

			string thewhitelist;
			for (auto& i : base->ally_tags)
			{
				thewhitelist.append(wstos(i).c_str());
				thewhitelist.append("\n");
			}

			fprintf(file, "<td class=\"column0\">%s</td>", thewhitelist.c_str());

			string theblacklist;
			for (auto& i : base->hostile_names)
			{
				theblacklist.append(wstos(i).c_str());
				theblacklist.append("\n");
			}

			fprintf(file, "<td class=\"column0\">%s</td>", theblacklist.c_str());

			fprintf(file, "</tr>\n");
			
		}


		fprintf(file, "</table>\n\n</body></html>\n");
		fclose(file);
	}
}

char* VectorToString(Vector& pos)
{
	static char buf[50];
	_snprintf(buf, sizeof(buf), "%0.0f, %0.0f, %0.0f", pos.x, pos.y, pos.z);
	return buf;
}

void ExportData::ToJSON()
{
	stringstream stream;
	minijson::object_writer writer(stream);
	writer.write("timestamp", pt::to_iso_string(pt::second_clock::local_time()));

	minijson::object_writer sii = writer.nested_object("shop_item_info");

	sii.write("0", "id");
	sii.write("1", "quantity");
	sii.write("2", "price");
	sii.write("3", "sell_price");
	sii.write("4", "min_stock");
	sii.write("5", "max_stock");

	sii.close();

	minijson::object_writer pwc = writer.nested_object("bases");

	static unordered_map<uint, string> itemNameMap;
	static unordered_map<uint, string> repNameMap;

	for (auto& iter : player_bases)
	{
		PlayerBase* base = iter.second;
		if (!base->isPublic)
		{
			continue;
		}

		//begin the object writer
		minijson::object_writer pw = pwc.nested_object(wstos(HtmlEncode(base->basename)).c_str());

		pw.write("pass", wstos(base->passwords.begin()->pass).c_str());

		//add basic elements
		pw.write("pos", VectorToString(base->position));
		pw.write("system", base->system);
		pw.write("affiliation", base->affiliation);
		pw.write("level", base->base_level);
		pw.write("money", base->money);
		pw.write("health", 100 * (base->base_health / base->max_base_health));
		pw.write("defensemode", (int)base->defense_mode);

		if (!base->infocard.empty() || !base->infocardHeader.empty())
		{
			minijson::array_writer infocards = pw.nested_array("infocard_paragraphs");
			for (auto& infocard : base->infocard_para)
			{
				if (!infocard.empty())
				{
					infocards.write(wstos(infocard));
				}
			}
			infocards.close();
		}

		minijson::array_writer shop = pw.nested_array("shop_items");
		for (auto& goodId : base->pinned_market_items)
		{
			auto& marketItemIter = base->market_items.find(goodId);

			auto& marketItem = marketItemIter->second;

			minijson::array_writer item = shop.nested_array();
			item.write(goodId);
			item.write(marketItem.quantity);
			item.write(marketItem.price);
			item.write(marketItem.sellPrice);
			item.write(marketItem.min_stock);
			item.write(marketItem.max_stock);
			item.close();

		}
		for (auto& goodId : base->public_market_items)
		{
			auto& marketItemIter = base->market_items.find(goodId);

			auto& marketItem = marketItemIter->second;
			if (!marketItem.is_public || marketItem.is_pinned)
			{
				continue;
			}

			minijson::array_writer item = shop.nested_array();
			item.write(goodId);
			item.write(marketItem.quantity);
			item.write(marketItem.price);
			item.write(marketItem.sellPrice);
			item.write(marketItem.min_stock);
			item.write(marketItem.max_stock);
			item.close();
		}
		shop.close();


		if (base->defense_mode == PlayerBase::DEFENSE_MODE::IFF)
		{
			if (!base->hostile_factions.empty())
			{
				minijson::array_writer iffList = pw.nested_array("hostile_list");
				for (auto& faction : base->hostile_factions)
				{
					iffList.write(faction);
				}
				iffList.close();
			}
			if (!base->hostile_tags.empty())
			{
				minijson::array_writer tagList = pw.nested_array("hostile_tag_list");
				for (auto& tag : base->hostile_tags)
				{
					tagList.write(wstos(tag));
				}
				tagList.close();
			}
			if (!base->hostile_names.empty())
			{
				minijson::array_writer nameList = pw.nested_array("hostile_name_list");
				for (auto& name : base->hostile_names)
				{
					nameList.write(wstos(name));
				}
				nameList.close();
			}
		}

		if (base->defense_mode == PlayerBase::DEFENSE_MODE::NODOCK_HOSTILE
			|| base->defense_mode == PlayerBase::DEFENSE_MODE::NODOCK_NEUTRAL)
		{
			if (!base->ally_factions.empty())
			{
				minijson::array_writer iffList = pw.nested_array("ally_list");
				for (auto& faction : base->ally_factions)
				{
					iffList.write(faction);
				}
				iffList.close();
			}
			if (!base->ally_tags.empty())
			{
				minijson::array_writer tagList = pw.nested_array("ally_tag_list");
				for (auto& tag : base->ally_tags)
				{
					tagList.write(wstos(tag));
				}
				tagList.close();
			}
			if (!base->ally_names.empty())
			{
				minijson::array_writer nameList = pw.nested_array("ally_name_list");
				for (auto& name : base->ally_names)
				{
					nameList.write(wstos(name));
				}
				nameList.close();
			}
		}

		if (!base->srp_factions.empty())
		{
			minijson::array_writer iffList = pw.nested_array("srp_list");
			for (auto& srpFaction : base->srp_factions)
			{
				iffList.write(srpFaction);
			}
			iffList.close();
		}
		if (!base->srp_tags.empty())
		{
			minijson::array_writer tagList = pw.nested_array("srp_tag_list");
			for (auto& tag : base->srp_tags)
			{
				tagList.write(wstos(tag));
			}
			tagList.close();
		}
		if (!base->srp_names.empty())
		{
			minijson::array_writer nameList = pw.nested_array("srp_name_list");
			for (auto& name : base->srp_names)
			{
				nameList.write(wstos(name));
			}
			nameList.close();
		}

		pw.close();

	}
	pwc.close();

	writer.close();

	//dump to a file
	FILE* file = fopen(set_status_path_json_public_shop.c_str(), "w");
	if (file)
	{
		fprintf(file, stream.str().c_str());
		fclose(file);
	}
}

void ExportData::ToJSONBasic()
{
	stringstream stream;
	minijson::object_writer writer(stream);
	writer.write("timestamp", pt::to_iso_string(pt::second_clock::local_time()));
	minijson::object_writer pwc = writer.nested_object("bases");

	static unordered_map<uint, string> itemNameMap;
	static unordered_map<uint, string> repNameMap;
	for (auto& iter : player_bases)
	{
		PlayerBase* base = iter.second;
		//grab the affiliation before we begin
		string theaffiliation;
		if (repNameMap.count(base->affiliation))
		{
			theaffiliation = repNameMap.at(base->affiliation);
		}
		else
		{
			string& repName = wstos(HtmlEncode(HkGetWStringFromIDS(Reputation::get_name(base->affiliation))));
			if (repName == "Object Unknown")
			{
				repName = "No Affiliation";
			}
			repNameMap[base->affiliation] = repName;
			theaffiliation = repName;
		}

		//begin the object writer
		minijson::object_writer pw = pwc.nested_object(wstos(HtmlEncode(base->basename)).c_str());

		minijson::array_writer pwds = pw.nested_array("passwords");
		// first thing we'll do is grab all administrator passwords, encoded.
		for (auto& bp : base->passwords)
		{
			wstring l = bp.pass;
			if (!bp.admin && bp.viewshop)
			{
				l += L" viewshop";
			}
			pwds.write(wstos(HtmlEncode(l)).c_str());
		}
		pwds.close();

		//add basic elements
		pw.write("affiliation", theaffiliation.c_str());
		pw.write("type", base->basetype.c_str());
		pw.write("solar", base->basesolar.c_str());
		pw.write("loadout", base->baseloadout.c_str());
		pw.write("level", base->base_level);
		pw.write("money", base->money);
		pw.write("health", 100 * (base->base_health / base->max_base_health));
		pw.write("defensemode", (int)base->defense_mode);
		pw.write("shieldstate", base->isShieldOn);
		pw.close();

	}
	pwc.close();

	writer.close();

	//dump to a file
	FILE* file = fopen(set_status_path_json.c_str(), "w");
	if (file)
	{
		fprintf(file, stream.str().c_str());
		fclose(file);
	}
}
