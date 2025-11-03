/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/vmath.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/classes/ic_playerclass.h>
#include <game/server/infclass/classes/infected/infected.h>
#include <game/infclass/damage_type.h>
#include <game/server/infclass/ic_gamecontroller.h>
#include <game/server/infclass/ic_player.h>
#include <game/server/infclass/snap_filter.h>

#include "electric-box.h"
#include "game/server/infclass/entities/ic_entity.h"
#include "ic_character.h"

static const float g_BarrierMaxLength = 300.0;
static const float g_BarrierRadius = 0.0;

int CElectricBox::EntityId{};

CElectricBox::CElectricBox(CGameContext *pGameContext, vec2 Pos1, int Owner)
	: CPlacedObject(pGameContext, EntityId, Pos1, Owner)
{
	m_InfClassObjectType = INFCLASS_OBJECT_TYPE_LASER_WALL;
	m_MaxLength = Config()->m_InfVoltageBoxRange;

	m_EndPointId = Server()->SnapNewId();
	m_EndPointId2 = Server()->SnapNewId();
	m_EndPointId3 = Server()->SnapNewId();
	m_EndPointId4 = Server()->SnapNewId();
	m_EndPointId5 = Server()->SnapNewId();
	m_EndPointId6 = Server()->SnapNewId();
	m_EndPointId7 = Server()->SnapNewId();
	m_Lives = Config()->m_InfVoltageBoxCharges;

	GameWorld()->InsertEntity(this);
}

CElectricBox::~CElectricBox()
{
	Server()->SnapFreeId(m_EndPointId);
}

void CElectricBox::Tick()
{
	CPlacedObject::Tick();

	if (m_Lives > 0 && (m_EndTick.has_value() ? (m_EndTick.value() - Server()->Tick()) : 1000) == 1)
	{
		ResetLifespan();
		ResetSecondPosition();
	}

	if (!GameServer()->GetPlayerChar(GetOwner()))
	{
		MarkForDestroy();
	}

	if(IsMarkedForDestroy())
		return;

	if (m_Lives > 0 && distance(m_Pos, GameServer()->GetPlayerChar(GetOwner())->m_Pos) > m_MaxLength)
	{
		SetLifespan(0.0f);
		m_Lives = 0;
		return;
	}

	if(!HasSecondPosition())
	{
		m_SnapStartTick = Server()->Tick();
		return;
	}

	if(m_WallFlashTicks > 0)
		m_WallFlashTicks--;

	// Find other players
	for(TEntityPtr<CIcCharacter> p = GameWorld()->FindFirst<CIcCharacter>(); p; ++p)
	{
		if(p->IsHuman())
			continue;

		vec2 IntersectPos;
		if(!closest_point_on_line(m_Pos, GameServer()->GetPlayerChar(GetOwner())->m_Pos, p->m_Pos, IntersectPos))
			continue;

		float Len = distance(p->m_Pos, IntersectPos);
		if(Len < p->m_ProximityRadius + g_BarrierRadius)
		{
			OnHitInfected(p);
		}
	}

	PrepareSnapData();
}

void CElectricBox::TickPaused()
{
	CPlacedObject::TickPaused();

	++m_SnapStartTick;
}

void CElectricBox::Snap(int SnappingClient)
{
	if(!DoSnapForClient(SnappingClient))
		return;

	// if(!HasSecondPosition())
	// {
	// 	if(!SnapFiltersPassed(this, SnappingClient, EFilterFlag::Follower | EFilterFlag::SameTeam | EFilterFlag::Demo | EFilterFlag::Restricted))
	// 	{
	// 		return;
	// 	}
	// }

	if(Server()->GetClientInfclassVersion(SnappingClient))
	{
		CNetObj_InfClassObject *pInfClassObject = SnapInfClassObject();
		if(!pInfClassObject)
			return;

		if(true)
		{
			// Snap fake second position to fix OwnerIcon position
			pInfClassObject->m_EndTick = -1;
			pInfClassObject->m_Flags |= INFCLASS_OBJECT_FLAG_HAS_SECOND_POSITION;
			pInfClassObject->m_X2 = pInfClassObject->m_X;
			pInfClassObject->m_Y2 = pInfClassObject->m_Y - 1;
		}
	}

	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	CSnapContext Context(SnappingClientVersion);

	// GameServer()->SnapLaserObject(Context, GetId(), m_Pos, m_Pos, m_SnapStartTick, GetOwner());
	
	int extraTicks = 4;
	if(distance(m_Pos, GameServer()->GetPlayerChar(GetOwner())->m_Pos) > MaxLength() - 32.0*6.0)
		extraTicks = 5;
	if(HasSecondPosition())
		extraTicks = 0;

	GameServer()->SnapLaserObject(Context, m_EndPointId, m_Pos, GameServer()->GetPlayerChar(GetOwner())->m_Pos, Server()->Tick()-extraTicks, GetOwner());

	if(HasSecondPosition())
		extraTicks = 4;
	GameServer()->SnapLaserObject(Context, m_EndPointId2, {m_Pos.x-16, m_Pos.y-16}, {m_Pos.x-16, m_Pos.y+16}, Server()->Tick()-extraTicks, GetOwner());
	GameServer()->SnapLaserObject(Context, m_EndPointId3, {m_Pos.x-16, m_Pos.y+16}, {m_Pos.x+16, m_Pos.y+16}, Server()->Tick()-extraTicks, GetOwner());
	GameServer()->SnapLaserObject(Context, m_EndPointId4, {m_Pos.x+16, m_Pos.y+16}, {m_Pos.x+16, m_Pos.y-16}, Server()->Tick()-extraTicks, GetOwner());
	GameServer()->SnapLaserObject(Context, m_EndPointId5, {m_Pos.x+16, m_Pos.y-16}, {m_Pos.x-16, m_Pos.y-16}, Server()->Tick()-extraTicks, GetOwner());
}

