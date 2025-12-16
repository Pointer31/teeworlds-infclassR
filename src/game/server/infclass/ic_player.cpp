#include "ic_player.h"

#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/ic_gamecontroller.h>
#include <game/server/infclass/events-director.h>

#include "classes/humans/human.h"
#include "classes/ic_playerclass.h"
#include "classes/infected/infected.h"
#include "engine/server.h"
#include "entities/ic_character.h"

MACRO_ALLOC_POOL_ID_IMPL(CIcPlayer, MAX_CLIENTS)

CIcPlayer::CIcPlayer(CIcGameController *pGameController, int UniqueClientId, int ClientId, int Team)
	: CPlayer(pGameController->GameServer(), UniqueClientId, ClientId, Team)
	, m_pGameController(pGameController)
{
	m_class = EPlayerClass::Invalid;
	m_PreferredClass = EPlayerClass::Invalid;

	m_InfectionTick = -1;
	m_HookProtection = false;
	m_HookProtectionAutomatic = true;
	m_ShowOthers = SHOW_OTHERS_ONLY_TEAM;

	SetClass(EPlayerClass::None);
}

CIcPlayer::~CIcPlayer()
{
	SetCharacterClass(nullptr);
}

CIcGameController *CIcPlayer::GameController() const
{
	return m_pGameController;
}

void CIcPlayer::TryRespawn()
{
	SpawnContext Context;
	if(!GameController()->TryRespawn(this, &Context))
		return;

	m_Spawning = false;
	CIcCharacter *pCharacter = new(m_ClientId) CIcCharacter(GameController(), GameServer()->GetLastPlayerInput(m_ClientId));

	m_pCharacter = pCharacter;
	m_pCharacter->Spawn(this, Context.SpawnPos);
	OnCharacterSpawned(Context);

	GameServer()->CreatePlayerSpawn(Context.SpawnPos, GameController()->GetMaskForPlayerWorldEvent(m_ClientId));
}

int CIcPlayer::GetScore(int SnappingClient) const
{
	if(GetTeam() == TEAM_SPECTATORS)
	{
	}
	else
	{
		if(GameController()->GetRoundType() == ERoundType::Survival)
		{
			return m_Kills + m_Assists / 3;
		}
		if(GameController()->GetRoundType() == ERoundType::HideAndSeek)
		{
			if(IsHuman())
			{
				return m_Kills;
			}
			else
			{
				int Score = -m_Deaths;
				if (m_TeamChangeTick >= GameController()->GetInfectionStartTick() + 5)
				{
					Score -= 1;
				}
				return Score;
			}
		}

		return Server()->RoundStatistics()->PlayerScore(m_ClientId);
	}

	return CPlayer::GetScore(SnappingClient);
}

void CIcPlayer::Tick()
{
	if(!Server()->ClientIngame(m_ClientId))
		return;

	HandleInfection();

	CPlayer::Tick();

	if(!GameServer()->m_World.m_Paused)
	{
		if(IsHuman())
			m_HumanTime++;
	}

	if(m_MapMenu > 0)
		m_MapMenuTick++;

	if(GetClass() == EPlayerClass::Ghoul)
	{
		if(m_GhoulLevel > 0)
		{
			m_GhoulLevelTick--;

			if(m_GhoulLevelTick <= 0)
			{
				m_GhoulLevelTick = (Server()->TickSpeed() * GameServer()->Config()->m_InfGhoulDigestion);
				IncreaseGhoulLevel(-1);
				GetCharacterClass()->UpdateSkin();
			}
		}
	}

	HandleTuningParams();

	if(!GameServer()->m_World.m_Paused)
	{
		if(m_SpecialCameraTicks.has_value())
		{
			--m_SpecialCameraTicks.value();
			if(m_SpecialCameraTicks.value() <= 0)
			{
				ResetSpecialCamera();
			}
		}
	}

	if(SpecialCameraIsActive())
	{
		UpdateSpecialCamera();
	}
}

void CIcPlayer::PostTick()
{
	// update latency value
	if(m_PlayerFlags & PLAYERFLAG_SCOREBOARD)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				m_aCurLatency[i] = GameServer()->m_apPlayers[i]->m_Latency.m_Min;
		}
	}

	UpdateSpectatorPos();
}

