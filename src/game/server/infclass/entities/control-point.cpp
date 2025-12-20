#include "control-point.h"

#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/entities/ic_character.h>
#include <game/server/infclass/ic_gamecontroller.h>
#include <game/server/infclass/ic_player.h>

int CControlPoint::EntityId{};

static const float PortalConnectionTime = 5.0f;

CControlPoint::CControlPoint(CGameContext *pGameContext, const vec2 &Pos) :
	CPlacedObject(pGameContext, EntityId, Pos)
{
	GameWorld()->InsertEntity(this);
	m_Radius = Config()->m_InfControlPointCaptionRadius;
	m_ProximityRadius = m_Radius;

	for(int i = 0; i < NUM_IDS; i++)
	{
		m_Ids[i] = Server()->SnapNewId();
	}

	StartMeridiansVisualEffect();
}

CControlPoint::~CControlPoint()
{
	for(int i = 0; i < NUM_IDS; i++)
	{
		Server()->SnapFreeId(m_Ids[i]);
	}
}

void CControlPoint::Snap(int SnappingClient)
{
	vec2 *ParticlePos = nullptr;

	// Draw AntiPing white hole effect
	vec2 AntipingParticlePos[NUM_IDS];
	static_assert(sizeof(AntipingParticlePos) == sizeof(m_ParticlePos), "Default and antiping versions must have the same SnapIDs count");

	const CIcPlayer *pPlayer = GameController()->GetPlayer(SnappingClient);
	const bool AntiPing = pPlayer && pPlayer->GetAntiPingEnabled();

	if(AntiPing)
	{
		// Draw medians

		static_assert(NUM_HINT == 12, "The antiping drawing code is hardcoded for 12 IDs");
		PrepareAntipingParticles(AntipingParticlePos);
		ParticlePos = AntipingParticlePos;
	}
	else
	{
		// AntiPing is off, draw precomputed particles
		ParticlePos = m_ParticlePos;
	}

	for(int i = 0; i < NUM_IDS; i++)
	{
		CNetObj_Projectile *pObj = Server()->SnapNewItem<CNetObj_Projectile>(m_Ids[i]);
		if(pObj)
		{
			pObj->m_X = ParticlePos[i].x;
			pObj->m_Y = ParticlePos[i].y;
			pObj->m_VelX = 0;
			pObj->m_VelY = 0;
			pObj->m_StartTick = Server()->Tick();
			pObj->m_Type = WEAPON_HAMMER;
		}
	}

	if(IsTaken() && !IsBlocked())
	{
		if(!m_Infected)
		{
			int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
			int NetworkType = POWERUP_ARMOR;
			int Subtype = 0;
			GameServer()->SnapPickup(CSnapContext(SnappingClientVersion, Server()->IsSixup(SnappingClient)), GetId(), m_Pos, NetworkType, Subtype);
		}
	}
}

void CControlPoint::Tick()
{
	if(IsMarkedForDestroy())
		return;

	SetReady(IsTaken() && (Server()->Tick() >= GetReadyTick()));
	m_SpeedMultiplier = CalcSpeedMultiplier();

	MoveParallelsParticles();
	MoveMeridiansParticles();

	float MaxDistance = GetProximityRadius() + TileSizeF;
	ClientsArray Targets;
	using PlayersArray = icArray<CIcPlayer *, MAX_CLIENTS>;
	GameController()->GetSortedTargetsInRange(GetPos(), MaxDistance, ClientsArray(), &Targets);

	PlayersArray PlayersInRadius;
	if(!Targets.IsEmpty())
	{
		bool HasHumans = false;
		bool HasInfected = false;

		for(int TargetID : Targets)
		{
			CIcPlayer *pTarget = GameController()->GetPlayer(TargetID);
			if (pTarget->GetCharacter()->IsSolo())
				continue;

			if(pTarget->IsHuman())
			{
				PlayersInRadius.Add(pTarget);
				HasHumans = true;
			}
			else
			{
				PlayersInRadius.Add(pTarget);
				HasInfected = true;
			}
		}

		SetBlocked(HasHumans && HasInfected);
	}

	if(IsBlocked())
	{
		++m_NextEffectTick;
		++m_OwnershipChangedTick;
		return;
	}

	if(!PlayersInRadius.IsEmpty())
	{
		TakeControlPoint(PlayersInRadius.First());
	}

	if(!IsReady())
	{
		return;
	}

	if(IsTaken())
	{
		if(m_Infected && false)
		{
			int CurrentTick = Server()->Tick() - m_OwnershipChangedTick;
			if((CurrentTick % 27) == 0)
			{
				GameServer()->CreatePlayerSpawn(GetPos());
			}
		}
	}

	if(Server()->Tick() == m_NextEffectTick)
	{
		GameController()->ApplyControlPointEffect(this);
	}
}

