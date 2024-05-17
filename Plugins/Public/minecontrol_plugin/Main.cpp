/**
 Mining Plugin for FLHook-Plugin
 by Cannon.

0.1:
 Initial release
0.2:
 Use own asteriod field calculations.
0.3:
 On loot-cheat make player's ship explode and log to flhook cheaters.log
0.4:
 Fixed zone calculation problems.
 Added field by field bonus
0.5:
 Fixed the fix for zone calculation problems
 Added commodity modification for fields
1.0:
 Gave up on my own zone calculations and went back to using the FL ones.
 Changed the bonuses to only work if all equipment items are present.
 Changed the configuration file format to make setup a little quicker.
1.1:
 Fixed player bonus initialisation problem.
 Added playerbonus section error messages and fixed annoying warning
 in flserver-errors.log
1.2:
 Changed mined loot to go directly into cargo hold. Also mining only works
 if the floating loot is hit with a mining gun. Regular guns don't work.
 The system now maintains a historical record of mined ore from fields. Fields
 recharge over time and are depleted as they're mined.
*/

// includes 
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
#include <array>

static const uint DESTRUCTION_TIMER = 180;

static int set_iPluginDebug = 0;
static string set_scStatsPath;
static float set_miningCheatLogThreshold = 2.0f;
static uint set_miningMunition = CreateID("mining_gun_ammo");
static uint set_deployableContainerCommodity = CreateID("commodity_deployable_container");
static float set_globalModifier = 1.0f;
static float set_containerModifier = 1.05f;
static uint set_containerJettisonCount = 5000;
static uint set_containerLootCrateID = CreateID("lootcrate_ast_loot_metal");
static uint set_containerSolarArchetypeID = CreateID("dsy_playerbase_01");
static uint set_containerLoadoutArchetypeID = CreateID("dsy_playerbase_01");
static bool set_scaleFieldRechargeWithPlayerCount = false;

const uint insufficientCargoSoundId = CreateID("insufficient_cargo_space");

extern void PrintZones();

struct MiningNodeInfo {
    uint itemArchId = 0;
    uint lootArchId;
    float countMin;
    float countMax;
};

struct MiningArchetype
{
    uint archetype;
    uint loadout;
};

struct MiningSpawnPointDB {
    uint system;
    string miningNodeGroupName;
    vector<Vector> positions;
    vector<MiningArchetype> nodeArchetypes;
    vector<string> nicknamesVector;
    uint nodeIDS;
    uint respawnCD;
    uint cdProgress = 0;
    uint maxSpawnCount;
    uint spawnedNodesCount = 0;
};

struct NodeInfo
{
    MiningSpawnPointDB* miningDB;
    string nickname;
};

struct MiningBonus
{
    float bonusStandard = 1.0f;
    float bonusAsteroidNodes = 1.0f;
    float bonusAsteroidDestruction = 1.0f;
};

enum class MiningType
{
    DynamicAsteroid,
    StaticAsteroidNode,
    StaticAsteroidCode
};

unordered_map<uint, unordered_map<uint, float>> idBonusMap;
unordered_map<uint, array<MiningNodeInfo, 32>> miningSolarMap;
static array<MiningBonus, 25> set_shipClassModifiers; // using a simple array for optimization purposes
vector<MiningSpawnPointDB> miningDB;
unordered_map<uint, NodeInfo> miningNodeMap;

unordered_map<uint, uint> pendingDestructionNodes;

struct ZONE_BONUS
{
    ZONE_BONUS() : fMultiplier(0.0f), iReplacementLootID(0), fRechargeRate(0), fCurrReserve(100000), fMaxReserve(50000), fMined(0) {}

    wstring scZone;

    // The loot bonus multiplier.
    float fMultiplier;

    // The hash of the item to replace the dropped 
    uint iReplacementLootID;

    // The recharge rate of the zone. This is the number of units of ore added
    // to the reserve per minute.
    float fRechargeRate;

    // The current amount of ore in the zone. When this gets low, ore gets harder
    // to mine. When it gets to 0, ore is impossible to mine.
    float fCurrReserve;

    // The maximum limit for the amount of ore in the field
    float fMaxReserve;

    // The amount of ore that has been mined.
    float fMined;
};
unordered_map<uint, ZONE_BONUS> set_mapZoneBonus;

struct CONTAINER_DATA
{
    uint loot1Id = 0;
    uint loot1Count = 0;
    wstring loot1Name;
    uint lootCrate1Id = 0;
    uint loot2Id = 0;
    uint loot2Count = 0;
    wstring loot2Name;
    uint lootCrate2Id = 0;
    uint nameIDS = 0;
    wstring solarName;
    uint systemId = 0;
    Vector jettisonPos;
    uint clientId = 0;
};

struct CLIENT_DATA
{
    CLIENT_DATA() = default;

    bool initialized = false;
    uint equippedID = 0;
    float equippedVolume = 0.0f;
    uint lootID = 0;
    uint itemCount = 0;
    uint miningEvents = 0;
    uint miningSampleStart = 0;
    float overminedFraction = 0;
    uint deployedContainerId = 0;
    uint lastValidTargetId = 0;
    uint lastValidPlayerId = 0;
    uint lastValidContainerId = 0;
    CONTAINER_DATA* lastValidContainer = nullptr;
    MiningBonus shipClassMiningBonus;

    uint LastTimeMessageAboutBeingFull = 0;
};
unordered_map<uint, CLIENT_DATA> mapClients;
unordered_map<uint, CONTAINER_DATA> mapMiningContainers;