void CIcPlayer::Snap(int SnappingClient)
{
	if(!Server()->ClientIngame(m_ClientId))
		return;

	CPlayer::Snap(SnappingClient);

	CNetObj_DDNetPlayer *pDDNetPlayer = Server()->SnapNewItem<CNetObj_DDNetPlayer>(m_ClientId);
	if(!pDDNetPlayer)
		return;

	pDDNetPlayer->m_AuthLevel = 0; // Server()->GetAuthedState(id);
	pDDNetPlayer->m_Flags = 0;
	if(m_Afk)
		pDDNetPlayer->m_Flags |= EXPLAYERFLAG_AFK;
	if(SpecialCameraIsActive())
		pDDNetPlayer->m_Flags |= EXPLAYERFLAG_SPEC;

	CIcCharacter *pCharacter = GetCharacter();
	if(pCharacter && pCharacter->IsSleeping())
	{
		pDDNetPlayer->m_Flags |= EXPLAYERFLAG_AFK;
	}

	int InfClassVersion = Server()->GetClientInfclassVersion(SnappingClient);

	if(InfClassVersion)
	{
		CNetObj_InfClassPlayer *pInfClassPlayer = Server()->SnapNewItem<CNetObj_InfClassPlayer>(m_ClientId);
		if(!pInfClassPlayer)
			return;

		pInfClassPlayer->m_Class = toNetValue(m_class);
		pInfClassPlayer->m_Flags = 0;
		if(IsInfected())
		{
			pInfClassPlayer->m_Flags |= INFCLASS_PLAYER_FLAG_INFECTED;
		}
		if(!HookProtectionEnabled())
		{
			pInfClassPlayer->m_Flags |= INFCLASS_PLAYER_FLAG_HOOK_PROTECTION_OFF;
		}
		// Note:
		// INFCLASS_PLAYER_FLAG_FORCED_TO_SPECTATE flag is deprecated because
		// EXPLAYERFLAG_SPEC does the same thing in a more compatible way

		pInfClassPlayer->m_Kills = m_Kills;
		pInfClassPlayer->m_Deaths = m_Deaths;
		pInfClassPlayer->m_Assists = m_Assists;
		pInfClassPlayer->m_Score = m_Score;

		GetCharacterClass()->OnPlayerSnap(SnappingClient, InfClassVersion);
	}

	if((SnappingClient == m_ClientId) && SpecialCameraIsActive())
	{
		CNetObj_SpectatorInfo *pSpectatorInfo = Server()->SnapNewItem<CNetObj_SpectatorInfo>(m_ClientId);
		if(!pSpectatorInfo)
			return;

		pSpectatorInfo->m_SpectatorId = GetSpecialCameraTargetCid();
		pSpectatorInfo->m_X = m_ViewPos.x;
		pSpectatorInfo->m_Y = m_ViewPos.y;
	}
}

void CIcPlayer::SnapClientInfo(int SnappingClient, int SnappingClientMappedId)
{
	CNetObj_ClientInfo *pClientInfo = Server()->SnapNewItem<CNetObj_ClientInfo>(SnappingClientMappedId);
	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, Server()->ClientName(m_ClientId));
	StrToInts(&pClientInfo->m_Clan0, 3, GetClan(SnappingClient));
	pClientInfo->m_Country = Server()->ClientCountry(m_ClientId);

	const CWeakSkinInfo SkinInfo = GetSkinInfo(SnappingClient);

	StrToInts(&pClientInfo->m_Skin0, 6, SkinInfo.pSkinName);
	pClientInfo->m_UseCustomColor = SkinInfo.UseCustomColor;
	pClientInfo->m_ColorBody = SkinInfo.ColorBody;
	pClientInfo->m_ColorFeet = SkinInfo.ColorFeet;
}

void CIcPlayer::HandleInfection()
{
	if(m_InfectionType == INFECTION_TYPE::NO)
	{
		return;
	}
	if(IsInfected() && (m_InfectionType == INFECTION_TYPE::REGULAR))
	{
		// Do not infect if inf class already set
		m_InfectionType = INFECTION_TYPE::NO;
		return;
	}

	const EPlayerClass PreviousClass = GetClass();
	CIcPlayer *pInfectiousPlayer = GameController()->GetPlayer(m_InfectiousPlayerCid);

	m_InfectionType = INFECTION_TYPE::NO;
	m_InfectiousPlayerCid = -1;

	GameController()->DoPlayerInfection(this, pInfectiousPlayer, PreviousClass);
}

