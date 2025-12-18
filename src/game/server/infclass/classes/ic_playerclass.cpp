#include "ic_playerclass.h"

#include <base/system.h>
#include <engine/shared/config.h>
#include <game/gamecore.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/entities/ic_character.h>
#include <game/server/infclass/ic_gamecontroller.h>
#include <game/server/infclass/ic_player.h>
#include <game/server/teeinfo.h>

CIcPlayerClass::CIcPlayerClass(CIcPlayer *pPlayer)
	: m_pPlayer(pPlayer)
{
	m_NormalEmote = EMOTE_NORMAL;
}

CGameContext *CIcPlayerClass::GameContext() const
{
	if(m_pPlayer)
		return m_pPlayer->GameServer();

	return nullptr;
}

// A lot of code use GameServer() as CGameContext* getter, so let it be.
CGameContext *CIcPlayerClass::GameServer() const
{
	return GameContext();
}

CGameWorld *CIcPlayerClass::GameWorld() const
{
	if(m_pPlayer)
		return m_pPlayer->GameServer()->GameWorld();

	return nullptr;
}

CIcGameController *CIcPlayerClass::GameController() const
{
	if(m_pPlayer)
		return m_pPlayer->GameController();

	return nullptr;
}

CConfig *CIcPlayerClass::Config()
{
	if(m_pPlayer)
		return m_pPlayer->GameServer()->Config();

	return nullptr;
}

const CConfig *CIcPlayerClass::Config() const
{
	if(m_pPlayer)
		return m_pPlayer->GameServer()->Config();

	return nullptr;
}

IServer *CIcPlayerClass::Server() const
{
	if(m_pPlayer)
		return m_pPlayer->Server();

	return nullptr;
}

CIcPlayer *CIcPlayerClass::GetPlayer()
{
	return m_pPlayer;
}

const CIcPlayer *CIcPlayerClass::GetPlayer() const
{
	return m_pPlayer;
}

int CIcPlayerClass::GetCid() const
{
	const CIcPlayer *pPlayer = GetPlayer();
	if(pPlayer)
		return pPlayer->GetCid();

	return -1;
}

vec2 CIcPlayerClass::GetPos() const
{
	if(m_pCharacter)
		return m_pCharacter->GetPos();

	return vec2(0, 0);
}

vec2 CIcPlayerClass::GetDirection() const
{
	if(m_pCharacter)
		return m_pCharacter->GetDirection();

	return vec2(0, 0);
}

vec2 CIcPlayerClass::GetProjectileStartPos(float Offset) const
{
	vec2 From = GetPos();
	vec2 To = From + GetDirection() * Offset;
	GameServer()->Collision()->IntersectLine(From, To, nullptr, &To);

	return To;
}

float CIcPlayerClass::GetProximityRadius() const
{
	if(m_pCharacter)
		return m_pCharacter->GetProximityRadius();

	return 0;
}

int CIcPlayerClass::GetUpgradeLevel() const
{
	return m_UpgradeLevel;
}

void CIcPlayerClass::ResetUpgradeLevel()
{
	m_UpgradeLevel = 0;
	m_Upgrades.Clear();
}

void CIcPlayerClass::SetCharacter(CIcCharacter *character)
{
	if(m_pCharacter == character)
	{
		return;
	}

	if(m_pCharacter)
	{
		DestroyChildEntities();
		m_pCharacter->SetClass(nullptr);
	}

	m_pCharacter = character;

	if(m_pCharacter)
	{
		m_pCharacter->SetClass(this);
		GiveClassAttributes();
	}
}

bool CIcPlayerClass::IsZombie() const
{
	return !IsHuman();
}

void CIcPlayerClass::ResetNormalEmote()
{
	SetNormalEmote(EMOTE_NORMAL);
}

void CIcPlayerClass::SetNormalEmote(int Emote)
{
	m_NormalEmote = Emote;
}