/** A return code to indicate to FLHook if we want the hook processing to continue. */
PLUGIN_RETURNCODE returncode;

float GetMiningYieldBonus(uint id, const uint lootId)
{
    const auto& bonusForId = idBonusMap.find(id);
    if (bonusForId != idBonusMap.end())
    {
        const auto& bonusForLoot = bonusForId->second.find(lootId);
        if (bonusForLoot != bonusForId->second.end()) {
            return bonusForLoot->second;
        }
    }

    return 1.0f;
}

void CheckClientSetup(const uint iClientID)
{
    const auto& equipDesc = Players[iClientID].equipDescList.equip;
    for (auto& equip : equipDesc)
    {
        if (!equip.bMounted || !equip.is_internal())
        {
            continue;
        }
        const Archetype::Equipment* itemPtr = Archetype::GetEquipment(equip.iArchID);
        if (itemPtr->get_class_type() == Archetype::TRACTOR)
        {
            mapClients[iClientID].equippedID = equip.iArchID;
            break;
        }
    }

    mapClients[iClientID].shipClassMiningBonus = set_shipClassModifiers[Archetype::GetShip(Players[iClientID].iShipArchetype)->iShipClass];
}

void DestroyContainer(const uint clientID)
{
    const auto& iter = mapClients.find(clientID);
    if (iter != mapClients.end())
    {
        if (iter->second.deployedContainerId)
        {
            const auto& cd = mapMiningContainers[iter->second.deployedContainerId];
            if (cd.loot1Count)
            {
                Server.MineAsteroid(cd.systemId, cd.jettisonPos, cd.lootCrate1Id, cd.loot1Id, cd.loot1Count, cd.clientId);
            }
            if (cd.loot2Count)
            {
                Server.MineAsteroid(cd.systemId, cd.jettisonPos, cd.lootCrate2Id, cd.loot2Id, cd.loot2Count, cd.clientId);
            }
            Server.MineAsteroid(cd.systemId, cd.jettisonPos, set_containerLootCrateID, set_deployableContainerCommodity, 1, cd.clientId);
            mapMiningContainers.erase(iter->second.deployedContainerId);
            DESPAWN_SOLAR_STRUCT info;
            info.destroyType = FUSE;
            info.spaceObjId = iter->second.deployedContainerId;
            Plugin_Communication(CUSTOM_DESPAWN_SOLAR, &info);
        }
        iter->second.deployedContainerId = 0;
    }
}

void SpawnNode(MiningSpawnPointDB& ms, uint timestamp)
{
    int selectedPositionIndex = rand() % ms.positions.size();
    int selectedArchetypeIndex = rand() % ms.nodeArchetypes.size();

    Vector eulerRotation;
    eulerRotation.x = static_cast<float>((rand() % 360) - 180);
    eulerRotation.y = static_cast<float>((rand() % 360) - 180);
    eulerRotation.z = static_cast<float>((rand() % 360) - 180);

    SPAWN_SOLAR_STRUCT info;
    info.nickname = ms.nicknamesVector.back();
    ms.nicknamesVector.pop_back();
    info.solarArchetypeId = ms.nodeArchetypes[selectedArchetypeIndex].archetype;
    info.loadoutArchetypeId = ms.nodeArchetypes[selectedArchetypeIndex].loadout;
    info.iSystemId = ms.system;
    info.pos = ms.positions[selectedPositionIndex];
    info.ori = EulerMatrix(eulerRotation);
    info.solar_ids = ms.nodeIDS;

    Plugin_Communication(CUSTOM_SPAWN_SOLAR, &info);

    ms.spawnedNodesCount++;
    ms.positions.erase(ms.positions.begin() + selectedPositionIndex);

    NodeInfo node = { &ms, info.nickname };
    miningNodeMap[info.iSpaceObjId] = node;
    ms.cdProgress = 0;
}

EXPORT void HkTimerCheckKick()
{
    returncode = DEFAULT_RETURNCODE;
    uint currTime = static_cast<uint>(time(nullptr));
    // Perform 120 second tasks. 
    if (currTime % 120 == 0)
    {
        uint playerModifier = 0;
        if (set_scaleFieldRechargeWithPlayerCount)
        {
            PlayerData* pd = nullptr;
            while (pd = Players.traverse_active(pd))
            {
                playerModifier++;
            }
        }
        else
        {
            playerModifier = 1;
        }

        char szDataPath[MAX_PATH];
        GetUserDataPath(szDataPath);
        string scStatsPath = string(szDataPath) + R"(\Accts\MultiPlayer\mining_stats.txt)";
        FILE* file = fopen(scStatsPath.c_str(), "w");
        if (file)
            fprintf(file, "[Zones]\n");

        // Recharge the fields
        for (auto& i = set_mapZoneBonus.begin(); i != set_mapZoneBonus.end(); i++)
        {
            auto& zone = i->second;
            zone.fCurrReserve = min(zone.fCurrReserve + (zone.fRechargeRate * playerModifier), zone.fMaxReserve);

            if (file && !zone.scZone.empty() && zone.fMaxReserve > 0 && zone.fMaxReserve != zone.fCurrReserve)
            {
                fprintf(file, "%ls, %0.0f, %0.0f\n", zone.scZone.c_str(), zone.fCurrReserve, zone.fMined);
            }
        }

        if (file)
        {
            fclose(file);
        }
    }

    if (currTime % 30 == 0 && !pendingDestructionNodes.empty())
    {
        for (auto damagedNode = pendingDestructionNodes.begin(); damagedNode != pendingDestructionNodes.end(); )
        {
            if (currTime > damagedNode->second)
            {
                pub::SpaceObj::Destroy(damagedNode->first, DestroyType::FUSE);
                damagedNode = pendingDestructionNodes.erase(damagedNode);
            }
            else
            {
                damagedNode++;
            }
        }
    }

    for (auto& entry : miningDB)
    {
        if (entry.spawnedNodesCount >= entry.maxSpawnCount)
        {
            continue;
        }

        if (++entry.cdProgress >= entry.respawnCD)
        {
            SpawnNode(entry, currTime);
        }
    }
}