void CIcPlayer::KillCharacter(int Weapon)
{
	if(!m_pCharacter)
		return;

	// Character actually died / removed from the world
	constexpr icArray<EPlayerClass, 2> EphemeralClasses = {
		EPlayerClass::Undead,
		EPlayerClass::Witch,
	};

	if((Weapon == WEAPON_SELF) && (IsHuman() || EphemeralClasses.Contains(GetClass())))
	{
		static const float SelfKillConfirmationTime = 3;
		if(Server()->Tick() > m_SelfKillAttemptTick + Server()->TickSpeed() * SelfKillConfirmationTime)
		{
			GameServer()->SendChatTarget_Localization(GetCid(), CHATCATEGORY_PLAYER,
				_("Self kill attempt prevented. Trigger self kill again to confirm."));
			m_SelfKillAttemptTick = Server()->Tick();
			// Reset last kill tick:
			m_LastKill = -1; // This could be done in the GameContext but let's keep it here to avoid conflicts
			return;
		}
	}

	CPlayer::KillCharacter(Weapon);

	if(!m_pCharacter && (Weapon != WEAPON_GAME))
	{
		for(int i = m_PreviousClasses.Size() - 1; i >= 0; i--)
		{
			if(EphemeralClasses.Contains(m_PreviousClasses.At(i)))
			{
				m_PreviousClasses.RemoveAt(i);
			}
		}
	}
}

int CIcPlayer::GetDefaultEmote() const
{
	if(m_pInfcPlayerClass)
		return m_pInfcPlayerClass->GetDefaultEmote();

	return CPlayer::GetDefaultEmote();
}

CWeakSkinInfo CIcPlayer::GetSkinInfo(int SnappingClient) const
{
	CWeakSkinInfo SkinInfo;
	if(m_SkinGetter)
	{
		IServer::CClientInfo ClientInfo = {nullptr};
		if(SnappingClient != SERVER_DEMO_CLIENT)
		{
			Server()->GetClientInfo(SnappingClient, &ClientInfo);
		}

		CIcPlayer *pSnappingClient = GameController()->GetPlayer(SnappingClient);

		bool SameTeam = pSnappingClient && (m_Team == pSnappingClient->m_Team) && (IsHuman() == pSnappingClient->IsHuman());

		const CSkinContext &SkinContext = SameTeam ? m_SameTeamSkinContext : m_DiffTeamSkinContext;
		m_SkinGetter(SkinContext, &SkinInfo, ClientInfo.m_DDNetVersion, ClientInfo.m_InfClassVersion);
		EventsDirector::SetupSkin(SkinContext, &SkinInfo, ClientInfo.m_DDNetVersion, ClientInfo.m_InfClassVersion);
	}
	else
	{
		SkinInfo.pSkinName = "default";
	}
	return SkinInfo;
}

bool CIcPlayer::GetAntiPingEnabled() const
{
	return m_AntiPing;
}

void CIcPlayer::SetAntiPingEnabled(bool Enabled)
{
	m_AntiPing = Enabled;
}

void CIcPlayer::SetInfectionTimestamp(int Timestamp)
{
	m_GameInfectionTimestamp = Timestamp;
}

int CIcPlayer::GetInfectionTimestamp() const
{
	return m_GameInfectionTimestamp;
}

void CIcPlayer::SetPreferredClass(EPlayerClass Class)
{
	m_PreferredClass = Class;
}

void CIcPlayer::SetPreviouslyPickedClass(EPlayerClass Class)
{
	m_PickedClass = Class;
}

CIcCharacter *CIcPlayer::GetCharacter()
{
	return CIcCharacter::GetInstance(m_pCharacter);
}

const CIcCharacter *CIcPlayer::GetCharacter() const
{
	return static_cast<const CIcCharacter*>(m_pCharacter);
}

void CIcPlayer::SetCharacterClass(CIcPlayerClass *pClass)
{
	if(m_pInfcPlayerClass)
	{
		m_pInfcPlayerClass->SetCharacter(nullptr);
		delete m_pInfcPlayerClass;
	}

	m_pInfcPlayerClass = pClass;
}