void CControlPoint::TickPaused()
{
	if(m_OwnershipChangedTick)
		++m_OwnershipChangedTick;

	++m_NextEffectTick;
}

void CControlPoint::StartParallelsVisualEffect()
{
	MoveParallelsParticles();
}

void CControlPoint::StartMeridiansVisualEffect()
{
	float Radius = m_Radius / 2;
	float RandomRadius, RandomAngle;
	float VecX, VecY;
	for(int i = 0; i < NUM_HINT; i++)
	{
		RandomRadius = random_float() * (Radius - 4.0f);
		RandomAngle = 2.0f * pi * random_float();
		VecX = cos(RandomAngle);
		VecY = sin(RandomAngle);
		m_ParticlePos[i] = m_Pos + vec2(RandomRadius * VecX, RandomRadius * VecY);
		m_ParticleVec[i] = vec2(-VecX, -VecY);
	}
	// find out how long it takes for a particle to reach the mid
	RandomRadius = random_float() * (Radius - 4.0f);
	RandomAngle = 2.0f * pi * random_float();
	VecX = cos(RandomAngle);
	VecY = sin(RandomAngle);
	vec2 ParticlePos = m_Pos + vec2(Radius * VecX, Radius * VecY);
	vec2 ParticleVec = vec2(-VecX, -VecY);
	vec2 VecMid;
	float Speed;
	int i = 0;
	for(; i < 500; i++)
	{
		VecMid = m_Pos - ParticlePos;
		Speed = m_ParticleStartSpeed * clamp(1.0f - length(VecMid) / Radius + 0.5f, 0.0f, 1.0f);
		Speed *= m_SpeedMultiplier;
		ParticlePos += vec2(ParticleVec.x * Speed, ParticleVec.y * Speed);
		if(dot(VecMid, ParticleVec) <= 0)
			break;
		ParticleVec *= m_ParticleAcceleration;
	}
	m_ParticleStopTickTime = i;
}

void CControlPoint::MoveParallelsParticles()
{
	// Move in a circle around the center
	static const float AngleStep = 2.0f * pi / NUM_SIDE;
	for(int i = 0; i < NUM_SIDE; i++)
	{
		vec2 PosStart = m_Pos + vec2(m_Radius * cos(m_Angle + AngleStep * i), m_Radius * sin(m_Angle + AngleStep * i));
		m_ParticlePos[NUM_HINT + i] = PosStart;
	}

	const float AngleDelta = AngleStep / 20.0f * m_SpeedMultiplier;

	switch(GetPortalType())
	{
	case PortalType::Disconnected:
		m_Angle += AngleDelta;
		break;
	case PortalType::In:
		m_Angle += AngleDelta;
		break;
	case PortalType::Out:
		m_Angle -= AngleDelta;
		break;
	}

	const float MaxAngle = 2.0f * pi;
	if(m_Angle > MaxAngle)
	{
		m_Angle -= MaxAngle;
	}
	else if(m_Angle < -MaxAngle)
	{
		m_Angle += MaxAngle;
	}
}