/// Clear client info when a client connects.
EXPORT void ClearClientInfo(uint iClientID)
{
    returncode = DEFAULT_RETURNCODE;
    auto& cd = mapClients[iClientID];
    cd.equippedID = 0;
    cd.itemCount = 0;
    cd.lootID = 0;
    DestroyContainer(iClientID);
}

/// Load the configuration
EXPORT void LoadSettings()
{
    returncode = DEFAULT_RETURNCODE;

    // The path to the configuration file.
    char szCurDir[MAX_PATH];
    GetCurrentDirectory(sizeof(szCurDir), szCurDir);
    string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\minecontrol.cfg";
    string solarArchFile = string(szCurDir) + "..\\DATA\\SOLAR\\solararch.ini";

    // Load generic settings
    set_iPluginDebug = IniGetI(scPluginCfgFile, "MiningGeneral", "Debug", 0);
    set_scStatsPath = IniGetS(scPluginCfgFile, "MiningGeneral", "StatsPath", "");
    set_globalModifier = IniGetF(scPluginCfgFile, "MiningGeneral", "GlobalModifier", set_globalModifier);
    set_containerModifier = IniGetF(scPluginCfgFile, "MiningGeneral", "ContainerModifier", set_containerModifier);
    set_containerJettisonCount = IniGetI(scPluginCfgFile, "MiningGeneral", "ContainerJettisonCount", set_containerJettisonCount);
    set_containerSolarArchetypeID = CreateID(IniGetS(scPluginCfgFile, "MiningGeneral", "ContainerSolarArchetype", "dsy_playerbase_01").c_str());
    set_containerLoadoutArchetypeID = CreateID(IniGetS(scPluginCfgFile, "MiningGeneral", "ContainerLoadoutArchetype", "dsy_playerbase_01").c_str());
    set_deployableContainerCommodity = CreateID(IniGetS(scPluginCfgFile, "MiningGeneral", "ContainerCommodity", "commodity_scrap_metal").c_str());
    set_miningMunition = CreateID(IniGetS(scPluginCfgFile, "MiningGeneral", "MiningMunition", "mining_gun_ammo").c_str());
    set_miningCheatLogThreshold = IniGetF(scPluginCfgFile, "MiningGeneral", "MiningCheatLogThreshold", set_miningCheatLogThreshold);
    set_scaleFieldRechargeWithPlayerCount = IniGetB(scPluginCfgFile, "MiningGeneral", "PlayerScalingRecharge", set_scaleFieldRechargeWithPlayerCount);

    set_containerLootCrateID = Archetype::GetEquipment(set_deployableContainerCommodity)->get_loot_appearance()->iArchID;

    if (set_iPluginDebug)
    {
        ConPrint(L"NOTICE: debug=%d\n", set_iPluginDebug);
    }

    // Load the player bonus list and the field bonus list.
    // To receive the bonus for the particular commodity the player has to have 
    // the affiliation (unless this field is empty), one of the ships and 
    // all of the equipment items.
    // The [PlayerBonus] section has the following format:
    // Commodity, Bonus, Affiliation, List of ships, equipment or cargo separated by commas.
    // The [FieldBonus] section has the following format:
    // Field, Bonus, Replacement Commodity
    set_mapZoneBonus.clear();
    INI_Reader ini;
    if (ini.open(scPluginCfgFile.c_str(), false))
    {
        while (ini.read_header())
        {
            uint solarArch;
            if (ini.is_header("MiningSolar"))
            {
                while (ini.read_value())
                {
                    if (ini.is_value("nickname"))
                    {
                        solarArch = CreateID(ini.get_value_string());
                    }
                    else if (ini.is_value("node"))
                    {
                        MiningNodeInfo& node = miningSolarMap[solarArch][ini.get_value_int(0)];
                        node.itemArchId = CreateID(ini.get_value_string(1));
                        node.lootArchId = Archetype::GetEquipment(node.itemArchId)->get_loot_appearance()->iArchID;
                        node.countMin = ini.get_value_float(2);
                        node.countMax = ini.get_value_float(3);
                    }
                }
            }
            else if (ini.is_header("PlayerBonus"))
            {
                while (ini.read_value())
                {
                    if (ini.is_value("pb"))
                    {
                        uint licenceId = CreateID(ini.get_value_string(0));
                        wstring licenceName = stows(ini.get_value_string(0));
                        uint lootId = CreateID(ini.get_value_string(1));
                        wstring lootName = stows(ini.get_value_string(1));
                        float bonus = ini.get_value_float(2);
                        if (set_iPluginDebug)
                        {
                            ConPrint(L"NOTICE: licence %ls bonus=%2.2f loot=%s(%u)\n",
                                licenceName.c_str(), bonus, lootName.c_str(), lootId);
                        }

                        idBonusMap[licenceId][lootId] = bonus;
                    }
                }
            }
            else if (ini.is_header("ZoneBonus"))
            {
                while (ini.read_value())
                {
                    if (ini.is_value("zb"))
                    {
                        uint zoneID = CreateID(ini.get_value_string(0));
                        wstring zoneName = stows(ini.get_value_string(0));
                        float bonus = ini.get_value_float(1);
                        uint replacementLootID = CreateID(ini.get_value_string(2));
                        wstring replacementLootName = stows(ini.get_value_string(2));
                        float rechargeRate = ini.get_value_float(3);
                        float maxReserve = ini.get_value_float(4);
                        if (zoneName.empty() || bonus <= 0.0f || maxReserve <= 0.0f)
                        {
                            ConPrint(L"Incorrectly setup Zone Bonus entry!\n");
                            continue;
                        }
                        set_mapZoneBonus[zoneID].scZone = zoneName;
                        set_mapZoneBonus[zoneID].fMultiplier = bonus;
                        set_mapZoneBonus[zoneID].iReplacementLootID = replacementLootID;
                        set_mapZoneBonus[zoneID].fRechargeRate = rechargeRate;
                        set_mapZoneBonus[zoneID].fCurrReserve = maxReserve;
                        set_mapZoneBonus[zoneID].fMaxReserve = maxReserve;
                        if (set_iPluginDebug)
                        {
                            ConPrint(L"NOTICE: zone bonus %s bonus=%2.2f replacementLootID=%s(%u) rechargeRate=%0.0f maxReserve=%0.0f\n",
                                zoneName.c_str(), bonus, replacementLootName.c_str(), replacementLootID, rechargeRate, maxReserve);
                        }
                    }
                }
            }
            else if (ini.is_header("ShipTypeBonus"))
            {
                while (ini.read_value())
                {
                    if (ini.is_value("ship_class_bonus"))
                    {
                        set_shipClassModifiers[ini.get_value_int(0)] = { ini.get_value_float(1),ini.get_value_float(2),ini.get_value_float(3) };
                    }
                }
            }
        }
        ini.close();
    }

    // Read the last saved zone reserve.
    char szDataPath[MAX_PATH];
    GetUserDataPath(szDataPath);
    string scStatsPath = string(szDataPath) + R"(\Accts\MultiPlayer\mining_stats.txt)";
    if (ini.open(scStatsPath.c_str(), false))
    {
        while (ini.read_header())
        {
            if (ini.is_header("Zones"))
            {
                while (ini.read_value())
                {
                    string zoneName = ini.get_value_string(0);
                    if (zoneName.empty())
                    {
                        ConPrint(L"Incorrect entry in mining stats file!\n");
                        continue;
                    }
                    uint zoneID = CreateID(zoneName.c_str());
                    auto& zoneData = set_mapZoneBonus[zoneID];
                    zoneData.fCurrReserve = ini.get_value_float(1);
                    zoneData.fMined = ini.get_value_float(2);
                }
            }
        }
        ini.close();
    }

    string miningDatabaseCfg = string(szCurDir) + R"(\flhook_plugins\minecontrol_nodes.cfg)";
    if (ini.open(miningDatabaseCfg.c_str(), false))
    {
        while (ini.read_header())
        {
            if (!ini.is_header("MiningSystem"))
            {
                continue;
            }

            MiningSpawnPointDB miningDBEntry;

            while (ini.read_value())
            {
                if (ini.is_value("position"))
                {
                    miningDBEntry.positions.push_back({ ini.get_value_float(0) ,ini.get_value_float(1) ,ini.get_value_float(2) });
                }
                else if (ini.is_value("system"))
                {
                    miningDBEntry.system = CreateID(ini.get_value_string());
                }
                else if (ini.is_value("max_spawn_count"))
                {
                    miningDBEntry.maxSpawnCount = ini.get_value_int(0);
                }
                else if (ini.is_value("respawn_cooldown"))
                {
                    miningDBEntry.respawnCD = ini.get_value_int(0);
                }
                else if (ini.is_value("node_archetype"))
                {
                    miningDBEntry.nodeArchetypes.push_back({ CreateID(ini.get_value_string(0)), CreateID(ini.get_value_string(1)) });
                }
                else if (ini.is_value("name"))
                {
                    miningDBEntry.miningNodeGroupName = ini.get_value_string();
                }
                else if (ini.is_value("node_ids"))
                {
                    miningDBEntry.nodeIDS = ini.get_value_int(0);
                }
            }

            if (miningDBEntry.maxSpawnCount > miningDBEntry.positions.size())
            {
                AddLog("ERROR: INCORRECT MINING NODE CONFIG FOR NODE SYSTEM %s\n", miningDBEntry.miningNodeGroupName.c_str());
                ConPrint(L"ERROR: INCORRECT MINING NODE CONFIG FOR NODE SYSTEM %ls\n", stows(miningDBEntry.miningNodeGroupName).c_str());
                continue;
            }
            for (uint i = 0; i < miningDBEntry.maxSpawnCount; ++i)
            {
                miningDBEntry.nicknamesVector.emplace_back("mining_node_" + miningDBEntry.miningNodeGroupName + itos(i));
            }

            miningDB.emplace_back(miningDBEntry);
        }
    }

    struct PlayerData* pPD = 0;
    while (pPD = Players.traverse_active(pPD))
    {
        uint iClientID = HkGetClientIdFromPD(pPD);
        ClearClientInfo(iClientID);
    }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    // If we're being loaded from the command line while FLHook is running then
    // set_scCfgFile will not be empty so load the settings as FLHook only
    // calls load settings on FLHook startup and .rehash.
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        if (set_scCfgFile.length() > 0)
        {
            LoadSettings();
        }

        HkLoadStringDLLs();
    }
    if (fdwReason == DLL_PROCESS_DETACH)
    {
        for (auto item : miningNodeMap)
        {
            DESPAWN_SOLAR_STRUCT info;
            info.spaceObjId = item.first;
            info.destroyType = DestroyType::VANISH;
            Plugin_Communication(CUSTOM_DESPAWN_SOLAR, &info);
        }
        HkUnloadStringDLLs();
    }
    return true;

}