void CIcPlayer::SetClass(EPlayerClass NewClass)
{
	if(m_class == NewClass)
		return;

	if(m_class != EPlayerClass::Invalid)
	{
		if(m_PreviousClasses.Size() == m_PreviousClasses.Capacity())
		{
			m_PreviousClasses.RemoveAt(0);
		}

		m_PreviousClasses.Add(m_class);
	}

	m_GhoulLevel = 0;
	m_GhoulLevelTick = 0;

	// Also reset the last move tick to fix Hero flag indicator
	m_LastActionMoveTick = Server()->Tick();

	if(m_pInfcPlayerClass)
	{
		m_pInfcPlayerClass->SetCharacter(nullptr);
	}

	m_class = NewClass;

	const bool HadHumanClass = GetCharacterClass() && GetCharacterClass()->IsHuman();
	const bool HadInfectedClass = GetCharacterClass() && GetCharacterClass()->IsZombie();
	const bool SameTeam = IsHuman() ? HadHumanClass : HadInfectedClass;

	if(!SameTeam)
	{
		if(IsInfected())
		{
			SetCharacterClass(new(m_ClientId) CInfClassInfected(this));
			m_InfectionTick = Server()->Tick();
		}
		else
		{
			SetCharacterClass(new(m_ClientId) CInfClassHuman(this));
			m_InfectionTick = -1;
		}
	}

	// Skip the SetCharacter() routine if the World ResetRequested because it
	// means that the Character is going to be destroyed during this
	// IGameServer::Tick() which also invalidates possible auto class selection.
	if(!GameServer()->m_World.m_ResetRequested)
	{
		CIcCharacter *pCharacter = GetCharacter();
		m_pInfcPlayerClass->SetCharacter(pCharacter);
		if(pCharacter && !SameTeam)
		{
			// Changed team (was not an infected but is infected now or vice versa)
			pCharacter->ResetHelpers();
			pCharacter->SetPassenger(nullptr);
			CIcCharacter *pDriver = pCharacter->GetTaxi();
			if(pDriver)
			{
				pDriver->SetPassenger(nullptr);
			}
		}
	}
	m_pInfcPlayerClass->OnPlayerClassChanged();
	GameController()->OnPlayerClassChanged(this);

	SendClassIntro();
}

void CIcPlayer::UpdateSkin()
{
	if(m_pInfcPlayerClass)
	{
		m_pInfcPlayerClass->SetupSkinContext(&m_SameTeamSkinContext, true);
		m_pInfcPlayerClass->SetupSkinContext(&m_DiffTeamSkinContext, false);
		m_SkinGetter = m_pInfcPlayerClass->GetSkinGetter();
	}
	else
	{
		m_SkinGetter = nullptr;
	}

	const CWeakSkinInfo SkinInfo = GetSkinInfo(SERVER_DEMO_CLIENT);
	m_TeeInfos = CTeeInfo(SkinInfo.pSkinName, SkinInfo.UseCustomColor, SkinInfo.ColorBody, SkinInfo.ColorFeet);
	m_TeeInfos.ToSixup();
}

void CIcPlayer::StartInfection(int InfectiousPlayerCid, INFECTION_TYPE InfectionType)
{
	dbg_assert(InfectionType != INFECTION_TYPE::NO, "Invalid infection");

	if((InfectionType == INFECTION_TYPE::REGULAR) && IsInfected())
		return;

	m_InfectionType = InfectionType;
	m_InfectiousPlayerCid = InfectiousPlayerCid;
	m_InfectionCause = InfectiousPlayerCid >= 0 ? INFECTION_CAUSE::PLAYER : INFECTION_CAUSE::GAME;
}

bool CIcPlayer::IsInfectionStarted() const
{
	return m_InfectionType != INFECTION_TYPE::NO;
}

void CIcPlayer::OpenMapMenu(int Menu)
{
	m_MapMenu = Menu;
	m_MapMenuTick = 0;
}

void CIcPlayer::CloseMapMenu()
{
	m_MapMenu = 0;
	m_MapMenuTick = -1;
}

bool CIcPlayer::MapMenuClickable()
{
	return (m_MapMenu > 0 && (m_MapMenuTick > Server()->TickSpeed()/2));
}

void CIcPlayer::SetHookProtection(bool Value, bool Automatic)
{
	if(m_HookProtection != Value)
	{
		m_HookProtection = Value;

		if(!m_HookProtectionAutomatic || !Automatic)
		{
			if(m_HookProtection)
				GameServer()->SendChatTarget_Localization(GetCid(), CHATCATEGORY_DEFAULT, _("Hook protection enabled"), NULL);
			else
				GameServer()->SendChatTarget_Localization(GetCid(), CHATCATEGORY_DEFAULT, _("Hook protection disabled"), NULL);
		}
	}

	m_HookProtectionAutomatic = Automatic;
}

