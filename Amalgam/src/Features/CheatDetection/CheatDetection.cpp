#include "CheatDetection.h"

#include "../Players/PlayerUtils.h"
#include "../Output/Output.h"

bool CCheatDetection::ShouldScan()
{
	if (!Vars::CheatDetection::Methods.Value)
		return false;

	static int iStaticTickcount = I::GlobalVars->tickcount;
	const int iLastTickcount = iStaticTickcount;
	const int iCurrTickcount = iStaticTickcount = I::GlobalVars->tickcount;
	if (iCurrTickcount != iLastTickcount + 1)
		return false;

	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (pNetChan && (pNetChan->GetTimeSinceLastReceived() > TICK_INTERVAL * 2 || pNetChan->IsTimingOut()))
		return false;

	return true;
}

bool CCheatDetection::InvalidPitch(CTFPlayer* pEntity)
{
	return Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::InvalidPitch && fabsf(pEntity->m_angEyeAnglesX()) == 90.f;
}

bool CCheatDetection::IsChoking(CTFPlayer* pEntity)
{
	bool bReturn = mData[pEntity].m_PacketChoking.m_bInfract;
	mData[pEntity].m_PacketChoking.m_bInfract = false;

	return Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::PacketChoking && bReturn;
}

bool CCheatDetection::IsFlicking(CTFPlayer* pEntity)
{
	if (!(Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::AimFlicking))
	{
		mData[pEntity].m_AimFlicking = {};
		return false;
	}

	auto& tFlick = mData[pEntity].m_AimFlicking;
	if (tFlick.m_bInfract)
	{
		tFlick.m_bInfract = false;
		return true;
	}

	auto pWeapon = pEntity->m_hActiveWeapon()->As<CTFWeaponBase>();
	bool bFired = false;
	if (pWeapon)
	{
		const float flLastFire = pWeapon->m_flLastFireTime();
		if (flLastFire > 0.f && flLastFire != tFlick.m_flLastFireTime)
		{
			if (tFlick.m_flLastFireTime > 0.f)
				bFired = true;
			tFlick.m_flLastFireTime = flLastFire;
		}
	}

	tFlick.m_vAngles.emplace_front(pEntity->GetEyeAngles(), pEntity->m_flSimulationTime(), I::GlobalVars->tickcount, false, bFired, false);
	if (tFlick.m_vAngles.size() > 24)
		tFlick.m_vAngles.pop_back();

	if (tFlick.m_vAngles.size() < 3)
		return false;

	const auto& recAfter = tFlick.m_vAngles[0];
	const auto& recShot = tFlick.m_vAngles[1];
	const auto& recBefore = tFlick.m_vAngles[2];

	const Vec3 vBefore = recBefore.m_vAngle;
	const Vec3 vShot = recShot.m_vAngle;
	const Vec3 vAfter = recAfter.m_vAngle;

	const Vec3 vDeltaIn = vShot.DeltaAngle(vBefore);
	const Vec3 vDeltaOut = vAfter.DeltaAngle(vShot);
	const Vec3 vDeltaBase = vAfter.DeltaAngle(vBefore);

	const float flLenIn = sqrtf(vDeltaIn.x * vDeltaIn.x + vDeltaIn.y * vDeltaIn.y);
	const float flLenOut = sqrtf(vDeltaOut.x * vDeltaOut.x + vDeltaOut.y * vDeltaOut.y);
	if (flLenIn < 0.05f || flLenOut < 0.05f)
		return false;

	const float flDot = vDeltaIn.x * vDeltaOut.x + vDeltaIn.y * vDeltaOut.y;
	const float flCos = flDot / (flLenIn * flLenOut);
	if (flCos >= -0.65f)
		return false;

	const float flFovIn = Math::CalcFov(vBefore, vShot);
	const float flFovOut = Math::CalcFov(vShot, vAfter);
	const float flFovBase = Math::CalcFov(vBefore, vAfter);

	Vec3 vMid = vBefore + vDeltaBase * 0.5f;
	Math::ClampAngles(vMid);
	const float flExcursion = Math::CalcFov(vShot, vMid);

	const float flMinFov = std::min(flFovIn, flFovOut);
	const float flMaxFov = std::max(flFovIn, flFovOut);
	const float flRatio = flMinFov / (flMaxFov + 0.001f);

	if (flRatio < 0.35f || flExcursion < 0.15f)
		return false;

	const float flMaxNoise = Vars::CheatDetection::MaxNoise.Value * (TICK_INTERVAL / 0.015f);
	if (flExcursion >= Vars::CheatDetection::MinFlick.Value && flFovBase <= flMaxNoise)
		return true;

	const Vec3 vShootPos = pEntity->m_vecOrigin() + pEntity->GetViewOffset();
	bool bAimedAtEnemy = false;
	for (auto pTargetEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pTarget = pTargetEntity->As<CTFPlayer>();
		if (!pTarget || pTarget == pEntity || !pTarget->IsAlive() || pTarget->IsDormant() || pTarget->IsAGhost())
			continue;

		if (pTarget->m_iTeamNum() == pEntity->m_iTeamNum() && !SDK::FriendlyFire())
			continue;

		const Vec3 vTargetCenter = pTarget->GetCenter();
		const Vec3 vAngleToCenter = Math::CalcAngle(vShootPos, vTargetCenter);
		const float flTargetCenterFov = Math::CalcFov(vShot, vAngleToCenter);

		const Vec3 vTargetEye = pTarget->GetEyePosition();
		const Vec3 vAngleToEye = Math::CalcAngle(vShootPos, vTargetEye);
		const float flTargetEyeFov = Math::CalcFov(vShot, vAngleToEye);

		const float flTargetMinFov = std::min(flTargetCenterFov, flTargetEyeFov);
		if (flTargetMinFov <= 4.5f)
		{
			const float flBeforeCenterFov = Math::CalcFov(vBefore, vAngleToCenter);
			const float flBeforeEyeFov = Math::CalcFov(vBefore, vAngleToEye);
			const float flBeforeFov = std::min(flBeforeCenterFov, flBeforeEyeFov);

			const float flAfterCenterFov = Math::CalcFov(vAfter, vAngleToCenter);
			const float flAfterEyeFov = Math::CalcFov(vAfter, vAngleToEye);
			const float flAfterFov = std::min(flAfterCenterFov, flAfterEyeFov);

			if (flTargetMinFov < flBeforeFov && flTargetMinFov < flAfterFov)
			{
				bAimedAtEnemy = true;
				break;
			}
		}
	}

	const bool bAction = recShot.m_bFired || recShot.m_bAttacking || recShot.m_bDamage;
	if (bAimedAtEnemy)
	{
		if (bAction)
			return true;

		if (flExcursion >= 0.35f && flFovBase <= flMinFov * 0.5f)
			return true;
	}
	else if (bAction && flExcursion >= 0.5f && flFovBase <= flMinFov * 0.4f)
		return true;

	return false;
}