bool UserCmd_Process(uint client, const wstring& args)
{
    returncode = DEFAULT_RETURNCODE;

    if (args.find(L"/cs") != 0 && args.find(L"/cargostored") != 0)
    {
        return false;
    }

    CShip* cship = ClientInfo[client].cship;

    if (!cship)
    {
        PrintUserCmdText(client, L"ERR Not in space");
        return true;
    }

    IObjRW* target = cship->target;
    if (!target)
    {
        PrintUserCmdText(client, L"ERR Mining container not selected");
        return true;
    }

    uint targetId = target->get_id();
    if (!targetId)
    {
        PrintUserCmdText(client, L"ERR Mining container not selected");
        return true;
    }
    const auto& container = mapMiningContainers.find(targetId);
    if (container == mapMiningContainers.end())
    {
        PrintUserCmdText(client, L"ERR Mining container not selected");
        return true;
    }

    if (!container->second.loot1Count && !container->second.loot2Count)
    {
        PrintUserCmdText(client, L"Container is empty!");
    }
    else if (!container->second.loot2Id)
    {
        PrintUserCmdText(client, L"Container holds %u units of %ls", container->second.loot1Count, container->second.loot1Name.c_str());
    }
    else
    {
        PrintUserCmdText(client, L"Container holds %u units of %ls and %u units of %ls",
            container->second.loot1Count, container->second.loot1Name.c_str(), container->second.loot2Count, container->second.loot2Name.c_str());
    }


    return true;
}

