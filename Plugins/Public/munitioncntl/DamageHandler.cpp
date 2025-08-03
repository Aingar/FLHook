#include "MunitionCntl.h"

static FlMap<uint, FlMap<uint, float>>* shieldResistMap = (FlMap<uint, FlMap<uint, float>>*)(0x658A9C0);

void FetchShipArmor(uint shipHash)
{
	if (shipArmorArch == shipHash)
	{
		return;
	}

	shipArmorIter = shipArmorMap.find(shipHash);
	shipArmorArch = shipHash;
	if (shipArmorIter == shipArmorMap.end())
	{
		shipArmorRating = 0;
	}
	else
	{
		shipArmorRating = shipArmorIter->second[1];
	}
}

void FetchSolarArmor(uint solarhash)
{
	if (solarArmorArch == solarhash)
	{
		return;
	}

	solarArmorIter = solarArmorMap.find(solarhash);
	solarArmorArch = solarhash;
	if (solarArmorIter == solarArmorMap.end())
	{
		solarArmorRating = 0;
	}
	else
	{
		solarArmorRating = solarArmorIter->second[1];
	}
}

float __fastcall GetWeaponModifier(CEShield* shield, void* edx, uint& weaponType)
{
	if (!weaponType || !shield || !shield->highestToughnessShieldGenArch)
	{
		return 1.0f;
	}
	auto shieldResistIter = shieldResistMap->find(weaponType);
	if (shieldResistIter == shieldResistMap->end() || !shieldResistIter.key())
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

	for (auto& distance : colGrpMultVector)
	{
		float dmgMult = colGrpMultSum > 1.0f ? distance.second / colGrpMultSum : distance.second;
		float damage = dmgMult * explosion->explosionArchetype->fHullDamage;
		float damageToDeal = damage * distance.first->colGrp->explosionResistance;
		if (explData && explData->percentageDamageHull)
		{
			damageToDeal += explData->percentageDamageHull * distance.first->colGrp->hitPts;
		}
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

	armorEnabled = false;
}

bool ShieldAndDistance(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg, float& rootDistance, ExplosionDamageData* explData)
{
	CEqObj* ceqobj = reinterpret_cast<CEqObj*>(iobj->cobj);

	PhySys::RayHit rayHits[20];
	int collisionCount = FindRayCollisions(ceqobj->system, explosion->explosionPosition, iobj->cobj->vPos, rayHits, 20);

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

	rootDistance = min(rootDistance, SquaredDistance3D(iobj->cobj->vPos, explosion->explosionPosition, 0));

	float detDist = explData ? explData->detDist : 0.0f;
	rootDistance -= detDist * detDist;
	rootDistance = max(rootDistance, 0.1f);

	CEShield* shield = reinterpret_cast<CEShield*>(ceqobj->equip_manager.FindFirst(Shield));
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
	else if (ceqobj->type == ObjectType::TradelaneRing && rootDistance < (520*520))
	{
		dmgMult = 1.0f;
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

void __stdcall ShipShieldDamage(IObjRW* iobj, CEShield* shield, float& incDmg, DamageList* dmg)
{
	returncode = DEFAULT_RETURNCODE;

	if (armorEnabled && weaponMunitionData && weaponMunitionData->percentageShieldDmg)
	{
		incDmg += shield->maxShieldHitPoints * (1.0f - shield->offlineThreshold) * weaponMunitionData->percentageShieldDmg;
	}

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

typedef void(__thiscall* SolarMunitionHitFunc)(IObjRW*, MunitionImpactData*, DamageList*);
SolarMunitionHitFunc SolarMunitionHitCall = SolarMunitionHitFunc(0x6D02D90);

void __fastcall SolarMunitionHit(IObjRW* solar, void* edx, MunitionImpactData* data, DamageList* dmg)
{
	if (!reinterpret_cast<CSolar*>(solar->cobj)->isDestructible)
	{
		SolarMunitionHitCall(solar, data, dmg);
		return;
	}

	if (weaponMunitionDataArch == data->munitionId->iArchID)
	{
		armorEnabled = true;
		SolarMunitionHitCall(solar, data, dmg);
		armorEnabled = false;
		return;
	}

	weaponMunitionDataArch = data->munitionId->iArchID;
	const auto munitionIter = munitionArmorPenMap.find(data->munitionId->iArchID);
	if (munitionIter == munitionArmorPenMap.end())
	{
		weaponMunitionData = nullptr;
	}
	else
	{
		weaponMunitionData = &munitionIter->second;
	}

	armorEnabled = true;

	SolarMunitionHitCall(solar, data, dmg);

	armorEnabled = false;
}

typedef void(__thiscall* ShipMunitionHitFunc)(IObjRW*, MunitionImpactData*, DamageList*);
ShipMunitionHitFunc ShipMunitionHitCall = ShipMunitionHitFunc(0x6CE9350);

void __fastcall ShipMunitionHit(IObjRW* iShip, void* edx, MunitionImpactData* data, DamageList* dmg)
{

	if (weaponMunitionDataArch == data->munitionId->iArchID)
	{
		armorEnabled = true;
		ShipMunitionHitCall(iShip, data, dmg);
		armorEnabled = false;
		return;
	}

	weaponMunitionDataArch = data->munitionId->iArchID;
	const auto munitionIter = munitionArmorPenMap.find(data->munitionId->iArchID);
	if (munitionIter == munitionArmorPenMap.end())
	{
		weaponMunitionData = nullptr;
	}
	else
	{
		weaponMunitionData = &munitionIter->second;
	}

	armorEnabled = true;

	ShipMunitionHitCall(iShip, data, dmg);

	armorEnabled = false;
}

void __stdcall SolarHullDamage(IObjRW* iobj, float& incDmg, DamageList* dmg)
{
	if (armorEnabled)
	{
		FetchSolarArmor(iobj->cobj->archetype->iArchID);

		int finalArmorValue = solarArmorRating;
		if (weaponMunitionData)
		{
			incDmg += iobj->cobj->archetype->fHitPoints * weaponMunitionData->percentageHullDmg;
			finalArmorValue = max(0, finalArmorValue - weaponMunitionData->armorPen);
		}

		if (finalArmorValue)
		{
			incDmg *= armorReductionVector.at(finalArmorValue);
		}
		armorEnabled = false;
	}
}

__declspec(naked) void SolarHullDamageNaked()
{
	__asm {
		push ecx
		push[esp + 0xC]
		lea eax, [esp + 0xC]
		push eax
		push ecx
		call SolarHullDamage
		pop ecx
		jmp[SolarHullDmgFunc]
	}
}

void __stdcall SolarColGrpDmg(IObjRW* iobj, CArchGroup* colGrp, float& incDmg, DamageList* dmg)
{
	if (!armorEnabled)
	{
		return;
	}

	if (weaponMunitionData && weaponMunitionData->percentageHullDmg)
	{
		incDmg += iobj->cobj->archetype->fHitPoints * weaponMunitionData->percentageHullDmg;
	}

	int colGrpArmor = 0;

	FetchSolarArmor(iobj->cobj->archetype->iArchID);

	if (solarArmorIter != solarArmorMap.end())
	{
		auto colGrpIter = solarArmorIter->second.find(colGrp->colGrp->id);
		if (colGrpIter != solarArmorIter->second.end())
		{
			colGrpArmor = colGrpIter->second;
		}
		else
		{
			colGrpArmor = solarArmorRating;
		}

		if (weaponMunitionData)
		{
			colGrpArmor = max(0, colGrpArmor - weaponMunitionData->armorPen);
		}

		if (colGrpArmor)
		{
			incDmg *= armorReductionVector.at(colGrpArmor);
		}
	}
	armorEnabled = false;
}

__declspec(naked) void SolarColGrpDamageNaked()
{
	__asm {
		push ecx
		push[esp + 0x10]
		lea eax, [esp + 0x10]
		push eax
		push[esp + 0x10]
		push ecx
		call SolarColGrpDmg
		pop ecx
		jmp[SolarColGrpDmgFunc]
	}
}

void __stdcall ShipColGrpDmg(IObjRW* iobj, CArchGroup* colGrp, float& incDmg, DamageList* dmg)
{
	if (armorEnabled)
	{
		if (weaponMunitionData && weaponMunitionData->percentageHullDmg)
		{
			incDmg += iobj->cobj->archetype->fHitPoints * weaponMunitionData->percentageHullDmg;
		}
		
		int colGrpArmor = 0;
		static auto shipIter = shipArmorMap.end();

		FetchShipArmor(iobj->cobj->archetype->iArchID);

		if (shipArmorIter != shipArmorMap.end())
		{
			auto colGrpIter = shipArmorIter->second.find(colGrp->colGrp->id);
			if (colGrpIter != shipArmorIter->second.end())
			{
				colGrpArmor = colGrpIter->second;
			}
			else
			{
				colGrpArmor = shipArmorRating;
			}

			if (weaponMunitionData)
			{
				colGrpArmor = max(0, colGrpArmor - weaponMunitionData->armorPen);
			}

			if (colGrpArmor)
			{
				incDmg *= armorReductionVector.at(colGrpArmor);
			}
		}
	}
	armorEnabled = false;
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

using ShipEnergyDamageType = void(__thiscall*)(IObjRW*, float incDmg, DamageList* dmg);
ShipEnergyDamageType ShipEnergyDamageFunc = ShipEnergyDamageType(0x6CEAFC0);
void __fastcall ShipEnergyDamage(IObjRW* iobj, void* edx, float incDmg, DamageList* dmg)
{
	if (armorEnabled && weaponMunitionData && weaponMunitionData->percentageEnergyDmg)
	{
		incDmg += reinterpret_cast<CShip*>(iobj->cobj)->maxPower * weaponMunitionData->percentageEnergyDmg;
	}

	ShipEnergyDamageFunc(iobj, incDmg, dmg);
}


using ShipEquipDamageType = void(__thiscall*)(IObjRW*, CEquip*, float incDmg, DamageList* dmg);
ShipEquipDamageType ShipEquipDamageFunc = ShipEquipDamageType(0x6CEA4A0);
void __fastcall ShipEquipDamage(IObjRW* iobj, void* edx, CAttachedEquip* equip, float incDmg, DamageList* dmg)
{
	if (armorEnabled && weaponMunitionData && weaponMunitionData->percentageHullDmg)
	{
		incDmg += incDmg * weaponMunitionData->percentageHullDmg;
	}

	if (!iobj->is_player())
	{
		ShipEquipDamageFunc(iobj, equip, incDmg, dmg);
		return;
	}

	auto cship = iobj->cobj;
	auto invulData = invulMap.find(cship->id);
	if (invulData == invulMap.end() || invulData->second.invulType == InvulType::HULLONLY)
	{
		ShipEquipDamageFunc(iobj, equip, incDmg, dmg);
		return;
	}
	float minHpAllowed = invulData->second.minHpPerc * equip->archetype->fHitPoints;
	if (equip->hitPts <= minHpAllowed)
	{
		return;
	}

	float hpPercPostDamage = equip->hitPts - incDmg;
	incDmg = min(incDmg, equip->hitPts - minHpAllowed);

	if (incDmg > 0.0f)
	{
		ShipEquipDamageFunc(iobj, equip, incDmg, dmg);
	}
}

void __fastcall CShipInit(CShip* ship, void* edx, CShip::CreateParms& parms)
{
	using CSHIPINIT = void(__thiscall*)(CShip*, CShip::CreateParms&);
	const static CSHIPINIT cshipInitFunc = CSHIPINIT(0x62B2690);
	
	cshipInitFunc(ship, parms);

	if (ship->ownerPlayer)
	{
		return;
	}

	CEquipTraverser tr(EquipmentClass::Gun);
	CELauncher* gun;
	while (gun = reinterpret_cast<CELauncher*>(ship->equip_manager.Traverse(tr)))
	{
		auto burstGunDataIter = burstGunData.find(gun->archetype->iArchID);
		if (burstGunDataIter == burstGunData.end())
		{
			continue;
		}

		shipGunData[ship->id][gun->iSubObjId] =
		{ burstGunDataIter->second.magSize,burstGunDataIter->second.magSize, burstGunDataIter->second.reloadTime };
	}
}

FireResult __fastcall CELauncherFire(CELauncher* gun, void* edx, const Vector& pos)
{
	using CELAUNCHERFIRE = FireResult(__thiscall*)(CELauncher*, const Vector&);
	const static CELAUNCHERFIRE gunfirefunc = CELAUNCHERFIRE(0x62995C0);

	FireResult fireResult = gunfirefunc(gun, pos);
	if (fireResult != FireResult::Success)
	{
		return fireResult;
	}

	if (gun->owner->ownerPlayer)
	{
		return fireResult;
	}

	if (gun->owner->objectClass == CObject::CSHIP_OBJECT)
	{
		auto shipDataIter = shipGunData.find(gun->owner->id);
		if (shipDataIter == shipGunData.end())
		{
			return fireResult;
		}

		auto gunData = shipDataIter->second.find(gun->iSubObjId);
		if (gunData == shipDataIter->second.end())
		{
			return fireResult;
		}

		if (--gunData->second.bulletsLeft == 0)
		{
			gunData->second.bulletsLeft = gunData->second.maxMagSize;
			gun->refireDelayElapsed = gunData->second.reloadTime;
		}
	}

	return fireResult;
}

void __stdcall ShipEquipmentDestroyed(IObjRW* ship, CEquip* eq, DamageEntry::SubObjFate fate, DamageList* dmgList)
{
	if (eq->CEquipType != EquipmentClass::ShieldGenerator)
	{
		return;
	}

	CShip* cship = reinterpret_cast<CShip*>(ship->cobj);
	CEShield* shield = reinterpret_cast<CEShield*>(cship->equip_manager.FindFirst(EquipmentClass::Shield));
	if (!shield)
	{
		return;
	}

	if (shield->linkedShieldGen.size() == 1)
	{
		ship->cequip_death(shield, fate, dmgList);
	}
}

__declspec(naked) void ShipEquipmentDestroyedNaked()
{
	__asm {
		push ecx
		push[esp + 0x10]
		push[esp + 0x10]
		push[esp + 0x10]
		push ecx
		call ShipEquipmentDestroyed
		pop ecx
		jmp[ShipEquipDestroyedFunc]
	}
}