int CIcPlayerClass::GetDefaultEmote() const
{
	int EmoteNormal = m_NormalEmote;

	if(!m_pCharacter)
		return EmoteNormal;

	if(m_pCharacter->IsSleeping())
		EmoteNormal = EMOTE_BLINK;

	if(m_pCharacter->IsBlind())
		EmoteNormal = EMOTE_BLINK;

	if(m_pCharacter->IsInvisible())
		EmoteNormal = EMOTE_BLINK;

	if(m_pCharacter->IsInDeepDefence())
		EmoteNormal = EMOTE_BLINK;

	if(m_pCharacter->IsInLove())
		EmoteNormal = EMOTE_HAPPY;

	if(m_pCharacter->IsInSlowMotion() || m_pCharacter->HasHallucination())
		EmoteNormal = EMOTE_SURPRISE;

	if(m_pCharacter->IsFrozen())
	{
		if(m_pCharacter->GetFreezeReason() == FREEZEREASON_UNDEAD)
		{
			EmoteNormal = EMOTE_PAIN;
		}
		else
		{
			EmoteNormal = EMOTE_BLINK;
		}
	}

	return EmoteNormal;
}

CAmmoParams CIcPlayerClass::GetAmmoParams(int Weapon) const
{
	CAmmoParams Params;
	EInfclassWeapon InfWID = m_pCharacter->GetInfWeaponId(Weapon);
	Params.RegenInterval = GameController()->GetAmmoRegenTime(InfWID);
	Params.MaxAmmo = GameController()->GetMaxAmmo(InfWID);
	return Params;
}

int CIcPlayerClass::GetJumps() const
{
	// From DDNet:

	// Special jump cases:
	// Jumps == -1: A tee may only make one ground jump. Second jumped bit is always set
	// Jumps == 0: A tee may not make a jump. Second jumped bit is always set
	// Jumps == 1: A tee may do either a ground jump or an air jump. Second jumped bit is set after the first jump
	// The second jumped bit can be overridden by special tiles so that the tee can nevertheless jump.

	return 2; // Ground jump + Air jump
}

bool CIcPlayerClass::CanDie() const
{
	return true;
}

bool CIcPlayerClass::CanBeHit() const
{
	return true;
}

bool CIcPlayerClass::CanBeUnfreezed() const
{
	return true;
}

PlayerUpgradesArray CIcPlayerClass::GetUpgrade(int Level) const
{
	return {};
}

PlayerUpgradesArray CIcPlayerClass::GetNextUpgrade() const
{
	return GetUpgrade(GetUpgradeLevel() + 1);
}

float CIcPlayerClass::GetHammerProjOffset() const
{
	return GetProximityRadius() * 0.75f;
}

float CIcPlayerClass::GetHammerRange() const
{
	if (!m_pCharacter)
		return 0;

	const EInfclassWeapon Weapon = m_pCharacter->GetInfWeaponId(WEAPON_HAMMER);
	float Range = m_pCharacter->GetProximityRadius() * 0.5f;
	if (Weapon == EInfclassWeapon::STUNNING_HAMMER)
	{
		Range += 1.5f * 32;
	}
	return Range;
}

float CIcPlayerClass::GetGhoulPercent() const
{
	return 0;
}

EPlayerClass CIcPlayerClass::GetPlayerClass() const
{
	if(m_pPlayer)
		return m_pPlayer->GetClass();

	return EPlayerClass::None;
}

void CIcPlayerClass::OnPlayerClassChanged()
{
	UpdateSkin();
	IsZombie() ? SetNormalEmote(EMOTE_ANGRY) : SetNormalEmote(EMOTE_NORMAL); 

	// Enable hook protection by default for both infected and humans on class changed
	m_pPlayer->SetHookProtection(true);

	if(m_pCharacter)
	{
		GameServer()->CreatePlayerSpawn(GetPos(), GameController()->GetMaskForPlayerWorldEvent(GetCid()));
	}
}

void CIcPlayerClass::PrepareToDie(DeathContext *pContext)
{
}

void CIcPlayerClass::DisableHealing(float Duration, int From, EDamageType DamageType)
{
	m_HealingDisabledTicks = maximum<int>(m_HealingDisabledTicks, Duration * Server()->TickSpeed());
}

void CIcPlayerClass::OnPlayerSnap(int SnappingClient, int InfClassVersion)
{
}

bool CIcPlayerClass::IsHealingDisabled() const
{
	return m_HealingDisabledTicks > 0;
}