/// Called when a gun hits something
void __stdcall SPMunitionCollision(struct SSPMunitionCollisionInfo const& ci, unsigned int iClientID)
{
    returncode = DEFAULT_RETURNCODE;
    // If this is not a lootable rock, do no other processing.
    if (ci.iProjectileArchID != set_miningMunition || ci.dwTargetShip != 0)
    {
        return;
    }

    returncode = SKIPPLUGINS_NOFUNCTIONCALL;

    CLIENT_DATA& cd = mapClients[iClientID];

    if (!cd.itemCount)
    {
        return;
    }

    // use floats to ensure precision when applying various minor modifiers.
    float miningYield = static_cast<float>(cd.itemCount);
    uint lootId = cd.lootID;
    cd.itemCount = 0;
    cd.lootID = 0;

    CShip* cship = ClientInfo[iClientID].cship;
    if (!cship)
    {
        return;
    }

    uint iClientSystemID = cship->system;
    CmnAsteroid::CAsteroidSystem* csys = CmnAsteroid::Find(iClientSystemID);
    if (!csys)
    {
        return;
    }

    Vector& vPos = cship->vPos;

    // Find asteroid field that matches the best.
    for (CmnAsteroid::CAsteroidField* cfield = csys->FindFirst(); cfield; cfield = csys->FindNext())
    {
        if (!cfield->near_field(vPos))
        {
            continue;
        }
        const Universe::IZone* zone = cfield->get_lootable_zone(vPos);
        if (!zone || !zone->lootableZone)
        {
            continue;
        }

        const auto& zoneBonusData = set_mapZoneBonus.find(zone->iZoneID);
        ZONE_BONUS* finalZone = nullptr;
        if (zoneBonusData != set_mapZoneBonus.end())
        {
            auto& zoneData = zoneBonusData->second;
            if (zoneData.fCurrReserve == 0.0f)
            {
                return;
            }

            if (zoneData.iReplacementLootID)
            {
                lootId = zoneData.iReplacementLootID;
            }
            miningYield *= zoneData.fMultiplier;

            miningYield = max(miningYield, zoneData.fCurrReserve);
            finalZone = &zoneData; // save ZONE_BONUS ref to update AFTER all the bonuses are applied
        }

        miningYield *= GetMiningYieldBonus(cd.equippedID, lootId) * set_globalModifier * cd.shipClassMiningBonus.bonusStandard;
        miningYield += cd.overminedFraction; // add the decimal remainder from last mining event.

        if (finalZone)
        {
            finalZone->fCurrReserve -= miningYield;
            finalZone->fMined += miningYield;
        }
        // If this ship is has another ship targetted then send the ore into the cargo
        // hold of the other ship.
        uint iSendToClientID = iClientID;
        const Archetype::Equipment* lootInfo = Archetype::GetEquipment(lootId);

        bool foundContainer = false;
        IObjRW* target = cship->target;

        if (target)
        {
            uint targetId = target->get_id();
            uint objType = target->get_object_type();
            if ((objType & (Fighter | Freighter | Transport | Gunboat | Cruiser | Capital | DestructibleDepot)) && HkDistance3D(vPos, target->get_position()) < 1000.0f)
            {

                CONTAINER_DATA* container = nullptr;
                if (cd.lastValidTargetId == targetId)
                {
                    iSendToClientID = cd.lastValidPlayerId;
                }
                else if (cd.lastValidContainerId == targetId)
                {
                    container = cd.lastValidContainer;
                }
                else if (!(objType & DestructibleDepot))
                {
                    uint iTargetClientID = ((CShip*)target->cobj)->ownerPlayer;
                    if (iTargetClientID)
                    {
                        iSendToClientID = iTargetClientID;
                        cd.lastValidTargetId = targetId;
                        cd.lastValidPlayerId = iTargetClientID;
                    }
                    else
                    {
                        const auto& containerIter = mapMiningContainers.find(targetId);
                        if (containerIter != mapMiningContainers.end())
                        {
                            container = &containerIter->second;
                            cd.lastValidContainer = container;
                            cd.lastValidContainerId = targetId;
                        }
                    }
                }

                if (container)
                {
                    uint* lootCount = nullptr;
                    if (container->loot1Id == lootId)
                    {
                        foundContainer = true;
                        lootCount = &container->loot1Count;
                    }
                    else if (container->loot2Id == lootId)
                    {
                        foundContainer = true;
                        lootCount = &container->loot2Count;
                    }
                    if (foundContainer)
                    {
                        *lootCount += static_cast<uint>(miningYield * set_containerModifier);

                        uint amountToJettison = static_cast<uint>(static_cast<float>(set_containerJettisonCount) / lootInfo->fVolume);
                        if (*lootCount >= amountToJettison)
                        {
                            Server.MineAsteroid(container->systemId, container->jettisonPos, set_containerLootCrateID, lootId, amountToJettison, container->clientId);
                            *lootCount -= amountToJettison;
                        }
                    }
                }
            }
        }

        uint miningYieldInt = static_cast<uint>(miningYield);
        cd.overminedFraction = miningYield - miningYieldInt; // save the unused decimal portion for the next mining event.

        if (cd.miningSampleStart < time(nullptr))
        {
            float average = cd.miningEvents / 30.0f;
            if (average > set_miningCheatLogThreshold)
            {
                AddLog("NOTICE: high mining rate charname=%s rate=%0.1f/sec location=%0.0f,%0.0f,%0.0f system=%08x zone=%08x",
                    wstos((const wchar_t*)Players.GetActiveCharacterName(iClientID)).c_str(),
                    average, vPos.x, vPos.y, vPos.z, zone->iSystemID, zone->iZoneID);
            }

            cd.miningSampleStart = static_cast<uint>(time(nullptr)) + 30;
            cd.miningEvents = 0;
        }

        if (foundContainer)
        {
            return;
        }

        float fHoldRemaining = cship->get_cargo_hold_remaining();

        if (fHoldRemaining <= 0.0f)
        {
            miningYieldInt = 0;
        }
        else if (fHoldRemaining < static_cast<float>(miningYieldInt) * lootInfo->fVolume)
        {
            miningYieldInt = static_cast<uint>(fHoldRemaining / lootInfo->fVolume);
        }

        if (!miningYieldInt)
        {
            if (((uint)time(nullptr) - mapClients[iClientID].LastTimeMessageAboutBeingFull) > 1)
            {
                PrintUserCmdText(iClientID, L"%s's cargo is now full.", reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(iSendToClientID)));
                pub::Player::SendNNMessage(iClientID, insufficientCargoSoundId);
                if (iClientID != iSendToClientID)
                {
                    PrintUserCmdText(iSendToClientID, L"Your cargo is now full.");
                    pub::Player::SendNNMessage(iSendToClientID, insufficientCargoSoundId);
                }
                mapClients[iClientID].LastTimeMessageAboutBeingFull = (uint)time(nullptr);
            }
        }
        else
        {
            pub::Player::AddCargo(iSendToClientID, lootId, miningYieldInt, 1.0, false);
        }
    }

}

