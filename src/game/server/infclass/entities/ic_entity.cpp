#include "ic_entity.h"

#include <base/tl/ic_array.h>

#include <game/animation.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/ic_gamecontroller.h>

CIcEntity::CIcEntity(CGameContext *pGameContext, int ObjectType, vec2 Pos, std::optional<int> Owner,
	int ProximityRadius) :
	CEntity(pGameContext->GameWorld(), ObjectType, Pos, ProximityRadius)
{
	// dbg_assert(ObjectType != 0, "Invalid ObjectType. Ensure that the type is registered via RegisterEntityType().");
	SetOwner(Owner.value_or(-1));
}

CIcGameController *CIcEntity::GameController() const
{
	return static_cast<CIcGameController*>(GameServer()->m_pController);
}

void CIcEntity::SetOwner(int ClientId)
{
	if(ClientId < 0)
	{
		m_Owner.reset();
	}
	else
	{
		m_Owner = ClientId;
	}
}

CIcCharacter *CIcEntity::GetOwnerCharacter() const
{
	return m_Owner.has_value() ? GameController()->GetCharacter(m_Owner.value()) : nullptr;
}

EntityFilter CIcEntity::GetOwnerFilterFunction(int Owner)
{
	static int s_FilterOwnerId{};
	s_FilterOwnerId = Owner;

	const auto OwnerFilter = [](const CEntity *pEntity) {
		const CIcEntity *pInfEntity = static_cast<const CIcEntity *>(pEntity);
		return pInfEntity->GetOwner() == s_FilterOwnerId;
	};

	return OwnerFilter;
}

EntityFilter CIcEntity::GetOwnerFilterFunction()
{
	return GetOwnerFilterFunction(GetOwner());
}

EntityFilter CIcEntity::GetExceptEntitiesFilterFunction(const icArray<const CEntity *, 10> &aEntities)
{
	static icArray<const CEntity *, 10> s_aFilterEntities;
	s_aFilterEntities = aEntities;

	const auto ExceptEntitiesFilter = [](const CEntity *pEntity) {
		return !s_aFilterEntities.Contains(pEntity);
	};

	return ExceptEntitiesFilter;
}

EntityFilter CIcEntity::GetOnlyEntitiesFilterFunction(const icArray<const CEntity *, 10> &aEntities)
{
	static icArray<const CEntity *, 10> s_aFilterEntities;
	s_aFilterEntities = aEntities;

	const auto ExceptEntitiesFilter = [](const CEntity *pEntity) {
		return s_aFilterEntities.Contains(pEntity);
	};

	return ExceptEntitiesFilter;
}

void CIcEntity::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CIcEntity::Tick()
{
	if(m_EndTick.has_value() && (Server()->Tick() >= m_EndTick))
	{
		GameWorld()->DestroyEntity(this);
		return;
	}

	if(m_PosEnv >= 0)
	{
		SyncPosition();
	}
	else
	{
		m_Pos += m_Velocity;
	}
}

void CIcEntity::TickPaused()
{
	if (m_EndTick.has_value())
		++m_EndTick.value();
}

void CIcEntity::MoveTo(const vec2 &Position)
{
	SetPos(Position);
}

void CIcEntity::SetPos(const vec2 &Position)
{
	m_Pos = Position;
}

void CIcEntity::SetAnimatedPos(const vec2 &Pivot, const vec2 &RelPosition, int PosEnv)
{
	m_Pivot = Pivot;
	m_RelPosition = RelPosition;
	m_PosEnv = PosEnv;
}

float CIcEntity::GetLifespan() const
{
	if (!m_EndTick.has_value())
		return -1;

	int RemainingTicks = m_EndTick.value_or(0) - Server()->Tick();
	return RemainingTicks <= 0 ? 0 : RemainingTicks / static_cast<float>(Server()->TickSpeed());
}

void CIcEntity::SetLifespan(float Lifespan)
{
	m_EndTick = static_cast<int>(Server()->Tick() + Server()->TickSpeed() * Lifespan);
}

void CIcEntity::ResetLifespan()
{
	m_EndTick.reset();
}

bool CIcEntity::DoSnapForClient(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return false;

	return true;
}

void CIcEntity::SyncPosition()
{
	vec2 Position(0.0f, 0.0f);
	float Angle = 0.0f;
	if(m_PosEnv >= 0)
	{
		GetAnimationTransform(GameController()->GetTime(), m_PosEnv, GameServer()->Layers(), Position, Angle);
	}

	float x = (m_RelPosition.x * cosf(Angle) - m_RelPosition.y * sinf(Angle));
	float y = (m_RelPosition.x * sinf(Angle) + m_RelPosition.y * cosf(Angle));

	SetPos(Position + m_Pivot + vec2(x, y));
}
