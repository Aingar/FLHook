#include "MunitionCntl.h"

static FlMap<uint, FlMap<uint, float>>* shieldResistMap = (FlMap<uint, FlMap<uint, float>>*)(0x658A9C0);

float __fastcall GetWeaponModifier(CEShield* shield, void* edx, uint& weaponType)
{
	if (!weaponType || !shield || !shield->highestToughnessShieldGenArch)
	{
		return 1.0f;
	}
	auto shieldResistIter = shieldResistMap->find(weaponType);
	if (shieldResistIter == shieldResistMap->end())
	{
		return 1.0f;
	}

	auto shieldResistMap2 = shieldResistIter.value();
	auto shieldResistIter2 = shieldResistMap2->find(shield->highestToughnessShieldGenArch->iShieldTypeID);
	if (shieldResistIter2 == shieldResistMap2->end() || !shieldResistIter2.key())
	{
		return 1.0f;
	}

	return *shieldResistIter2.value();
}

float SquaredDistance3D(Vector v1, Vector v2, float radius)
{
	float sq1 = v1.x - v2.x, sq2 = v1.y - v2.y, sq3 = v1.z - v2.z;
	return (sq1 * sq1 + sq2 * sq2 + sq3 * sq3 - radius * radius);
}

float Distance3D(Vector v1, Vector v2)
{
	float sq1 = v1.x - v2.x, sq2 = v1.y - v2.y, sq3 = v1.z - v2.z;
	return sqrtf(sq1 * sq1 + sq2 * sq2 + sq3 * sq3);
}

float GetRayHitRange(CSimple* csimple, CArchGroup* colGrp, Vector& explosionPosition, float& hullDistance)
{
	Vector centerOfMass;
	float radius;
	colGrp->GetCenterOfMass(centerOfMass);
	colGrp->GetRadius(radius);

	PhySys::RayHit rayHits[20];
	int collisionCount = FindRayCollisions(csimple->system, explosionPosition, centerOfMass, rayHits, 20);

	bool firstHit = true;
	float centerOfMassDistance = SquaredDistance3D(centerOfMass, explosionPosition, radius);
	float colGrpDistance = FLT_MAX;
	float colGrpDistance2 = FLT_MAX;

	for (int i = 0; i < collisionCount; i++)
	{
		if (reinterpret_cast<CSimple*>(rayHits[i].cobj) != csimple)
		{
			continue;
		}

		Vector explosionVelocity = { explosionPosition.x - rayHits[i].position.x,
		explosionPosition.y - rayHits[i].position.y,
		explosionPosition.z - rayHits[i].position.z };

		float rayDistance = explosionVelocity.x * explosionVelocity.x + explosionVelocity.y * explosionVelocity.y + explosionVelocity.z * explosionVelocity.z;

		if (firstHit)
		{
			hullDistance = min(hullDistance, rayDistance);
			firstHit = false;
		}

		colGrpDistance2 = colGrpDistance;
		colGrpDistance = rayDistance;
	}

	if (colGrpDistance2 != FLT_MAX)
	{
		return colGrpDistance2;
	}
	if (colGrpDistance != FLT_MAX)
	{
		return colGrpDistance;
	}
	return centerOfMassDistance;
}