void __stdcall MineAsteroid(uint iClientSystemID, class Vector const& vPos, uint iCrateID, uint iLootID, uint iCount, uint iClientID)
{
    returncode = SKIPPLUGINS_NOFUNCTIONCALL;
    CLIENT_DATA& data = mapClients[iClientID];
    if (!data.initialized)
    {
        data.initialized = true;
        CheckClientSetup(iClientID);
    }
    data.itemCount = iCount;
    data.lootID = iLootID;
}

void __stdcall JettisonCargo(unsigned int iClientID, struct XJettisonCargo const& jc)
{
    returncode = DEFAULT_RETURNCODE;

    if (jc.iCount != 1)
    {
        return;
    }

    CEquipTraverser tr(EquipmentClass::Cargo);
    CShip* cship = ClientInfo[iClientID].cship;
    if (!cship)
    {
        return;
    }

    CEquip* equip;
    equip = cship->equip_manager.FindByID(jc.iSlot);

    if (!equip)
    {
        PrintUserCmdText(iClientID, L"ERR Issue when handling jettison event, contact developers. Error code %u", jc.iSlot);
        AddLog("Error: jettisoned item not found! %u", jc.iSlot);
        return;
    }

    if (equip->archetype->iArchID != set_deployableContainerCommodity)
    {
        return;
    }
    returncode = SKIPPLUGINS_NOFUNCTIONCALL;

    const auto& cd = mapClients.find(iClientID);
    if (cd != mapClients.end() && cd->second.deployedContainerId)
    {
        PrintUserCmdText(iClientID, L"ERR A mining container is already deployed");
        return;
    }

    uint shipId = cship->id;
    uint systemId = cship->system;
    Vector pos = cship->vPos;
    Matrix ori = cship->mRot;
    wstring commodityName1;
    wstring commodityName2;
    uint loot1Id = 0;
    uint loot2Id = 0;

    TranslateX(pos, ori, -400);

    CmnAsteroid::CAsteroidSystem* csys = CmnAsteroid::Find(systemId);
    if (!csys)
    {
        PrintUserCmdText(iClientID, L"ERR Not in a mineable field!");
        return;
    }

    bool alreadyFoundFirstMineable = false;
    // Find asteroid field that matches the best.
    for (CmnAsteroid::CAsteroidField* cfield = csys->FindFirst(); cfield; cfield = csys->FindNext())
    {
        uint tempLootId;
        if (!cfield->near_field(pos))
        {
            continue;
        }
        const Universe::IZone* zone = cfield->get_lootable_zone(pos);
        if (!zone || !zone->lootableZone)
        {
            continue;
        }
        const auto& zoneBonusData = set_mapZoneBonus.find(zone->iZoneID);
        if (zoneBonusData != set_mapZoneBonus.end() && zoneBonusData->second.iReplacementLootID)
        {
            tempLootId = zoneBonusData->second.iReplacementLootID;
        }
        else
        {
            tempLootId = zone->lootableZone->dynamic_loot_commodity;
        }

        if (loot1Id && tempLootId == loot1Id)
        {
            continue;
        }

        const GoodInfo* gi = GoodList::find_by_id(tempLootId);
        if (!alreadyFoundFirstMineable)
        {
            loot1Id = tempLootId;
            alreadyFoundFirstMineable = true;
            commodityName1 = HkGetWStringFromIDS(gi->iIDSName);
        }
        else
        {
            loot2Id = tempLootId;
            commodityName2 = HkGetWStringFromIDS(gi->iIDSName);
            break;
        }

    }

    if (!loot1Id)
    {
        PrintUserCmdText(iClientID, L"ERR Not in a mineable field!");
        return;
    }


    wstring fullContainerName;
    if (loot2Id)
    {
        fullContainerName = commodityName1 + L"/" + commodityName2 + L" Container";
    }
    else
    {
        fullContainerName = commodityName1 + L" Container";
    }

    SPAWN_SOLAR_STRUCT data;
    data.iSystemId = systemId;
    data.pos = pos;
    data.ori = ori;
    data.overwrittenName = fullContainerName;
    data.nickname = "player_mining_container_" + itos(iClientID);
    data.solar_ids = 540999 + iClientID;
    data.solarArchetypeId = set_containerSolarArchetypeID;
    data.loadoutArchetypeId = set_containerLoadoutArchetypeID;

    Plugin_Communication(PLUGIN_MESSAGE::CUSTOM_SPAWN_SOLAR, &data);
    if (data.iSpaceObjId)
    {
        pub::SpaceObj::SetRelativeHealth(data.iSpaceObjId, 1.0f);
        CONTAINER_DATA cd;
        cd.systemId = systemId;
        pos.y -= 30;
        cd.jettisonPos = pos;
        cd.loot1Id = loot1Id;
        cd.loot1Name = commodityName1;
        cd.lootCrate1Id = Archetype::GetEquipment(loot1Id)->get_loot_appearance()->iArchID;
        if (loot2Id)
        {
            cd.loot2Id = loot2Id;
            cd.loot2Name = commodityName2;
            cd.lootCrate2Id = Archetype::GetEquipment(loot2Id)->get_loot_appearance()->iArchID;
        }
        cd.nameIDS = data.solar_ids;
        cd.solarName = data.overwrittenName;
        cd.clientId = iClientID;
        mapMiningContainers[data.iSpaceObjId] = cd;
        mapClients[iClientID].deployedContainerId = data.iSpaceObjId;
        pub::Player::RemoveCargo(iClientID, equip->iSubObjId, 1);
    }
}