bool CCheatDetection::IsDuckSpeed(CTFPlayer* pEntity)
{
	if (!(Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::DuckSpeed)
		|| !pEntity->IsDucking() || !pEntity->IsOnGround()
		|| pEntity->m_vecVelocity().Length2D() < pEntity->m_flMaxspeed() * 0.5f)
	{
		mData[pEntity].m_DuckSpeed.m_iStartTick = 0;
		return false;
	}

	if (!mData[pEntity].m_DuckSpeed.m_iStartTick)
		mData[pEntity].m_DuckSpeed.m_iStartTick = I::GlobalVars->tickcount;

	if (I::GlobalVars->tickcount - mData[pEntity].m_DuckSpeed.m_iStartTick > TIME_TO_TICKS(1))
	{
		mData[pEntity].m_DuckSpeed.m_iStartTick = 0;
		return true;
	}

	return false;
}

bool CCheatDetection::IsLagCompAbusing(CTFPlayer* pEntity, int iDeltaTicks)
{
	auto& tLagComp = mData[pEntity].m_PacketChoking.m_LagComp;
	if (!(Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::LagCompAbuse))
	{
		tLagComp = {};
		return false;
	}

	const int iMinDelta = std::max(2, Vars::CheatDetection::LagCompMinimumDelta.Value);
	const int iWindowTicks = std::max(1, TIME_TO_TICKS(std::max(0.1f, Vars::CheatDetection::LagCompWindow.Value)));
	const int iRequiredBursts = std::max(1, Vars::CheatDetection::LagCompBurstCount.Value);

	if (iDeltaTicks <= iMinDelta)
		return false;

	tLagComp.m_vBurstTicks.emplace_back(I::GlobalVars->tickcount);
	tLagComp.m_vDeltaCmds.emplace_back(iDeltaTicks);

	while (!tLagComp.m_vBurstTicks.empty() && I::GlobalVars->tickcount - tLagComp.m_vBurstTicks.front() > iWindowTicks)
	{
		tLagComp.m_vBurstTicks.pop_front();
		tLagComp.m_vDeltaCmds.pop_front();
	}

	if ((int)tLagComp.m_vBurstTicks.size() >= iRequiredBursts)
	{
		tLagComp.m_vBurstTicks.clear();
		tLagComp.m_vDeltaCmds.clear();
		tLagComp.m_bInfract = true;
	}

	bool bReturn = tLagComp.m_bInfract;
	tLagComp.m_bInfract = false;
	return bReturn;
}

