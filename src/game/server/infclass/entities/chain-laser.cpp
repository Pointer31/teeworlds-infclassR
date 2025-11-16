/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "chain-laser.h"


#include <engine/shared/config.h>

#include <game/generated/protocol.h>
#include <game/infclass/damage_type.h>
#include <game/infclass/weapons.h>
#include <game/server/gamecontext.h>

#include <game/server/infclass/entities/ic_character.h>
#include <game/server/infclass/ic_gamecontroller.h>

#include "growingexplosion.h"
#include "ic_laser.h"

CChainLaser::CChainLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg, EInfclassWeapon InfClassWeapon) :
	CIcEntity(pGameContext, CGameWorld::ENTTYPE_LASER, Pos, Owner), m_Weapon(InfClassWeapon)
{
	m_Dmg = Dmg;
	m_Energy = StartEnergy;
	m_Dir = Direction;
	m_MaxBounces = GameServer()->Tuning()->m_LaserBounceNum;
	m_BounceCost = GameServer()->Tuning()->m_LaserBounceCost;
	m_ZombiesHit = 0;
	m_ToZombiesSnaps[0] = Server()->SnapNewId();

	GameWorld()->InsertEntity(this);
}

bool CChainLaser::HitTarget(vec2 From, vec2 To)
{
	vec2 At;
	CIcCharacter *pOwnerChar = GameController()->GetCharacter(GetOwner());
	icArray<const CIcCharacter *, 10> IgnoreHits;
	if(m_IgnoreTarget.has_value())
	{
		CIcCharacter *pIgnoreChar = GameController()->GetCharacter(m_IgnoreTarget.value());
		if(pIgnoreChar)
		{
			IgnoreHits.Add(pIgnoreChar);
		}
		m_IgnoreTarget.reset();
	}
	CharacterFilter HitsFilter = CIcCharacter::GetExceptCharactersFilter(IgnoreHits);
	const bool IsInfected = pOwnerChar && pOwnerChar->IsInfected();
	CharacterFilter OnlyOtherTeamFilter = IsInfected ? CIcCharacter::GetHumansFilter() : CIcCharacter::GetInfectedFilter();
	CharacterFilter CombinedFilter = CIcCharacter::GetFilterAllOff(HitsFilter, OnlyOtherTeamFilter);

	CCharacter *pIntersect = GameWorld()->IntersectCharacter(From, To, 0.f, At, CombinedFilter);
	CIcCharacter *pHit = CIcCharacter::GetInstance(pIntersect);

	while(pHit)
	{
		if(pHit->IsReflectingProjectiles())
		{
			const vec2 RadiusVector = normalize(At - pHit->GetPos());
			m_Dir = m_Dir + RadiusVector * 2;

			// Ignore the target to ensure that the reflected laser
			// won't hit it at the `At` position on the next Bounce()
			m_IgnoreTarget = pHit->GetCid();
			DoReflect(At);
			return true;
		}

		IgnoreHits.Add(pHit);
		bool Confirmed = OnCharacterHit(pHit, At);
		if(Confirmed)
		{
			m_From = From;
			m_Pos = At;
			m_Energy = -1;

			int alreadyHit = 0;
			printf("ZOMBIE HIT\n");

			TEntityPtr<CIcCharacter> pChrClosest = NULL;
			int ClosestDistance = 1000000;

			int ignoreCIDS[10] = {pHit->GetCid(),-1,-1,-1,-1,-1,-1,-1,-1,-1};
			
			icArray<CIcCharacter *, MAX_CLIENTS> aCharacters;
			int Results = GameWorld()->FindEntities(m_Pos, 20000, reinterpret_cast<CEntity **>(aCharacters.Data()), aCharacters.Capacity(), CIcCharacter::EntityId);
			aCharacters.Resize(Results);

			for(const CIcCharacter *pChr : aCharacters)
			{
				if(!pChr->IsInfected())
					continue;

				bool cont = false;
				for (int i = 0; i < 10; i++)
					if (ignoreCIDS[i] == pChr->GetCid())
						cont = true;
				if (cont)
					continue;
				printf("ZOMBIE, %i\n", pChr->GetCid());

				float Len2 = distance_squared(pChr->GetPos(), m_Pos);

				if (Len2 < 20000 && Len2 < ClosestDistance) {
					printf("ZOMBIE CLOSEST YET, %i\n", pChr->GetCid());
					ClosestDistance = Len2;
					// pChrClosest = pChr;
					m_ToZombies[0] = pChr->GetPos();
					m_ZombiesHit = 1;
					// float StartEnergy = 200;
					// int Damage = GameServer()->Tuning()->m_LaserDamage;
					// vec2 Direction = {(pChr->GetPos().x - m_Pos.x)/Len2, (pChr->GetPos().y - m_Pos.y)/Len2};
					// CIcLaser::MakeLaser(GameServer(), {m_Pos.x + Direction.x*64, m_Pos.y + Direction.y*64}, Direction, StartEnergy, GetOwner(), Damage, EInfclassWeapon::ENGINEER_LASER);
				}

				alreadyHit++;
				if (alreadyHit >= 2)
					break;
			}
			return true;
		}

		pIntersect = GameWorld()->IntersectCharacter(m_Pos, To, 0.f, At, CombinedFilter);
		pHit = CIcCharacter::GetInstance(pIntersect);
	}

	return false;
}