void ShipExplosionHandlingExtEqColGrpHull(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg, float& rootDistance, ExplosionDamageData* explData)
{
	CEqObj* ceqobj = reinterpret_cast<CEqObj*>(iobj->cobj);

	float detonationDistance = 0.0f;
	if (explData && ceqobj->objectClass == CObject::CSHIP_OBJECT)
	{
		detonationDistance = explData->detDist;
		weaponArmorPenValue = explData->armorPen;
		weaponArmorPenArch = 0;
	}
	else
	{
		weaponArmorPenValue = 0;
		weaponArmorPenArch = 0;
	}

	float threeThirds = explosion->explosionArchetype->fRadius - detonationDistance;
	float twoThirds = (threeThirds * 0.666667f) + detonationDistance;
	float oneThird = (threeThirds * 0.333333f) + detonationDistance;
	threeThirds *= threeThirds;
	twoThirds *= twoThirds;
	oneThird *= oneThird;

	static Vector centerOfMass;
	static float radius;

	CEquipTraverser tr(ExternalEquipment);
	CAttachedEquip* equip;
	while (equip = reinterpret_cast<CAttachedEquip*>(ceqobj->equip_manager.Traverse(tr)))
	{
		if (equip->archetype->fExplosionResistance == 0.0f)
		{
			continue;
		}

		equip->GetCenterOfMass(centerOfMass);
		equip->GetRadius(radius);

		centerOfMass.x -= explosion->explosionPosition.x;
		centerOfMass.y -= explosion->explosionPosition.y;
		centerOfMass.z -= explosion->explosionPosition.z;

		float distance = centerOfMass.x * centerOfMass.x +
			centerOfMass.y * centerOfMass.y +
			centerOfMass.z * centerOfMass.z -
			radius * radius - detonationDistance;

		distance = max(distance, 0.1f);

		float eqDmgMult = 0.0f;

		rootDistance = min(rootDistance, distance);

		if (distance < oneThird)
		{
			eqDmgMult = 1.0f;
		}
		else if (distance < twoThirds)
		{
			eqDmgMult = 0.666666666f;
		}
		else if (distance < threeThirds)
		{
			eqDmgMult = 0.333333333f;
		}

		if (!eqDmgMult)
		{
			continue;
		}

		float hullDmg = explosion->explosionArchetype->fHullDamage;

		if (explData && explData->percentageDamageHull)
		{
			hullDmg += explData->percentageDamageHull * equip->archetype->fHitPoints;
		}

		float damageToDeal = eqDmgMult * hullDmg * equip->archetype->fExplosionResistance;


		iobj->damage_ext_eq(equip, damageToDeal, dmg);
	}


	float colGrpMultSum = 0;

	vector<pair<CArchGroup*, float>> colGrpMultVector;
	{
		CArchGroup* colGrp;
		CArchGrpTraverser tr2;

		while (colGrp = ceqobj->archGroupManager.Traverse(tr2))
		{
			if (colGrp->colGrp->explosionResistance == 0.0f)
			{
				continue;
			}

			float distance = GetRayHitRange(iobj->cobj, colGrp, explosion->explosionPosition, rootDistance);
			distance -= detonationDistance;
			distance = max(distance, 0.1f);

			float colGrpDmgMult = 0.0f;
			if (distance < oneThird)
			{
				colGrpDmgMult = 1.0f;
			}
			else if (distance < twoThirds)
			{
				colGrpDmgMult = 0.6666f;
			}
			else if (distance < threeThirds)
			{
				colGrpDmgMult = 0.3333f;
			}

			if (!colGrpDmgMult)
			{
				continue;
			}

			float hullDmg = explosion->explosionArchetype->fHullDamage;

			if (explData && explData->percentageDamageHull)
			{
				hullDmg += explData->percentageDamageHull * colGrp->colGrp->hitPts;
			}

			if (!colGrp->colGrp->rootHealthProxy)
			{
				if (ceqobj->objectClass == CObject::CSHIP_OBJECT)
				{
					armorEnabled = true;
				}
				float damage = colGrpDmgMult * hullDmg * colGrp->colGrp->explosionResistance;
				iobj->damage_col_grp(colGrp, damage, dmg);

				continue;
			}

			colGrpMultSum += colGrpDmgMult;
			colGrpMultVector.push_back({ colGrp, colGrpDmgMult });
		}
	}

	float dmgMult = 0.0f;
	if (rootDistance < oneThird)
	{
		dmgMult = 1.0f;
	}
	else if (rootDistance < twoThirds)
	{
		dmgMult = 0.6666f;
	}
	else if (rootDistance < threeThirds)
	{
		dmgMult = 0.3333f;
	}

	if (!dmgMult)
	{
		return;
	}

	rootDistance = max(rootDistance, 0.1f);
	
	float hullDmgBudget = explosion->explosionArchetype->fHullDamage;
	if (explData && explData->percentageDamageHull)
	{
		hullDmgBudget += explData->percentageDamageHull * ceqobj->archetype->fHitPoints;
	}
	hullDmgBudget *= ceqobj->archetype->fExplosionResistance;

	{

		for (auto& distance : colGrpMultVector)
		{
			float dmgMult = colGrpMultSum > 1.0f ? distance.second / colGrpMultSum : distance.second;
			float damage = dmgMult * explosion->explosionArchetype->fHullDamage;
			float damageToDeal = damage * distance.first->colGrp->explosionResistance;
			armorEnabled = true;
			iobj->damage_col_grp(distance.first, damageToDeal, dmg);
			hullDmgBudget -= damageToDeal;
		}

		if (hullDmgBudget <= 0.0f)
		{
			return;
		}

		if (ceqobj->objectClass == CObject::CSHIP_OBJECT)
		{
			armorEnabled = true;
		}
		iobj->damage_hull(hullDmgBudget, dmg);
	}

	armorEnabled = false;
}