EPlayerScoreMode CIcPlayer::GetScoreMode() const
{
	return m_ScoreMode;
}

void CIcPlayer::SetScoreMode(EPlayerScoreMode Mode)
{
	m_ScoreMode = Mode;
}

void CIcPlayer::ResetSpecialCamera()
{
	m_SpecialCameraTicks.reset();
	m_FollowTargetId.reset();
}

int CIcPlayer::GetSpecialCameraTargetCid() const
{
	return m_FollowTargetId.value_or(SPEC_FOLLOW);
}

void CIcPlayer::SetSpecialCameraTargetCid(int ClientId, float Duration)
{
	if(Duration < 0 || ClientId < 0)
	{
		ResetSpecialCamera();
		return;
	}

	m_FollowTargetId = ClientId;
	m_SpecialCameraTicks = static_cast<int>(Duration * Server()->TickSpeed());

	UpdateSpecialCamera();
}

int CIcPlayer::GetSpectatingCid() const
{
	int TargetCid = GetSpecialCameraTargetCid();
	if (TargetCid < 0)
	{
		TargetCid = m_SpectatorId;
	}

	return TargetCid;
}

float CIcPlayer::GetGhoulPercent() const
{
	return clamp(m_GhoulLevel/static_cast<float>(GameController()->GetGhoulStomackSize()), 0.0f, 1.0f);
}

void CIcPlayer::IncreaseGhoulLevel(int Diff)
{
	int NewGhoulLevel = m_GhoulLevel + Diff;
	m_GhoulLevel = clamp(NewGhoulLevel, 0, GameController()->GetGhoulStomackSize());
}

void CIcPlayer::SetRandomClassChoosen()
{
	m_RandomClassRoundId = GameController()->GetRoundId();
}

bool CIcPlayer::RandomClassChoosen() const
{
	return m_RandomClassRoundId == GameController()->GetRoundId();
}

EPlayerClass CIcPlayer::GetPreviousInfectedClass() const
{
	for (int i = m_PreviousClasses.Size() - 1; i > 0; --i)
	{
		EPlayerClass Class = m_PreviousClasses.At(i);
		if(IsInfectedClass(Class))
		{
			return Class;
		}
	}

	return EPlayerClass::Invalid;
}

EPlayerClass CIcPlayer::GetPreviousHumanClass() const
{
	for(int i = m_PreviousClasses.Size() - 1; i > 0; --i)
	{
		EPlayerClass Class = m_PreviousClasses.At(i);
		if(IsHumanClass(Class))
		{
			return Class;
		}
	}

	return EPlayerClass::Invalid;
}

EPlayerClass CIcPlayer::GetPreviouslyPickedClass() const
{
	return m_PickedClass;
}

void CIcPlayer::AddSavedPosition(const vec2 Position)
{
	m_SavedPositions.Resize(1);
	m_SavedPositions[0] = Position;
}

bool CIcPlayer::LoadSavedPosition(vec2 *pOutput) const
{
	if(m_SavedPositions.IsEmpty())
		return false;

	*pOutput = m_SavedPositions.At(0);
	return true;
}

void CIcPlayer::ResetRoundData()
{
	SetClass(EPlayerClass::None);
	m_PreviousClasses.Clear();

	m_HumanTime = 0;

	m_Kills = 0;
	m_Deaths = 0;
	m_Assists = 0;
	m_Score = 0;
}

void CIcPlayer::OnKill()
{
	++m_Kills;
}

void CIcPlayer::OnDeath()
{
	++m_Deaths;
}

void CIcPlayer::OnAssist()
{
	++m_Assists;
}

void CIcPlayer::SetMaxHP(int MaxHP)
{
	m_MaxHP = MaxHP;
	if(m_pInfcPlayerClass)
	{
		m_pInfcPlayerClass->UpdateSkin();
	}
}

void CIcPlayer::ApplyMaxHP()
{
	if(!GetCharacter())
		return;

	if(!m_MaxHP)
		return;

	int HP = clamp<int>(m_MaxHP, 0, 10);
	int Armor = m_MaxHP - HP;

	GetCharacter()->SetMaxArmor(Armor);
	GetCharacter()->SetHealthArmor(HP, Armor);
}

void CIcPlayer::OnCharacterSpawned(const SpawnContext &Context)
{
	CIcCharacter *pCharacter = GetCharacter();

	m_pInfcPlayerClass->SetCharacter(pCharacter);
	pCharacter->OnCharacterSpawned(Context);

	ResetSpecialCamera();
	ApplyMaxHP();
}

