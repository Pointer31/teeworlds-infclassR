#include "healing_particle.h"

#include <game/server/infclass/entities/growingexplosion.h>
#include <game/server/infclass/entities/ic_character.h>

#include <game/server/gamecontext.h>

#include <engine/server.h>

int CHealingParticle::EntityId{};

CHealingParticle::CHealingParticle(CGameContext *pGameContext, vec2 Pos, int Owner, vec2 Direction) :
	CIcEntity(pGameContext, EntityId, Pos, Owner)
{
	m_Direction = Direction;

	SetLifespan(1.0f);

	GameWorld()->InsertEntity(this);
	const float Vel = 16.0f;
	SetVelocity(m_Direction * Vel);
}

void CHealingParticle::Tick()
{
	vec2 PrevPos = m_Pos;
	CIcEntity::Tick();

	vec2 NewPos = m_Pos;

	int Collide = GameServer()->Collision()->IntersectLine(PrevPos, NewPos, nullptr, &NewPos);

	const float ProjectileRadius = 32.0f;
	CharacterFilter HumansOnly = CIcCharacter::GetHumansFilter();
	CharacterFilter Filter = CIcCharacter::GetFilterAllOff(CIcCharacter::GetExceptCharacterFilter(GetOwner()), HumansOnly);
	const CEntity *pTargetChr = GameWorld()->IntersectCharacter(PrevPos, NewPos, ProjectileRadius, NewPos, Filter);

	if(pTargetChr || Collide || GameLayerClipped(NewPos) || Server()->Tick() >= m_EndTick)
	{
		int HealingExplosionRadius = 4;
		new CGrowingExplosion(GameServer(), NewPos, m_Direction, GetOwner(), HealingExplosionRadius, EGrowingExplosionEffect::HEAL_HUMANS);

		MarkForDestroy();
		return;
	}

	m_Pos = NewPos;
}

void CHealingParticle::Snap(int SnappingClient)
{
	if(!DoSnapForClient(SnappingClient))
		return;

	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	int NetworkType = POWERUP_HEALTH;
	int Subtype = 0;
	GameServer()->SnapPickup(CSnapContext(SnappingClientVersion, Server()->IsSixup(SnappingClient)), GetId(), m_Pos, NetworkType, Subtype);
}