bool ShieldAndDistance(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg, float& rootDistance, ExplosionDamageData* explData)
{
	CEqObj* cship = reinterpret_cast<CEqObj*>(iobj->cobj);

	PhySys::RayHit rayHits[20];
	int collisionCount = FindRayCollisions(cship->system, explosion->explosionPosition, iobj->cobj->vPos, rayHits, 20);

	for (int i = 0; i < collisionCount; i++)
	{
		if (reinterpret_cast<CSimple*>(rayHits[i].cobj) != iobj->cobj)
		{
			continue;
		}
		Vector explosionVelocity = { explosion->explosionPosition.x - rayHits[i].position.x,
		explosion->explosionPosition.y - rayHits[i].position.y,
		explosion->explosionPosition.z - rayHits[i].position.z };

		rootDistance = explosionVelocity.x * explosionVelocity.x + explosionVelocity.y * explosionVelocity.y + explosionVelocity.z * explosionVelocity.z;
		break;
	}

	if (rootDistance == FLT_MAX)
	{
		rootDistance = SquaredDistance3D(iobj->cobj->vPos, explosion->explosionPosition, 0);
	}

	float detDist = explData ? explData->detDist : 0.0f;
	rootDistance -= detDist * detDist;
	rootDistance = max(rootDistance, 0.1f);

	CEShield* shield = reinterpret_cast<CEShield*>(cship->equip_manager.FindFirst(Shield));
	if (!shield || !shield->IsFunctioning())
	{
		return false;
	}

	float shieldDamage = (explosion->explosionArchetype->fHullDamage * ShieldEquipConsts::HULL_DAMAGE_FACTOR) + explosion->explosionArchetype->fEnergyDamage;

	if (explData)
	{
		if (explData->percentageDamageShield)
		{
			shieldDamage += explData->percentageDamageShield * shield->maxShieldHitPoints;
		}
		if (explData->weaponType)
		{
			shieldDamage *= GetWeaponModifier(shield, nullptr, explData->weaponType);
		}
	}

	float threeThirds = explosion->explosionArchetype->fRadius - detDist;
	float twoThirds = (threeThirds * 0.666667f) + detDist;
	float oneThird = (threeThirds * 0.333333f) + detDist;
	threeThirds *= threeThirds;
	twoThirds *= twoThirds;
	oneThird *= oneThird;

	float dmgMult = 0.0f;
	if (rootDistance < oneThird)
	{
		dmgMult = 1.0f;
	}
	else if (rootDistance < twoThirds)
	{
		dmgMult = 0.6666f;
	}
	else if (rootDistance < threeThirds)
	{
		dmgMult = 0.3333f;
	}
	else
	{
		return true;
	}

	float damage = dmgMult * shieldDamage;
	iobj->damage_shield_direct(shield, damage, dmg);

	return true;
}

