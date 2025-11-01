#pragma once

#include "ic_entity.h"

class CPlacedObject : public CIcEntity
{
public:
	CPlacedObject(CGameContext *pGameContext, int ObjectType, vec2 Pos = vec2(), int Owner = -1, int ProximityRadius=0);
	~CPlacedObject() override;

	void MoveTo(const vec2 &Position) override;

	bool HasSecondPosition() const { return m_Pos2.has_value(); }
	vec2 SecondPosition() const { return m_Pos2.value_or(m_Pos); }
	void SetSecondPosition(vec2 Position);
	void ResetSecondPosition() { m_Pos2 = {};};

	float MaxLength() const { return m_MaxLength.value_or(0); }
	void SetMaxLength(float Length);

	void Tick() override;

protected:
	bool DoSnapForClient(int SnappingClient) override;

	CNetObj_InfClassObject *SnapInfClassObject();

protected:
	std::optional<vec2> m_Pos2;
	std::optional<float> m_MaxLength;
	int m_InfClassObjectId = -1;
	int m_InfClassObjectType = -1;
	int m_InfClassObjectFlags = 0;
};