void CElectricBox::OnHitInfected(CIcCharacter *pCharacter)
{
	CInfClassInfected *pInfected = CInfClassInfected::GetInstance(pCharacter);

	if(pCharacter->GetPlayer() && pInfected)
	{
		// pInfected->OnLaserWall();
		if (!pCharacter->IsFrozen()) {
			pCharacter->TakeDamage({}, 10, GetOwner(), EDamageType::LASER_WALL);
			pCharacter->Freeze(1.0, GetOwner(), FREEZEREASON_FLASH);
		}

		if(!pCharacter->CanDie())
		{
			return;
		}

		int LifeSpanReducer = ((Server()->TickSpeed()*Config()->m_InfBarrierTimeReduce)/100);
		m_WallFlashTicks = 10;

		if(pCharacter->GetPlayerClass() == EPlayerClass::Ghoul)
		{
			float Factor = pInfected->GetGhoulPercent();
			LifeSpanReducer += Server()->TickSpeed() * 5.0f * Factor;
		}

		// if (m_EndTick.has_value())
		// {
		// 	m_EndTick.value() -= LifeSpanReducer;
		// }
	}

	// pCharacter->Die(GetOwner(), EDamageType::LASER_WALL);
}

void CElectricBox::PrepareSnapData()
{
	const int RemainingTicks = m_EndTick.has_value() ? (m_EndTick.value() - Server()->Tick()) : 1000;

	// Laser dieing animation
	int LifeDiff = (50 - RemainingTicks)/10;
	// if(m_WallFlashTicks > 0) // flash laser for a few ticks when zombie jumps
	// 	LifeDiff = 5;
	// else if(RemainingTicks < 1 * Server()->TickSpeed())
	// 	LifeDiff = random_int(4, 5);
	// else if(RemainingTicks < 2 * Server()->TickSpeed())
	// 	LifeDiff = random_int(3, 5);
	// else if(RemainingTicks < 3 * Server()->TickSpeed())
	// 	LifeDiff = random_int(2, 4);
	// else if(RemainingTicks < 4 * Server()->TickSpeed())
	// 	LifeDiff = random_int(1, 3);
	// else if(RemainingTicks < 5 * Server()->TickSpeed())
	// 	LifeDiff = random_int(0, 2);
	// else if(RemainingTicks < 6 * Server()->TickSpeed())
	// 	LifeDiff = random_int(0, 1);
	// else if(RemainingTicks < 7 * Server()->TickSpeed())
	// 	LifeDiff = (random_prob(3.0f / 4.0f)) ? 1 : 0;
	// else if(RemainingTicks < 8 * Server()->TickSpeed())
	// 	LifeDiff = (random_prob(5.0f / 6.0f)) ? 1 : 0;
	// else if(RemainingTicks < 9 * Server()->TickSpeed())
	// 	LifeDiff = (random_prob(5.0f / 6.0f)) ? 0 : -1;
	// else if(RemainingTicks < 10 * Server()->TickSpeed())
	// 	LifeDiff = (random_prob(5.0f / 6.0f)) ? 0 : -1;
	// else if(RemainingTicks < 11 * Server()->TickSpeed())
	// 	LifeDiff = (random_prob(5.0f / 6.0f)) ? -1 : -Server()->TickSpeed() * 2;
	// else
	// 	LifeDiff = -Server()->TickSpeed() * 2;

	m_SnapStartTick = Server()->Tick() - LifeDiff;
}
