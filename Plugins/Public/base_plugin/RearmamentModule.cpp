#include "Main.h"

RearmamentModule::RearmamentModule(PlayerBase* the_base) : Module(TYPE_REARMAMENT), base(the_base)
{
    the_base->isRearmamentAvailable = true;
}

RearmamentModule::~RearmamentModule()
{
    base->isRearmamentAvailable = false;
}

wstring RearmamentModule::GetInfo(bool xml)
{
    return L"Rearmament Module - " + itows(static_cast<int>(base->rearmamentCostPerCredit * 100.f)) + L"% of regular restock prices.";
}

void RearmamentModule::LoadState(INI_Reader& ini)
{
}

void RearmamentModule::SaveState(FILE* file)
{
    fprintf(file, "[RearmamentModule]\n");
}

void RearmamentModule::Rearm(uint clientId)
{
    PlayerBase* base = GetPlayerBaseForClient(clientId);

    if (!base)
    {
        PrintUserCmdText(clientId, L"ERR Not on a player base!");
        return;
    }

    if (!base->isRearmamentAvailable || !base->isCrewSupplied)
    {
        PrintUserCmdText(clientId, L"ERR Rearmament not available");
        return;
    }

    CUSTOM_AUTOBUY_CARTITEMS itemCart;
    itemCart.clientId = clientId;
    Plugin_Communication(PLUGIN_MESSAGE::CUSTOM_AUTOBUY_CART, &itemCart);

    if (itemCart.cartItems.empty())
    {
        return;
    }

    float sumToPay = 0;
    for (auto& item : itemCart.cartItems)
    {
        if (item.iCount == 0)
        {
            continue;
        }
        Archetype::Equipment* eq = Archetype::GetEquipment(item.iArchID);
        if (itemCart.remHoldSize < (eq->fVolume * item.iCount))
        {
            continue;
        }
        auto gi = GoodList::find_by_id(item.iArchID);
        sumToPay += gi->fPrice * item.iCount;
    }

    float creditCount = 0;
    float actualToPay = 0;


    if (!base->archetype || !base->archetype->hasUnlimitedResupply)
    {
        for (auto& item : rearmamentCreditRatio)
        {
            creditCount += base->HasMarketItem(item.first) * item.second;
            float itemsToConsume = ceilf(sumToPay / item.second);
            uint itemCount = base->HasMarketItem(item.first);
            itemsToConsume = min(itemsToConsume, itemCount);
            actualToPay += itemsToConsume * item.second;
            if (actualToPay >= sumToPay)
            {
                break;
            }
        }

        if (creditCount < sumToPay)
        {
            PrintUserCmdText(clientId, L"ERR Insufficient munition supplies!");
            return;
        }
    }
    else
    {
        actualToPay = itemCart.creditCost;
    }

    int creditCost = static_cast<int>(base->rearmamentCostPerCredit * actualToPay);

    if (Players[clientId].iInspectCash < creditCost)
    {
        PrintUserCmdText(clientId, L"ERR Insufficient money for a resupply!");
        return;
    }

    uint baseId = Players[clientId].iBaseID;
    uint location = Players[clientId].iBaseRoomID;
    Server.LocationExit(location, clientId);
    Server.BaseExit(baseId, clientId);

    int cost = static_cast<int>(creditCost);
    pub::Player::AdjustCash(clientId, -cost);
    base->ChangeMoney(cost);

    for (auto& item : rearmamentCreditRatio)
    {
        float itemsToConsume = ceilf(actualToPay / item.second);
        uint itemCount = base->HasMarketItem(item.first);
        itemsToConsume = min(itemsToConsume, itemCount);
        base->RemoveMarketGood(item.first, static_cast<uint>(itemsToConsume));
        actualToPay -= itemsToConsume * item.second;
        if (actualToPay <= 0.0f)
        {
            break;
        }
    }

    for (auto& item : itemCart.cartItems)
    {
        if (item.iCount == 0)
        {
            continue;
        }
        Archetype::Equipment* eq = Archetype::GetEquipment(item.iArchID);
        if (itemCart.remHoldSize < (eq->fVolume * item.iCount))
        {
            continue;
        }
        itemCart.remHoldSize -= static_cast<int>(eq->fVolume * item.iCount);
        pub::Player::AddCargo(clientId, item.iArchID, item.iCount, 1, false);

        PrintUserCmdText(clientId, L"Restock(%s): %d unit(s) loaded", item.wscDescription.c_str(), item.iCount);
    }

    static uint equipSound = CreateID("ui_load_cargo");
    pub::Audio::PlaySoundEffect(clientId, equipSound);

    PrintUserCmdText(clientId, L"Restock Cost: $%d", cost);
    Server.BaseEnter(baseId, clientId);
    Server.LocationEnter(location, clientId);
    
    base->Save();
}

void RearmamentModule::CheckPlayerInventory(uint clientId, PlayerBase* base)
{
    if (!base->isRearmamentAvailable || !base->isCrewSupplied)
    {
        return;
    }

    CUSTOM_AUTOBUY_CARTITEMS itemCart;
    itemCart.clientId = clientId;
    Plugin_Communication(PLUGIN_MESSAGE::CUSTOM_AUTOBUY_CART, &itemCart);

    if (itemCart.cartItems.empty())
    {
        return;
    }

    float sumToPay = 0;
    for (auto& item : itemCart.cartItems)
    {
        if (item.iCount == 0)
        {
            continue;
        }
        Archetype::Equipment* eq = Archetype::GetEquipment(item.iArchID);
        if (itemCart.remHoldSize < (eq->fVolume * item.iCount))
        {
            continue;
        }
        itemCart.remHoldSize -= static_cast<int>(eq->fVolume * item.iCount);
        auto gi = GoodList::find_by_id(item.iArchID);
        sumToPay += gi->fPrice * item.iCount;
    }

    if (sumToPay == 0)
    {
        return;
    }

    float actualToPay = 0;
    if (!base->archetype || !base->archetype->hasUnlimitedResupply)
    {
        for (auto& item : rearmamentCreditRatio)
        {
            float itemsToConsume = ceilf(sumToPay / item.second);
            uint itemCount = base->HasMarketItem(item.first);
            itemsToConsume = min(itemsToConsume, itemCount);
            actualToPay += itemsToConsume * item.second;
            if (actualToPay >= sumToPay)
            {
                break;
            }
        }
    }
    else
    {
        actualToPay = itemCart.creditCost;
    }

    if (actualToPay < sumToPay)
    {
        return;
    }

    if (Players[clientId].iInspectCash < actualToPay)
    {
        PrintUserCmdText(clientId, L"Rearmament available but you don't have enough credits (%u needed)", static_cast<int>(base->rearmamentCostPerCredit * actualToPay));
        return;
    }

    PrintUserCmdText(clientId, L"Rearmament available (type /restock), cost %d credits. (%0.0f%% of normal repair cost)", 
        static_cast<int>(base->rearmamentCostPerCredit * actualToPay), (base->rearmamentCostPerCredit * 100.f));
}