void EnergyExplosionHit(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg, const float rootDistance, ExplosionDamageData* explData)
{
	float detDist = explData ? explData->detDist : 0.0f;

	float threeThirds = explosion->explosionArchetype->fRadius - detDist;
	float twoThirds = (threeThirds * 0.666667f) + detDist;
	float oneThird = (threeThirds * 0.333333f) + detDist;
	threeThirds *= threeThirds;
	twoThirds *= twoThirds;
	oneThird *= oneThird;

	float dmgMult;
	if (rootDistance < oneThird)
	{
		dmgMult = 1.0f;
	}
	else if (rootDistance < twoThirds)
	{
		dmgMult = 0.6666f;
	}
	else if (rootDistance < threeThirds)
	{
		dmgMult = 0.3333f;
	}
	else
	{
		return;
	}

	float damage = dmgMult * explosion->explosionArchetype->fEnergyDamage;
	if (explData && explData->percentageDamageEnergy)
	{
		damage += reinterpret_cast<CEqObj*>(iobj->cobj)->maxPower * explData->percentageDamageEnergy;
	}

	iobj->damage_energy(damage, dmg);
}

bool __stdcall ShipExplosionHit(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg)
{
	returncode = NOFUNCTIONCALL;
	float rootDistance = FLT_MAX;
	const auto iter = explosionTypeMap.find(explosion->explosionArchetype->iID);
	const auto explData = iter == explosionTypeMap.end() ? nullptr : &iter->second;
	if (ShieldAndDistance(iobj, explosion, dmg, rootDistance, explData))
	{
		return true;
	}

	ShipExplosionHandlingExtEqColGrpHull(iobj, explosion, dmg, rootDistance, explData);
	EnergyExplosionHit(iobj, explosion, dmg, rootDistance, explData);
	return false;
}

void __stdcall ShipShieldDamage(IObjRW* iobj, CEShield* shield, float& incDmg)
{
	uint clientId = iobj->cobj->ownerPlayer;
	if (!clientId)
	{
		return;
	}

	auto& shieldState = playerShieldState[clientId];
	if (!shieldState.damageReduction)
	{
		return;
	}
	if (!shieldState.boostUntil)
	{
		return;
	}
	mstime currTime = timeInMS();
	if (shieldState.boostUntil > currTime)
	{
		incDmg *= 1.0f - shieldState.damageReduction;
		shieldState.damageTaken += incDmg;

		if ((shield->currShieldHitPoints - incDmg) <= (shield->maxShieldHitPoints * shield->offlineThreshold))
		{
			uint fuseId = shieldFuseMap[clientId].boostData->fuseId;

			HkUnLightFuse(iobj, fuseId, 0.0f);

			shieldFuseMap.erase(clientId);

			shieldState.boostUntil = 0;
			shieldState.damageReduction = 0;
			shieldState.damageTaken = 0;
		}
	}
	else
	{
		shieldState.boostUntil = 0;
		shieldState.damageReduction = 0;
		shieldState.damageTaken = 0;
	}
}

__declspec(naked) void ShipShieldDamageNaked()
{
	__asm {
		push ecx
		lea eax, [esp + 0xC]
		push eax
		push [esp + 0xC]
		push ecx
		call ShipShieldDamage
		pop ecx
		jmp[ShipShieldDamageOrigFunc]
	}
}

bool __stdcall GuidedExplosionHit(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg)
{
	auto iter = explosionTypeMap.find(explosion->explosionArchetype->iID);
	if (iter != explosionTypeMap.end() && iter->second.missileDestroy)
	{
		float distance = HkDistance3D(iobj->cobj->vPos, explosion->explosionPosition);
		if (distance <= explosion->explosionArchetype->fRadius)
		{
			iobj->damage_hull(iobj->cobj->hitPoints, dmg);
			return true;
		}
	}
	return false;
}

__declspec(naked) void GuidedExplosionHitNaked()
{
	__asm
	{
		push ecx
		push[esp + 0xC]
		push[esp + 0xC]
		push ecx
		call GuidedExplosionHit
		pop ecx
		test al, al
		jz callOriginal
		ret 0x8

		callOriginal:
		jmp [GuidedExplosionHitOrigFunc]
	}
}

