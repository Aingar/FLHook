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

float GetRayHitRange(CSimple* csimple, CArchGroup* colGrp, Vector& explosionPosition)
{
	Vector centerOfMass;
	float radius;
	colGrp->GetCenterOfMass(centerOfMass);
	colGrp->GetRadius(radius);

	PhySys::RayHit rayHits[20];
	int collisionCount = FindRayCollisions(csimple->system, explosionPosition, centerOfMass, rayHits, 20);


	float distance = SquaredDistance3D(centerOfMass, explosionPosition, radius);

	for (int i = 0; i < collisionCount; i++)
	{
		if (reinterpret_cast<CSimple*>(rayHits[i].cobj) != csimple)
		{
			continue;
		}
		Vector explosionVelocity = { explosionPosition.x - rayHits[i].position.x,
		explosionPosition.y - rayHits[i].position.y,
		explosionPosition.z - rayHits[i].position.z };

		distance = min(distance, explosionVelocity.x * explosionVelocity.x + explosionVelocity.y * explosionVelocity.y + explosionVelocity.z * explosionVelocity.z);
		break;
	}

	return distance;
}

void ShipExplosionHandlingExtEqColGrpHull(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg, float& rootDistance, ExplosionDamageData* explData)
{
	CShip* cship = reinterpret_cast<CShip*>(iobj->cobj);

	float squaredExplosionRadius = explosion->explosionArchetype->fRadius * explosion->explosionArchetype->fRadius;
	float twoThirds = squaredExplosionRadius * 0.4444444f;
	float oneThird = squaredExplosionRadius * 0.1111111f;

	static Vector centerOfMass;
	static float radius;

	CEquipTraverser tr(ExternalEquipment);
	CAttachedEquip* equip;
	while (equip = reinterpret_cast<CAttachedEquip*>(cship->equip_manager.Traverse(tr)))
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
			radius * radius;

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
		else if (distance < squaredExplosionRadius)
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


	float distanceSum = 0.0f;

	vector<pair<CArchGroup*, float>> distancesVector;
	{
		CArchGroup* colGrp;
		CArchGrpTraverser tr2;

		while (colGrp = cship->archGroupManager.Traverse(tr2))
		{
			if (colGrp->colGrp->explosionResistance == 0.0f)
			{
				continue;
			}

			float distance = GetRayHitRange(iobj->cobj, colGrp, explosion->explosionPosition);
			rootDistance = min(rootDistance, distance);

			if (!colGrp->colGrp->rootHealthProxy)
			{
				float colGrpDmgMult = 0.0f;
				if (distance < oneThird)
				{
					colGrpDmgMult = 1.0f;
				}
				else if (distance < twoThirds)
				{
					colGrpDmgMult = 0.6666f;
				}
				else if (distance < squaredExplosionRadius)
				{
					colGrpDmgMult = 0.3333f;
				}

				if (!colGrpDmgMult)
				{
					return;
				}

				float hullDmg = explosion->explosionArchetype->fHullDamage;

				if (explData && explData->percentageDamageHull)
				{
					hullDmg += explData->percentageDamageHull * colGrp->colGrp->hitPts;
				}

				iobj->damage_col_grp(colGrp, colGrpDmgMult* hullDmg* colGrp->colGrp->explosionResistance, dmg);
				continue;
			}

			if (distance > squaredExplosionRadius)
			{
				continue;
			}

			distance = max(0.01f, distance);
			distanceSum += distance;
			distancesVector.push_back({ colGrp, sqrtf(distance) });
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
	else if (rootDistance < squaredExplosionRadius)
	{
		dmgMult = 0.3333f;
	}

	if (!dmgMult)
	{
		return;
	}

	distanceSum += rootDistance;

	float unsquaredRootDistance = sqrtf(rootDistance);
	distanceSum = sqrtf(distanceSum);
	
	float totalDmg = 0.0f;
	{
		float rootExtraDamage = 0.0f;
		float rootMult = distanceSum / unsquaredRootDistance;
		float multSum = rootMult;
		for (auto& distance : distancesVector)
		{
			distance.second = distanceSum / distance.second;
			multSum += distance.second;
		}

		for (auto& distance : distancesVector)
		{
			float damage = dmgMult * explosion->explosionArchetype->fHullDamage * (distance.second / multSum);
			rootExtraDamage += damage;
			float damageToDeal = damage * distance.first->colGrp->explosionResistance;
			iobj->damage_col_grp(distance.first, damageToDeal, dmg);
			totalDmg += damageToDeal;
			ConPrint(L"rootColGrpDmg %u %0.0f %0.2f\n", distance.first->colGrp->id, damageToDeal, distance.first->colGrp->explosionResistance);
		}

		float hullDmg = explosion->explosionArchetype->fHullDamage;

		if (explData && explData->percentageDamageHull)
		{
			hullDmg += explData->percentageDamageHull * cship->archetype->fHitPoints;
		}

		float damageToDeal = dmgMult * hullDmg * cship->archetype->fExplosionResistance * (rootMult / multSum);
		iobj->damage_hull(damageToDeal + (rootExtraDamage * cship->archetype->fExplosionResistance), dmg);
		
		ConPrint(L"totalHullDamage rootextra %0.0f\nmult %0.2f\ndist %0.1fm\ndmg %0.0f\n", rootExtraDamage, dmgMult, unsquaredRootDistance, damageToDeal + (rootExtraDamage * cship->archetype->fExplosionResistance));
	}
}

bool ShieldAndDistance(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg, float& rootDistance, ExplosionDamageData* explData)
{
	CShip* cship = reinterpret_cast<CShip*>(iobj->cobj);

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
		return false;
	}

	CEShield* shield = reinterpret_cast<CEShield*>(cship->equip_manager.FindFirst(Shield));
	if (!shield || !shield->IsFunctioning())
	{
		return false;
	}

	float shieldDamage = (explosion->explosionArchetype->fHullDamage * ShieldEquipConsts::HULL_DAMAGE_FACTOR) + explosion->explosionArchetype->fEnergyDamage;

	if (explData && explData->weaponType)
	{
		shieldDamage *= GetWeaponModifier(shield, nullptr, explData->weaponType);
	}

	float squaredExplosionRadius = explosion->explosionArchetype->fRadius * explosion->explosionArchetype->fRadius;
	float twoThirds = squaredExplosionRadius * 0.4444444f;
	float oneThird = squaredExplosionRadius * 0.1111111f;

	float dmgMult = 0.0f;
	if (rootDistance < oneThird)
	{
		dmgMult = 1.0f;
	}
	else if (rootDistance < twoThirds)
	{
		dmgMult = 0.6666f;
	}
	else if (rootDistance < squaredExplosionRadius)
	{
		dmgMult = 0.3333f;
	}

	if (!dmgMult)
	{
		return false;
	}

	if (explData && explData->cruiseDisrupt)
	{
		dmg->damageCause = DamageCause::CruiseDisrupter;
	}

	ConPrint(L"ExplShieldDebug: dist mult: %0.2f, RayCastDist: %0.1fm, CenterDist: %0.1fm, explRadius: %0.0fm, baseDmg: %0.0f, finaldmg: %0.0f\n", dmgMult, sqrtf(rootDistance), Distance3D(iobj->cobj->vPos, explosion->explosionPosition), sqrtf(squaredExplosionRadius), shieldDamage, dmgMult * shieldDamage);
	float damage = dmgMult * shieldDamage;
	iobj->damage_shield_direct(shield, damage, dmg);

	return true;
}

void EnergyExplosionHit(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg, const float rootDistance, ExplosionDamageData* explData)
{
	float squaredExplosionRadius = explosion->explosionArchetype->fRadius * explosion->explosionArchetype->fRadius;
	float twoThirds = squaredExplosionRadius * 0.4444444f;
	float oneThird = squaredExplosionRadius * 0.1111111f;

	float dmgMult = 0.3333f;
	if (rootDistance < oneThird)
	{
		dmgMult = 1.0f;
	}
	else if (rootDistance < twoThirds)
	{
		dmgMult = 0.6666f;
	}
	float damage = dmgMult * explosion->explosionArchetype->fEnergyDamage;
	if (explData && explData->percentageDamageEnergy)
	{
		damage += reinterpret_cast<CShip*>(iobj->cobj)->maxPower * explData->percentageDamageEnergy;
	}

	iobj->damage_energy(damage, dmg);
}

void __stdcall ExplosionHit(IObjRW* iobj, ExplosionDamageEvent* explosion, DamageList* dmg)
{
	returncode = NOFUNCTIONCALL;
	float rootDistance = FLT_MAX;
	const auto iter = explosionTypeMap.find(explosion->explosionArchetype->iID);
	const auto explData = iter == explosionTypeMap.end() ? nullptr : &iter->second;
	if (ShieldAndDistance(iobj, explosion, dmg, rootDistance, explData))
	{
		return;
	}

	ShipExplosionHandlingExtEqColGrpHull(iobj, explosion, dmg, rootDistance, explData);
	EnergyExplosionHit(iobj, explosion, dmg, rootDistance, explData);
}

void __stdcall ShipShieldDamage(IObjRW* iobj, float& incDmg)
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

		CShip* cship = reinterpret_cast<CShip*>(iobj->cobj);
		CEShield* shield = reinterpret_cast<CEShield*>(cship->equip_manager.FindFirst(Shield));
		if (shield && (shield->currShieldHitPoints - incDmg) <= (shield->maxShieldHitPoints * shield->offlineThreshold))
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
		push ecx
		call ShipShieldDamage
		pop ecx
		jmp[ShipShieldDamageOrigFunc]
	}
}