void CControlPoint::MoveMeridiansParticles()
{
	// Move from/to the center to/from the outside
	float Radius = m_Radius;
	float MaxOutRadius = VisualRadius();
	float RandomAngle, Speed;
	float VecX, VecY;
	for(int i = 0; i < NUM_HINT; i++)
	{
		const vec2 ParticleRelativePos = m_Pos - m_ParticlePos[i];

		switch(GetPortalType())
		{
		case PortalType::Disconnected:
			if(dot(ParticleRelativePos, m_ParticleVec[i]) > 0)
			{
				Speed = m_ParticleStartSpeed * clamp(1.0f - length(ParticleRelativePos) / Radius + 0.5f, 0.0f, 1.0f);
				Speed *= m_SpeedMultiplier;
				m_ParticlePos[i] += vec2(m_ParticleVec[i].x * Speed, m_ParticleVec[i].y * Speed);
			}
			break;
		case PortalType::In:
			Speed = m_ParticleStartSpeed * clamp(1.0f - length(ParticleRelativePos) / Radius + 0.5f, 0.0f, 1.0f);
			Speed *= m_SpeedMultiplier;
			m_ParticlePos[i] += vec2(m_ParticleVec[i].x * Speed, m_ParticleVec[i].y * Speed);
			if(dot(ParticleRelativePos, m_ParticleVec[i]) <= 0)
			{
				RandomAngle = 2.0f * pi * random_float();
				VecX = cos(RandomAngle);
				VecY = sin(RandomAngle);
				m_ParticlePos[i] = m_Pos + vec2(Radius * VecX, Radius * VecY);
				m_ParticleVec[i] = vec2(-VecX, -VecY);
				continue;
			}
			break;
		case PortalType::Out:
			Speed = m_ParticleStartSpeed * clamp(length(ParticleRelativePos) / Radius + 0.5f, 0.0f, 1.0f);
			Speed *= m_SpeedMultiplier;
			m_ParticlePos[i] += vec2(m_ParticleVec[i].x * Speed, m_ParticleVec[i].y * Speed);
			if(length(ParticleRelativePos) > MaxOutRadius)
			{
				RandomAngle = 2.0f * pi * random_float();
				VecX = cos(RandomAngle);
				VecY = sin(RandomAngle);
				m_ParticlePos[i] = m_Pos;
				m_ParticleVec[i] = vec2(VecX, VecY);
				continue;
			}
			break;
		}

		m_ParticleVec[i] *= m_ParticleAcceleration;
	}
}

float CControlPoint::CalcSpeedMultiplier()
{
	static const float c_InactivePortalAnimationSpeed = 0.25;
	if(GetPortalType() == PortalType::Disconnected)
		return c_InactivePortalAnimationSpeed;

	if(IsBlocked() || !IsTaken())
		return c_InactivePortalAnimationSpeed;

	const int ReadyTick = m_OwnershipChangedTick + PortalConnectionTime * Server()->TickSpeed();
	if(Server()->Tick() >= ReadyTick)
		return 1.0;

	const float WarmupProgress = (Server()->Tick() - m_OwnershipChangedTick) / float(PortalConnectionTime * Server()->TickSpeed());
	return c_InactivePortalAnimationSpeed + (1.0f - c_InactivePortalAnimationSpeed) * WarmupProgress * WarmupProgress;
}

