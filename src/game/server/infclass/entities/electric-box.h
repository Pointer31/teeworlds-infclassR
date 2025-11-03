/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_ELECTRIC_BOX_H
#define GAME_SERVER_ENTITIES_ELECTRIC_BOX_H

#include <game/server/infclass/entities/ic_placed_object.h>

class CIcCharacter;

class CElectricBox : public CPlacedObject
{
public:
	static int EntityId;

	CElectricBox(CGameContext *pGameContext, vec2 Pos, int Owner);
	~CElectricBox() override;

	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;
	void OnHitInfected(CIcCharacter *pCharacter);
	int m_Lives{};
	
private:
	void PrepareSnapData();

	int m_EndPointId{};
	int m_EndPointId2{};
	int m_EndPointId3{};
	int m_EndPointId4{};
	int m_EndPointId5{};
	int m_EndPointId6{};
	int m_EndPointId7{};
	int m_WallFlashTicks{};
	int m_SnapStartTick{};
};

#endif