void __stdcall DisConnect(unsigned int iClientID, enum  EFLConnection state)
{
    returncode = DEFAULT_RETURNCODE;
    DestroyContainer(iClientID);
}

void __stdcall CharacterSelect(struct CHARACTER_ID const& cId, unsigned int iClientID)
{
    returncode = DEFAULT_RETURNCODE;
    DestroyContainer(iClientID);

    for (const auto& container : mapMiningContainers)
    {
        HkChangeIDSString(iClientID, container.second.nameIDS, container.second.solarName);
    }
}

void __stdcall SystemSwitchOut(unsigned int iShip, uint iClientID)
{
    returncode = DEFAULT_RETURNCODE;
    DestroyContainer(iClientID);
}

void __stdcall BaseEnter(uint base, uint iClientID)
{
    returncode = DEFAULT_RETURNCODE;
    const auto& clientInfo = mapClients.find(iClientID);
    if (clientInfo == mapClients.end())
    {
        return;
    }
    if (clientInfo->second.deployedContainerId
        && mapMiningContainers[clientInfo->second.deployedContainerId].systemId != Players[iClientID].iSystemID)
    {
        DestroyContainer(iClientID);
    }
}

void PlayerLaunch_AFTER(uint shipID, uint clientID)
{
    mapClients.erase(clientID);
}

float rand_FloatRange(float a, float b)
{
    return ((b - a) * ((float)rand() / RAND_MAX)) + a;
}

