/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_CHAIN_LASER_H
#define GAME_SERVER_CHAIN_LASER_H

#include "ic_entity.h"

struct WeaponFireContext;
enum class EDamageType;
enum class EInfclassWeapon;

class CChainLaser : public CIcEntity
{
public:
	CChainLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg, EInfclassWeapon InfClassWeapon);

	static CChainLaser *MakeLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg, EInfclassWeapon InfClassWeapon);

	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;

	virtual void DoBounce();

	void SetExplosive(bool Explosive);
	void SetPiercing(bool Piercing);
	void SetSnapType(int LaserType);

protected:
	EDamageType GetDamageType() const;

	virtual bool HitTarget(vec2 From, vec2 To);
	virtual bool OnCharacterHit(CIcCharacter *pHit, const vec2 &At);
	void DoReflect(const vec2 &To);

protected:
	vec2 m_From;
	vec2 m_Dir;
	vec2 m_ToZombies[5];
	int m_ToZombiesSnaps[5];
	int m_ZombiesHit;
	const EInfclassWeapon m_Weapon;
	int m_SnapLaserType = -1;
	float m_Energy;
	int m_Bounces = 0;
	int m_MaxBounces = 0;
	int m_BounceCost = 0;
	int m_EvalTick = 0;
	int m_Dmg = 0;
	bool m_Explosive = false;
	bool m_Piercing = false;
	std::optional<int> m_IgnoreTarget{};
};

#endif // GAME_SERVER_INFCLASS_ENTITIES_LASER_H