const char *CIcPlayer::GetClan(int SnappingClient) const
{
	if(GetTeam() == TEAM_SPECTATORS)
	{
		return Server()->ClientClan(m_ClientId);
	}

	EPlayerScoreMode SnapScoreMode = GameController()->GetPlayerScoreMode(SnappingClient);
	static char aBuf[32];

	if(SnapScoreMode == EPlayerScoreMode::Class)
	{
		const char *ClassName = CIcGameController::GetClanForClass(GetClass(), "?????");
		str_format(aBuf, sizeof(aBuf), "%s%s", Server()->IsClientLogged(GetCid()) ? "@" : " ", ClassName);
	}
	else if(SnapScoreMode == EPlayerScoreMode::Time)
	{
		float RoundDuration = static_cast<float>(m_HumanTime / ((float)Server()->TickSpeed())) / 60.0f;
		int Minutes = static_cast<int>(RoundDuration);
		int Seconds = static_cast<int>((RoundDuration - Minutes) * 60.0f);

		str_format(aBuf, sizeof(aBuf), "%i:%s%i min", Minutes, ((Seconds < 10) ? "0" : ""), Seconds);
	}
	else if(SnapScoreMode == EPlayerScoreMode::Clan)
	{
		return Server()->ClientClan(m_ClientId);
	}

	// This is not thread-safe but we don't have threads.
	return aBuf;
}

void CIcPlayer::HandleAutoRespawn()
{
	float AutoSpawnInterval = 3;

	if(GameController()->GetRoundType() == ERoundType::Survival && IsInfected())
	{
		AutoSpawnInterval = 0;
	}

	if(!m_pCharacter && m_DieTick+Server()->TickSpeed() * AutoSpawnInterval <= Server()->Tick())
	{
		Respawn();
	}
}

void CIcPlayer::UpdateSpectatorPos()
{
	if(m_Team != TEAM_SPECTATORS || m_SpectatorId == SPEC_FREEVIEW)
		return;

	const CIcPlayer *pTarget = GameController()->GetPlayer(m_SpectatorId);
	if(!pTarget)
		return;

	if(g_Config.m_SvStrictSpectateMode)
	{
		const CIcCharacter *pCharacter = pTarget->GetCharacter();
		if(pCharacter && pCharacter->IsInvisible())
			return;
	}

	m_ViewPos = GameServer()->m_apPlayers[m_SpectatorId]->m_ViewPos;
}

bool CIcPlayer::SpecialCameraIsActive() const
{
	if(!g_Config.m_InfEnableFollowingCamera)
	{
		return false;
	}

	if (IsSpectator())
		return false;

	return m_FollowTargetId.has_value();
}

void CIcPlayer::UpdateSpecialCamera()
{
	if(m_FollowTargetId.has_value())
	{
		const CIcCharacter *pFollowedCharacter = GameController()->GetCharacter(m_FollowTargetId.value());
		if(pFollowedCharacter && pFollowedCharacter->IsAlive() && (!pFollowedCharacter->IsHuman() || (pFollowedCharacter->IsHuman() == IsHuman())))
		{
			m_ViewPos = pFollowedCharacter->GetPos();
		}
		else
		{
			ResetSpecialCamera();
		}
	}
}

void CIcPlayer::SendClassIntro()
{
	const EPlayerClass Class = GetClass();
	if(!IsBot() && (Class != EPlayerClass::None) && (Class != EPlayerClass::Invalid))
	{
		const char *pClassName = CIcGameController::GetClassDisplayName(Class);
		const char *pTranslated = Server()->Localization()->Localize(GetLanguage(), pClassName);

		if(IsHuman())
			GameServer()->SendBroadcast_Localization(GetCid(), EBroadcastPriority::GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE,
				_("You are a human: {str:ClassName}"), "ClassName", pTranslated, NULL);
		else
			GameServer()->SendBroadcast_Localization(GetCid(), EBroadcastPriority::GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE,
				_("You are an infected: {str:ClassName}"), "ClassName", pTranslated, NULL);

		int Index = static_cast<int>(Class);
		if(!m_aKnownClasses[Index])
		{
			const char *className = CIcGameController::GetClassName(Class);
			GameServer()->SendChatTarget_Localization(GetCid(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", className, NULL);
			m_aKnownClasses[Index] = true;
		}
	}
}