bool CCheatDetection::IsCritManipulating(CTFPlayer* pEntity)
{
	auto& tCritTracker = mData[pEntity].m_CritTracker;
	if (!(Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::CritManipulation))
	{
		tCritTracker = {};
		return false;
	}

	bool bReturn = tCritTracker.m_bInfract;
	tCritTracker.m_bInfract = false;
	return bReturn;
}

void CCheatDetection::TrackCritEvent(CTFPlayer* pEntity, CTFWeaponBase* pWeapon, bool bCrit)
{
	if (!(Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::CritManipulation) || !pWeapon)
		return;

	auto& tCritTracker = mData[pEntity].m_CritTracker;

	if (pEntity->IsCritBoosted())
	{
		tCritTracker.m_mWeaponHistory.erase(pWeapon->GetWeaponID());
		return;
	}

	auto& tHistory = tCritTracker.m_mWeaponHistory[pWeapon->GetWeaponID()];
	tHistory.m_vHistory.emplace_back(bCrit);
	if (bCrit)
		tHistory.m_iCrits++;

	const int iWindow = std::max(1, Vars::CheatDetection::CritWindow.Value);
	while ((int)tHistory.m_vHistory.size() > iWindow)
	{
		if (tHistory.m_vHistory.front())
			tHistory.m_iCrits--;
		tHistory.m_vHistory.pop_front();
	}

	if ((int)tHistory.m_vHistory.size() < iWindow)
		return;

	const float flCritRate = (float(tHistory.m_iCrits) / float(tHistory.m_vHistory.size())) * 100.f;
	if (flCritRate >= Vars::CheatDetection::CritThreshold.Value)
	{
		tHistory.m_vHistory.clear();
		tHistory.m_iCrits = 0;
		tCritTracker.m_bInfract = true;
	}
}

void CCheatDetection::Infract(CTFPlayer* pEntity, const char* sReason)
{
	bool bMark = false;
	if (Vars::CheatDetection::DetectionsRequired.Value)
	{
		mData[pEntity].m_iDetections++;
		bMark = mData[pEntity].m_iDetections >= Vars::CheatDetection::DetectionsRequired.Value;
	}

	F::Output.CheatDetection(mData[pEntity].m_sName.c_str(), bMark ? "marked" : "infracted", sReason);
	if (bMark)
	{
		const int iDetections = std::max(mData[pEntity].m_iDetections, Vars::CheatDetection::DetectionsRequired.Value);
		mData[pEntity].m_iDetections = 0;
		F::PlayerUtils.AddTag(
			mData[pEntity].m_uAccountID,
			F::PlayerUtils.TagToIndex(CHEATER_TAG),
			true,
			mData[pEntity].m_sName.c_str(),
			sReason,
			iDetections,
			true);
	}
}

void CCheatDetection::Run()
{
	if (!ShouldScan() || !I::EngineClient->IsConnected() || I::EngineClient->IsPlayingDemo())
		return;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	for (auto& pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		int iIndex = pPlayer->entindex();
		float flDeltaTime = H::Entities.GetDeltaTime(iIndex);
		if (!flDeltaTime)
			continue;

		const int iDeltaTicks = TIME_TO_TICKS(flDeltaTime);

		if (iIndex == I::EngineClient->GetLocalPlayer() || !pPlayer->IsAlive() || pPlayer->IsAGhost()
			|| pResource->IsFakePlayer(iIndex) || F::PlayerUtils.HasTag(iIndex, F::PlayerUtils.TagToIndex(CHEATER_TAG)))
		{
			mData[pPlayer].m_PacketChoking = {};
			mData[pPlayer].m_AimFlicking = {};
			mData[pPlayer].m_DuckSpeed = {};
			mData[pPlayer].m_CritTracker = {};
			continue;
		}

		mData[pPlayer].m_uAccountID = pResource->m_iAccountID(iIndex);
		mData[pPlayer].m_sName = F::PlayerUtils.GetPlayerName(iIndex, pResource->GetName(iIndex));

		if (InvalidPitch(pPlayer))
			Infract(pPlayer, "invalid pitch");
		if (IsChoking(pPlayer))
			Infract(pPlayer, "choking packets");
		if (IsFlicking(pPlayer))
			Infract(pPlayer, "silent aim");
		if (IsDuckSpeed(pPlayer))
			Infract(pPlayer, "duck speed");
		if (IsLagCompAbusing(pPlayer, iDeltaTicks))
			Infract(pPlayer, "lag-comp abuse");
		if (IsCritManipulating(pPlayer))
			Infract(pPlayer, "crit manipulation");
	}
}

void CCheatDetection::Reset()
{
	mData.clear();
}