uint GetAsteroidMiningYield(const MiningNodeInfo& node, uint clientId, bool isNode)
{
    CLIENT_DATA& cd = mapClients[clientId];
    if (!cd.initialized)
    {
        cd.initialized = true;
        CheckClientSetup(clientId);
    }
    float miningBonus = GetMiningYieldBonus(cd.equippedID, node.itemArchId);
    float droppedAmount = rand_FloatRange(node.countMin, node.countMax) * miningBonus;
    if (isNode)
    {
        droppedAmount *= cd.shipClassMiningBonus.bonusAsteroidNodes;
    }
    else
    {
        droppedAmount *= cd.shipClassMiningBonus.bonusAsteroidDestruction;
    }
    return static_cast<uint>(droppedAmount);
}

void __stdcall BaseDestroyed(IObjRW* iobj, bool isKill, uint killerId)
{
    returncode = DEFAULT_RETURNCODE;
    uint space_obj = iobj->get_id();
    const auto& i = mapMiningContainers.find(space_obj);
    if (i != mapMiningContainers.end())
    {
        const CONTAINER_DATA& cd = i->second;
        mapClients[cd.clientId].deployedContainerId = 0;
        // container destruction drop all contents as well as 'packed up' container.
        if (cd.loot1Count)
        {
            Server.MineAsteroid(cd.systemId, cd.jettisonPos, set_containerLootCrateID, cd.loot1Id, cd.loot1Count, cd.clientId);
        }
        if (cd.loot2Count)
        {
            Server.MineAsteroid(cd.systemId, cd.jettisonPos, set_containerLootCrateID, cd.loot2Id, cd.loot2Count, cd.clientId);
        }
        Server.MineAsteroid(cd.systemId, cd.jettisonPos, set_containerLootCrateID, set_deployableContainerCommodity, 1, cd.clientId);
        mapMiningContainers.erase(space_obj);

        return;
    }
    if (miningNodeMap.count(space_obj))
    {
        uint clientKiller = HkGetClientIDByShip(killerId);
        if (clientKiller)
        {
            CLIENT_DATA& cd = mapClients[clientKiller];
            if (!cd.initialized)
            {
                cd.initialized = true;
                CheckClientSetup(clientKiller);
            }

            auto& nodeArray = miningSolarMap.at(iobj->cobj->archetype->iArchID);
            const MiningNodeInfo& node = nodeArray.at(1);
            uint minedAmount = GetAsteroidMiningYield(node, clientKiller, false);
            Server.MineAsteroid(iobj->cobj->system, iobj->get_position(), node.lootArchId, node.itemArchId, minedAmount, 0);
        }

        auto& nodeDb = miningNodeMap.at(space_obj);
        nodeDb.miningDB->spawnedNodesCount--;
        nodeDb.miningDB->positions.push_back(iobj->cobj->vPos);
        nodeDb.miningDB->nicknamesVector.emplace_back(nodeDb.nickname);
        miningNodeMap.erase(space_obj);
    }
}

void SolarColGrpDestroyed(IObjRW* iobj, CArchGroup* colGrp, DamageEntry::SubObjFate fate, DamageList* dmg)
{
    returncode = DEFAULT_RETURNCODE;

    if (!dmg->iInflictorPlayerID)
    {
        return;
    }
    if (!miningSolarMap.count(iobj->cobj->archetype->iArchID))
    {
        return;
    }

    if (!pendingDestructionNodes.count(iobj->get_id()))
    {
        pendingDestructionNodes[iobj->get_id()] = (uint)time(0) + DESTRUCTION_TIMER;
    }

    auto& nodeArray = miningSolarMap.at(iobj->cobj->archetype->iArchID);
    const MiningNodeInfo& node = nodeArray.at(colGrp->colGrp->id);
    if (!node.itemArchId)
    {
        return;
    }
    Vector colGrpCenter;
    colGrp->GetCenterOfMass(colGrpCenter);

    uint minedAmount = GetAsteroidMiningYield(node, dmg->iInflictorPlayerID, true);

    Server.MineAsteroid(iobj->cobj->system, colGrpCenter, node.lootArchId, node.itemArchId, minedAmount, 0);

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Functions to hook */
EXPORT PLUGIN_INFO* Get_PluginInfo()
{
    PLUGIN_INFO* p_PI = new PLUGIN_INFO();
    p_PI->sName = "Mine Control Plugin by cannon";
    p_PI->sShortName = "minecontrol";
    p_PI->bMayPause = true;
    p_PI->bMayUnload = true;
    p_PI->ePluginReturnCode = &returncode;
    p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
    p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ClearClientInfo, PLUGIN_ClearClientInfo, 0));
    p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect, PLUGIN_HkIServerImpl_CharacterSelect, 0));
    p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch_AFTER, PLUGIN_HkIServerImpl_PlayerLaunch_AFTER, 0));
    p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&DisConnect, PLUGIN_HkIServerImpl_DisConnect, 0));
    p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&SystemSwitchOut, PLUGIN_HkIServerImpl_SystemSwitchOutComplete, 0));
    p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseEnter, PLUGIN_HkIServerImpl_BaseEnter_AFTER, 0));
    p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseDestroyed, PLUGIN_BaseDestroyed, 0));
    p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&MineAsteroid, PLUGIN_HkIServerImpl_MineAsteroid, 0));
    p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&SPMunitionCollision, PLUGIN_HkIServerImpl_SPMunitionCollision, 0));
    p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&JettisonCargo, PLUGIN_HkIServerImpl_JettisonCargo, 0));
    p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&SolarColGrpDestroyed, PLUGIN_SolarColGrpDestroyed, 0));
    p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&HkTimerCheckKick, PLUGIN_HkTimerCheckKick, 0));
    p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
    return p_PI;
}