bool CChainLaser::OnCharacterHit(CIcCharacter *pHit, const vec2 &At)
{
	float DamageLeft = 0;
	pHit->TakeDamage(vec2(0.f, 0.f), m_Dmg, GetOwner(), GetDamageType(), &DamageLeft);
	m_Dmg = DamageLeft / 2;

	return !m_Piercing || (m_Dmg < 1);
}

void CChainLaser::DoReflect(const vec2 &To)
{
	m_From = m_Pos;
	m_Pos = To;

	m_Energy -= distance(m_From, m_Pos) + m_BounceCost;
	m_Bounces++;

	// if(m_Bounces > m_MaxBounces)
		m_Energy = -1;

	// GameServer()->CreateSound(m_Pos, SOUND_LASER_BOUNCE);
}

void CChainLaser::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0)
	{
		if(m_Explosive)
		{
			new CGrowingExplosion(GameServer(), m_Pos, vec2(0.0, -1.0), GetOwner(), 1, GetDamageType());
		}

		GameWorld()->DestroyEntity(this);
		return;
	}

	vec2 To = m_Pos + m_Dir * m_Energy;

	if(GameServer()->Collision()->IntersectLineWeapon(m_Pos, To, nullptr, &To))
	{
		if(!HitTarget(m_Pos, To))
		{
			vec2 TempDir = m_Dir * 4.0f;
			GameServer()->Collision()->MovePoint(&To, &TempDir, 1.0f, nullptr);
			m_Dir = normalize(TempDir);

			DoReflect(To);
		}
	}
	else
	{
		if(!HitTarget(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;
			m_Energy = -1;
		}
	}
}

CChainLaser *CChainLaser::MakeLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg, EInfclassWeapon InfClassWeapon)
{
	CChainLaser *pLaser = new CChainLaser(pGameContext, Pos, Direction, StartEnergy, Owner, Dmg, InfClassWeapon);
	pLaser->DoBounce();
	return pLaser;
}

void CChainLaser::Tick()
{
	if(Server()->Tick() > m_EvalTick+(Server()->TickSpeed()*GameServer()->Tuning()->m_LaserBounceDelay)/1000.0f)
		DoBounce();
}

void CChainLaser::TickPaused()
{
	++m_EvalTick;
}

void CChainLaser::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient) && NetworkClipped(SnappingClient, m_From))
		return;

	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	CSnapContext Context(SnappingClientVersion);

	if (m_ZombiesHit > 0)
		GameServer()->SnapLaserObject(Context, m_ToZombiesSnaps[0], m_ToZombies[0], m_Pos, m_EvalTick, GetOwner(), m_SnapLaserType);
	GameServer()->SnapLaserObject(Context, GetId(), m_Pos, m_From, m_EvalTick, GetOwner(), m_SnapLaserType);
}

void CChainLaser::SetExplosive(bool Explosive)
{
	m_Explosive = Explosive;
}

void CChainLaser::SetPiercing(bool Piercing)
{
	m_Piercing = Piercing;
}

void CChainLaser::SetSnapType(int LaserType)
{
	m_SnapLaserType = LaserType;
}

EDamageType CChainLaser::GetDamageType() const
{
	switch(m_Weapon)
	{
	case EInfclassWeapon::LOOPER_LASER:
		return EDamageType::LOOPER_LASER;
	case EInfclassWeapon::SNIPER_RIFLE:
		return EDamageType::SNIPER_RIFLE;
	case EInfclassWeapon::LASER_TURRET:
		return EDamageType::TURRET_LASER;
	case EInfclassWeapon::ELECTRICIAN_SHOTGUN:
	case EInfclassWeapon::ENGINEER_LASER:
	case EInfclassWeapon::HERO_LASER:
		return EDamageType::LASER;

	default:
		dbg_assert(false, "Invalid GetDamageType() call");
		return EDamageType::LASER;
	}
}