void CIcPlayerClass::OnCharacterPreCoreTick()
{
	if(m_pCharacter->IsPassenger())
	{
		if(m_pCharacter->m_Input.m_Jump && !m_pCharacter->m_PrevInput.m_Jump)
		{
			// Jump off is still in CCharacterCore::UpdateTaxiPassengers()
		}
		else
		{
			m_pCharacter->ResetMovementsInput();
			m_pCharacter->ResetHookInput();
		}
	}
}

void CIcPlayerClass::OnCharacterTick()
{
	if(m_HealingDisabledTicks > 0)
	{
		m_HealingDisabledTicks--;
	}

	BroadcastWeaponState();
}

void CIcPlayerClass::OnCharacterTickPaused()
{
}

void CIcPlayerClass::OnCharacterPostCoreTick()
{
}

void CIcPlayerClass::OnCharacterTickDeferred()
{
}

void CIcPlayerClass::OnCharacterSnap(int SnappingClient)
{
}

void CIcPlayerClass::OnCharacterSpawned(const SpawnContext &Context)
{
	m_ControlPointEffectAppliedTick = 0;
	m_HealingDisabledTicks = 0;

	UpdateSkin();
	GiveClassAttributes();
}

void CIcPlayerClass::OnCharacterDeath(EDamageType DamageType)
{
	if(m_pCharacter->HasPassenger())
	{
		m_pCharacter->SetPassenger(nullptr);
	}

	DestroyChildEntities();
}

void CIcPlayerClass::OnCharacterDamage(SDamageContext *pContext)
{
}

void CIcPlayerClass::OnCharacterBackFromDead()
{
}

void CIcPlayerClass::OnKilledCharacter(CIcCharacter *pVictim, const DeathContext &Context)
{
}

void CIcPlayerClass::OnHookAttachedPlayer()
{
}

void CIcPlayerClass::OnWeaponFired(WeaponFireContext *pFireContext)
{
	switch(pFireContext->Weapon)
	{
		case WEAPON_HAMMER:
			OnHammerFired(pFireContext);
			break;
		case WEAPON_GUN:
			OnGunFired(pFireContext);
			break;
		case WEAPON_SHOTGUN:
			OnShotgunFired(pFireContext);
			break;
		case WEAPON_GRENADE:
			OnGrenadeFired(pFireContext);
			break;
		case WEAPON_LASER:
			OnLaserFired(pFireContext);
			break;
		case WEAPON_NINJA:
			OnNinjaFired(pFireContext);
			break;
		default:
			break;
	}
}

void CIcPlayerClass::OnHammerFired(WeaponFireContext *pFireContext)
{
}

void CIcPlayerClass::OnGunFired(WeaponFireContext *pFireContext)
{
}

void CIcPlayerClass::OnShotgunFired(WeaponFireContext *pFireContext)
{
}

void CIcPlayerClass::OnGrenadeFired(WeaponFireContext *pFireContext)
{
}

void CIcPlayerClass::OnLaserFired(WeaponFireContext *pFireContext)
{
}

void CIcPlayerClass::OnNinjaFired(WeaponFireContext *pFireContext)
{
}

void CIcPlayerClass::OnFloatingPointCollected(int Points)
{
}

void CIcPlayerClass::GiveClassAttributes()
{
	if(!m_pCharacter)
	{
		return;
	}

	m_pCharacter->TakeAllWeapons();
	m_pCharacter->SetJumpsLimit(GetJumps());
}

void CIcPlayerClass::DestroyChildEntities()
{
}

void CIcPlayerClass::BroadcastWeaponState() const
{
}

void CIcPlayerClass::CreateHammerHit(const vec2 &ProjStartPos, const CIcCharacter *pTarget)
{
	const vec2 VecToTarget(pTarget->GetPos() - ProjStartPos);
	if(length(VecToTarget) > 0.0f)
		GameServer()->CreateHammerHit(pTarget->GetPos() - normalize(VecToTarget) * pTarget->GetProximityRadius() * 0.5f);
	else
		GameServer()->CreateHammerHit(ProjStartPos);
}

void CIcPlayerClass::UpdateSkin()
{
	if(!m_pPlayer)
		return;

	m_pPlayer->UpdateSkin();
}
