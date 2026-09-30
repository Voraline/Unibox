#pragma once
#include "../../SDK/SDK.h"

struct AngleHistory_t
{
	Vec3 m_vAngle = {};
	float m_flSimTime = 0.f;
	int m_iTick = 0;
	bool m_bAttacking = false;
	bool m_bFired = false;
	bool m_bDamage = false;
};

struct PlayerInfo
{
	uint32_t m_uAccountID = 0;
	std::string m_sName = "";

	int m_iDetections = 0;

	struct PacketChoking_t
	{
		std::deque<int> m_vChokes = {};
		bool m_bInfract = false;

		struct LagCompAbuse_t
		{
			std::deque<int> m_vBurstTicks = {};
			std::deque<int> m_vDeltaCmds = {};
			bool m_bInfract = false;
		} m_LagComp;
	} m_PacketChoking;

	struct AimFlicking_t
	{
		std::deque<AngleHistory_t> m_vAngles = {};
		float m_flLastFireTime = 0.f;
		int m_iLastClip = -1;
		int m_iLastShots = -1;
		Vec3 m_vLastOrigin = {};
		bool m_bInfract = false;
	} m_AimFlicking;

	struct DuckSpeed_t
	{
		int m_iStartTick = 0;
	} m_DuckSpeed;

	struct CritTracker_t
	{
		struct WeaponHistory_t
		{
			std::deque<bool> m_vHistory = {};
			int m_iCrits = 0;
		};

		std::unordered_map<int, WeaponHistory_t> m_mWeaponHistory = {};
		bool m_bInfract = false;
	} m_CritTracker;
};

class CCheatDetection
{
private:
	bool ShouldScan();

	bool InvalidPitch(CTFPlayer* pEntity);
	bool IsChoking(CTFPlayer* pEntity);
	bool IsFlicking(CTFPlayer* pEntity);
	bool IsDuckSpeed(CTFPlayer* pEntity);
	bool IsLagCompAbusing(CTFPlayer* pEntity, int iDeltaTicks);
	bool IsCritManipulating(CTFPlayer* pEntity);
	void TrackCritEvent(CTFPlayer* pEntity, CTFWeaponBase* pWeapon, bool bCrit);

	void Infract(CTFPlayer* pEntity, const char* sReason);

	std::unordered_map<CTFPlayer*, PlayerInfo> mData = {};

public:
	void Run();

	void ReportChoke(CTFPlayer* pEntity, int iChoke);
	void ReportDamage(IGameEvent* pEvent);
	void Reset();
};

ADD_FEATURE(CCheatDetection, CheatDetection);
