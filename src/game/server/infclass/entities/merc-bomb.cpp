/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>

#include "merc-bomb.h"

#include <game/infclass/damage_type.h>
#include <game/server/infclass/ic_gamecontroller.h>

#include "growingexplosion.h"
#include "ic_character.h"

int CMercenaryBomb::EntityId{};
static const float s_MercBombRadius = 80.0f;

CMercenaryBomb::CMercenaryBomb(CGameContext *pGameContext, vec2 Pos, int Owner)
	: CPlacedObject(pGameContext, EntityId, Pos, Owner, s_MercBombRadius)
{
	m_InfClassObjectType = INFCLASS_OBJECT_TYPE_MERCENARY_BOMB;
	GameWorld()->InsertEntity(this);
	m_LoadingTick = Server()->TickSpeed();
	m_Load = 0;

	for(int &Id : m_Ids)
	{
		Id = Server()->SnapNewId();
	}

	GameServer()->CreateSound(GetPos(), SOUND_PICKUP_ARMOR);
}

CMercenaryBomb::~CMercenaryBomb()
{
	for(int SnapId : m_Ids)
	{
		Server()->SnapFreeId(SnapId);
	}
}

void CMercenaryBomb::SetLoad(float Load)
{
	if (Load > m_Load)
		GameServer()->CreateSound(GetPos(), SOUND_PICKUP_ARMOR);

	m_Load = Load;

	float Factor = static_cast<float>(m_Load) / Config()->m_InfMercBombs;
	if (Factor > 1)
	{
		m_ProximityRadius = s_MercBombRadius * Factor;
	}
}

float CMercenaryBomb::GetLaserHitRadius() const
{
	return std::max<float>(s_MercBombRadius, GetProximityRadius());
}

void CMercenaryBomb::Tick()
{
	if(IsMarkedForDestroy())
		return;

	if(m_Load >= Config()->m_InfMercBombs && m_LoadingTick > 0)
		m_LoadingTick--;
	
	// Find other players
	CIcCharacter *pTriggerCharacter = nullptr;
	float ClosestLength = CCharacterCore::PhysicalSize() + GetProximityRadius();

	for(TEntityPtr<CIcCharacter> pChr = GameWorld()->FindFirst<CIcCharacter>(); pChr; ++pChr)
	{
		if(!pChr->IsInfected() || !pChr->CanDie())
			continue;

		float Len = distance(pChr->GetPos(), GetPos());

		if(Len < ClosestLength)
		{
			ClosestLength = Len;
			pTriggerCharacter = pChr;
		}
	}

	if(pTriggerCharacter)
	{
		Explode(pTriggerCharacter->GetCid());
	}
}

void CMercenaryBomb::Explode(int TriggeredBy)
{
	float Factor = static_cast<float>(m_Load) / Config()->m_InfMercBombs;

	if(m_Load > 1)
	{
		GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
		CGrowingExplosion *pExplosion = new CGrowingExplosion(GameServer(), m_Pos, vec2(0.0, -1.0), GetOwner(), 16.0f * Factor, EDamageType::MERCENARY_BOMB);
		pExplosion->SetTriggeredBy(TriggeredBy);
	}

	GameWorld()->DestroyEntity(this);
}

bool CMercenaryBomb::IsReadyToExplode() const
{
	return m_LoadingTick <= 0;
}

void CMercenaryBomb::Snap(int SnappingClient)
{
	if(!DoSnapForClient(SnappingClient))
		return;

	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	if(Server()->GetClientInfclassVersion(SnappingClient))
	{
		CNetObj_InfClassObject *pInfClassObject = SnapInfClassObject();
		if(!pInfClassObject)
			return;

		pInfClassObject->m_Data1 = f2fx(GetLaserHitRadius());
	}

	float AngleStart = (2.0f * pi * Server()->Tick()/static_cast<float>(Server()->TickSpeed()))/10.0f;
	float AngleStep = 2.0f * pi / static_cast<float>(CMercenaryBomb::NUM_SIDE);
	float R = 50.0f * static_cast<float>(m_Load) / Config()->m_InfMercBombs;

	CSnapContext Context(SnappingClientVersion, Server()->IsSixup(SnappingClient));
	for(int i = 0; i < CMercenaryBomb::NUM_SIDE; i++)
	{
		vec2 PosStart = m_Pos + vec2(R * cos(AngleStart + AngleStep*i), R * sin(AngleStart + AngleStep*i));
		GameServer()->SnapPickup(Context, m_Ids[i], PosStart, POWERUP_HEALTH, 0);
	}

	if(SnappingClient == GetOwner() && m_LoadingTick > 0)
	{
		R = GetProximityRadius();
		AngleStart = AngleStart * 2.0f;
		for(int i = 0; i < CMercenaryBomb::NUM_SIDE; i++)
		{
			vec2 PosStart = m_Pos + vec2(R * cos(AngleStart + AngleStep * i), R * sin(AngleStart + AngleStep * i));
			GameController()->SendHammerDot(PosStart, m_Ids[CMercenaryBomb::NUM_SIDE+i]);
		}
	}
}