void __stdcall CheckSolarExplosionDamage(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg)
{
	ExplosionDamageData* explData = nullptr;
	auto iter = explosionTypeMap.find(explosion->explosionArchetype->iID);
	if (iter != explosionTypeMap.end())
	{
		if (!iter->second.damageSolars)
		{
			return;
		}
		explData = &iter->second;
	}

	float rootDistance = FLT_MAX;
	if (ShieldAndDistance(iobj, explosion, dmg, rootDistance, explData))
	{
		return;
	}

	ShipExplosionHandlingExtEqColGrpHull(iobj, explosion, dmg, rootDistance, explData);
	EnergyExplosionHit(iobj, explosion, dmg, rootDistance, explData);
}

__declspec(naked) void SolarExplosionHitNaked()
{
	__asm {
		push ecx
		push[esp + 0xC]
		push[esp + 0xC]
		push ecx
		call CheckSolarExplosionDamage
		pop ecx
		ret 0x8
	}
}

typedef void(__thiscall* ShipMunitionHitFunc)(IObjRW*, MunitionImpactData*, DamageList*);
ShipMunitionHitFunc ShipMunitionHitCall = ShipMunitionHitFunc(0x6CE9350);

void __fastcall ShipMunitionHit(IObjRW* iShip, void* edx, MunitionImpactData* data, DamageList* dmg)
{

	if (weaponArmorPenArch == data->munitionId->iArchID || SubObjectID::IsShieldEquipID(data->subObjId))
	{
		armorEnabled = true;
		ShipMunitionHitCall(iShip, data, dmg);
		armorEnabled = false;
		return;
	}

	weaponArmorPenArch = data->munitionId->iArchID;
	const auto munitionIter = munitionArmorPenMap.find(data->munitionId->iArchID);
	if (munitionIter == munitionArmorPenMap.end())
	{
		weaponArmorPenValue = 0;
	}
	else
	{
		weaponArmorPenValue = munitionIter->second;
	}

	armorEnabled = true;

	ShipMunitionHitCall(iShip, data, dmg);

	armorEnabled = false;
}

void __stdcall ShipColGrpDmg(IObjRW* iobj, CArchGroup* colGrp, float& incDmg, DamageList* dmg)
{
	if (armorEnabled)
	{
		if (shipArmorArch != iobj->cobj->archetype->iArchID)
		{
			shipArmorArch = iobj->cobj->archetype->iArchID;
			const auto shipIter = shipArmorMap.find(shipArmorArch);
			if (shipIter == shipArmorMap.end())
			{
				shipArmorRating = 0;
			}
			else
			{
				shipArmorRating = shipIter->second;
			}
		}

		if (shipArmorRating && shipArmorRating > weaponArmorPenValue)
		{
			incDmg *= armorReductionVector.at(shipArmorRating - weaponArmorPenValue);
		}

		armorEnabled = false;
	}
}

__declspec(naked) void ShipColGrpDmgNaked()
{
	__asm {
		push ecx
		push [esp+0x10]
		lea eax, [esp + 0x10]
		push eax
		push[esp + 0x10]
		push ecx
		call ShipColGrpDmg
		pop ecx
		jmp [ShipColGrpDmgFunc]
	}
}

void __stdcall ShipFuseLight(IObjRW* ship, uint fuseCause, uint* fuseId, ushort sId, float radius, float fuseLifetime)
{
	uint client = ship->cobj->ownerPlayer;
	if (!client)
	{
		return;
	}
	auto shipDataIter = shipDataMap.find(ship->cobj->archetype->iArchID);
	if (shipDataIter == shipDataMap.end())
	{
		return;
	}
	
	auto hpMapIter = shipDataIter->second.fuseHpMap.find(*fuseId);
	if (hpMapIter == shipDataIter->second.fuseHpMap.end()
		|| hpMapIter->second.empty())
	{
		return;
	}

	for (auto& hp : hpMapIter->second)
	{
		FindAndDisableEquip(client, hp);
	}
}

__declspec(naked) void ShipFuseLightNaked()
{
	__asm {
		push ecx
		push [esp+0x18]
		push [esp+0x18]
		push [esp+0x18]
		push [esp+0x18]
		push [esp+0x18]
		push ecx
		call ShipFuseLight
		pop ecx
		jmp [ShipFuseLightFunc]
	}
}