void CCheatDetection::ReportChoke(CTFPlayer* pEntity, int iChoke)
{
	if (Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::PacketChoking)
	{
		mData[pEntity].m_PacketChoking.m_vChokes.push_back(iChoke);
		if (mData[pEntity].m_PacketChoking.m_vChokes.size() == 3)
		{
			mData[pEntity].m_PacketChoking.m_bInfract = true;
			for (auto& iChoke : mData[pEntity].m_PacketChoking.m_vChokes)
			{
				if (iChoke < Vars::CheatDetection::MinChoking.Value)
					mData[pEntity].m_PacketChoking.m_bInfract = false;
			}
			mData[pEntity].m_PacketChoking.m_vChokes.clear();
		}
	}
	else
		mData[pEntity].m_PacketChoking.m_vChokes.clear();
}

void CCheatDetection::ReportDamage(IGameEvent* pEvent)
{
	const bool bAimFlicking = Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::AimFlicking;
	const bool bCritTracking = Vars::CheatDetection::Methods.Value & Vars::CheatDetection::MethodsEnum::CritManipulation;
	if (!bAimFlicking && !bCritTracking)
		return;

	const int iAttacker = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker"));
	if (iAttacker == I::EngineClient->GetLocalPlayer())
		return;

	auto pAttacker = I::ClientEntityList->GetClientEntity(iAttacker)->As<CTFPlayer>();
	if (!pAttacker || !pAttacker->IsPlayer() || pAttacker->IsDormant())
		return;

	auto pWeapon = pAttacker->m_hActiveWeapon()->As<CTFWeaponBase>();
	switch (SDK::GetWeaponType(pWeapon))
	{
	case EWeaponType::UNKNOWN:
	case EWeaponType::PROJECTILE:
		return;
	}

	if (bAimFlicking)
	{
		auto& tFlick = mData[pAttacker].m_AimFlicking;
		const int iVictim = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));
		auto pVictim = I::ClientEntityList->GetClientEntity(iVictim)->As<CTFPlayer>();

		const size_t nAngles = tFlick.m_vAngles.size();
		if (nAngles >= 3)
		{
			const size_t nMaxCheck = std::min(nAngles - 1, size_t(10));
			for (size_t i = 1; i < nMaxCheck; i++)
			{
				auto& recAfter = tFlick.m_vAngles[i - 1];
				auto& recShot = tFlick.m_vAngles[i];
				auto& recBefore = tFlick.m_vAngles[i + 1];

				const Vec3 vBefore = recBefore.m_vAngle;
				const Vec3 vShot = recShot.m_vAngle;
				const Vec3 vAfter = recAfter.m_vAngle;

				const Vec3 vDeltaIn = vShot.DeltaAngle(vBefore);
				const Vec3 vDeltaOut = vAfter.DeltaAngle(vShot);
				const float flLenIn = sqrtf(vDeltaIn.x * vDeltaIn.x + vDeltaIn.y * vDeltaIn.y);
				const float flLenOut = sqrtf(vDeltaOut.x * vDeltaOut.x + vDeltaOut.y * vDeltaOut.y);
				if (flLenIn < 0.05f || flLenOut < 0.05f)
					continue;

				const float flDot = vDeltaIn.x * vDeltaOut.x + vDeltaIn.y * vDeltaOut.y;
				const float flCos = flDot / (flLenIn * flLenOut);
				if (flCos >= -0.65f)
					continue;

				const Vec3 vDeltaBase = vAfter.DeltaAngle(vBefore);
				Vec3 vMid = vBefore + vDeltaBase * 0.5f;
				Math::ClampAngles(vMid);
				const float flExcursion = Math::CalcFov(vShot, vMid);

				if (flExcursion >= 0.15f)
				{
					if (pVictim)
					{
						const Vec3 vShootPos = pAttacker->m_vecOrigin() + pAttacker->GetViewOffset();
						const float flFovToVictimCenter = Math::CalcFov(vShot, Math::CalcAngle(vShootPos, pVictim->GetCenter()));
						const float flFovToVictimEye = Math::CalcFov(vShot, Math::CalcAngle(vShootPos, pVictim->GetEyePosition()));
						const float flVictimFov = std::min(flFovToVictimCenter, flFovToVictimEye);

						const float flBeforeFov = std::min(
							Math::CalcFov(vBefore, Math::CalcAngle(vShootPos, pVictim->GetCenter())),
							Math::CalcFov(vBefore, Math::CalcAngle(vShootPos, pVictim->GetEyePosition()))
						);

						if (flVictimFov <= 6.0f && flVictimFov < flBeforeFov)
						{
							recShot.m_bDamage = true;
							tFlick.m_bInfract = true;
							break;
						}
					}

					recShot.m_bDamage = true;
					tFlick.m_bInfract = true;
					break;
				}
			}
		}

		if (!tFlick.m_vAngles.empty())
			tFlick.m_vAngles.front().m_bAttacking = true;
	}

	if (bCritTracking)
		TrackCritEvent(pAttacker, pWeapon, pEvent->GetBool("crit"));
}