void CControlPoint::PrepareAntipingParticles(vec2 *ParticlePos)
{
	const vec2 BasePosition = m_Pos;
	const float Scale = m_Radius / 2;
	const vec2 ArrowOffset = vec2(m_Radius * 0.25, 0);
	switch(GetPortalType())
	{
	case PortalType::Disconnected:
		for(int i = 0; i < NUM_HINT; i++)
		{
			ParticlePos[i] = m_Pos;
		}
		break;
	case PortalType::In:
		ParticlePos[0] = BasePosition + ArrowOffset + vec2(0.00, 0) * Scale;
		ParticlePos[1] = BasePosition + ArrowOffset + vec2(0.25, +0.3) * Scale;
		ParticlePos[2] = BasePosition + ArrowOffset + vec2(0.25, -0.3) * Scale;
		ParticlePos[3] = BasePosition + ArrowOffset + vec2(0.50, 0) * Scale;
		ParticlePos[4] = BasePosition + ArrowOffset + vec2(0.75, 0) * Scale;
		ParticlePos[5] = BasePosition + ArrowOffset + vec2(1.00, 0) * Scale;

		ParticlePos[6] = BasePosition - ArrowOffset + vec2(-0.00, 0.0) * Scale;
		ParticlePos[7] = BasePosition - ArrowOffset + vec2(-0.25, +0.3) * Scale;
		ParticlePos[8] = BasePosition - ArrowOffset + vec2(-0.25, -0.3) * Scale;
		ParticlePos[9] = BasePosition - ArrowOffset + vec2(-0.50, 0.0) * Scale;
		ParticlePos[10] = BasePosition - ArrowOffset + vec2(-0.75, 0.0) * Scale;
		ParticlePos[11] = BasePosition - ArrowOffset + vec2(-1.00, 0.0) * Scale;
		break;
	case PortalType::Out:
		ParticlePos[0] = BasePosition + ArrowOffset + vec2(1.00, 0) * Scale;
		ParticlePos[1] = BasePosition + ArrowOffset + vec2(0.75, +0.3) * Scale;
		ParticlePos[2] = BasePosition + ArrowOffset + vec2(0.75, -0.3) * Scale;
		ParticlePos[3] = BasePosition + ArrowOffset + vec2(0.50, 0) * Scale;
		ParticlePos[4] = BasePosition + ArrowOffset + vec2(0.25, 0) * Scale;
		ParticlePos[5] = BasePosition + ArrowOffset + vec2(0.00, 0) * Scale;

		ParticlePos[6] = BasePosition - ArrowOffset + vec2(-1.00, 0.0) * Scale;
		ParticlePos[7] = BasePosition - ArrowOffset + vec2(-0.75, +0.3) * Scale;
		ParticlePos[8] = BasePosition - ArrowOffset + vec2(-0.75, -0.3) * Scale;
		ParticlePos[9] = BasePosition - ArrowOffset + vec2(-0.50, 0.0) * Scale;
		ParticlePos[10] = BasePosition - ArrowOffset + vec2(-0.25, 0.0) * Scale;
		ParticlePos[11] = BasePosition - ArrowOffset + vec2(-0.00, 0.0) * Scale;
		break;
	}

	if(!IsReady())
	{
		ParticlePos[5] = ParticlePos[0];
		ParticlePos[11] = ParticlePos[6];
	}

	// Draw parallels
	static const float AngleStep = 2.0f * pi / NUM_SIDE;
	for(int i = 0; i < NUM_SIDE; i++)
	{
		vec2 PosStart = m_Pos + vec2(m_Radius * cos(AngleStep * i), m_Radius * sin(AngleStep * i));
		ParticlePos[NUM_HINT + i] = PosStart;
	}
}

CControlPoint::PortalType CControlPoint::GetPortalType() const
{
	return IsTaken() ? PortalType::Out : PortalType::Disconnected;
}

bool CControlPoint::IsReady() const
{
	return m_Ready;
}

bool CControlPoint::IsTaken() const
{
	return m_OwnershipChangedTick;
}

bool CControlPoint::IsBlocked() const
{
	return m_Blocked;
}

bool CControlPoint::IsInfected() const
{
	return m_Infected;
}

void CControlPoint::TakeControlPoint(CIcPlayer *pPlayer)
{
	if(IsTaken() && pPlayer->IsInfected() == m_Infected)
	{
		return;
	}

	m_Infected = pPlayer->IsInfected();
	m_InfectedPlayerId = pPlayer->GetCid();
	OnOwnershipChanged();
}

void CControlPoint::SetBlocked(bool Blocked)
{
	if(m_Blocked == Blocked)
		return;

	m_Blocked = Blocked;
}

int CControlPoint::GetInfectedCID()
{
	CIcPlayer *pPlayer = GameController()->GetPlayer(m_InfectedPlayerId);
	if(pPlayer && pPlayer->IsInfected())
		return m_InfectedPlayerId;

	int LastResortID = -1;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		const CIcPlayer *pPlayer = GameController()->GetPlayer(i);
		if(!pPlayer)
			continue;

		LastResortID = i;
		if(pPlayer->IsInfected())
		{
			m_InfectedPlayerId = i;
			return m_InfectedPlayerId;
		}
	}

	return LastResortID;
}

float CControlPoint::VisualRadius() const
{
	return g_Config.m_InfControlPointVisualRadius;
}

int CControlPoint::GetReadyTick()
{
	return m_OwnershipChangedTick + PortalConnectionTime * Server()->TickSpeed();
}

void CControlPoint::SetNextEffectTime(float Seconds)
{
	m_NextEffectTick = Server()->Tick() + Server()->TickSpeed() * Seconds;
}

void CControlPoint::SetReady(bool Ready)
{
	if(Ready == m_Ready)
		return;

	m_Ready = Ready;

	if(Ready)
	{
		GameController()->OnControlPointCaptured(this);
	}
}

void CControlPoint::OnOwnershipChanged()
{
	m_OwnershipChangedTick = Server()->Tick();
	SetReady(false);
}
