#include "CheatDetection.h"

#include "../Players/PlayerUtils.h"
#include "../Output/Output.h"
#include "../Backtrack/Backtrack.h"

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
	if (pNetChan && (pNetChan->GetTimeSinceLastReceived() > 0.25f || pNetChan->IsTimingOut()))
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

	if (pEntity->IsDormant())
		return false;

	auto& tFlick = mData[pEntity].m_AimFlicking;
	if (tFlick.m_bInfract)
	{
		tFlick.m_bInfract = false;
		return true;
	}

	const Vec3 vCurrentOrigin = pEntity->m_vecOrigin();
	if (!tFlick.m_vLastOrigin.IsZero() && vCurrentOrigin.DistToSqr(tFlick.m_vLastOrigin) > 62500.f)
	{
		tFlick.m_vAngles.clear();
		tFlick.m_vLastOrigin = vCurrentOrigin;
		return false;
	}
	tFlick.m_vLastOrigin = vCurrentOrigin;

	auto pWeapon = pEntity->m_hActiveWeapon()->As<CTFWeaponBase>();
	bool bFired = false;
	if (pWeapon)
	{
		const int iWeaponID = pWeapon->GetWeaponID();
		if (iWeaponID != tFlick.m_iLastWeaponID)
		{
			tFlick.m_iLastWeaponID = iWeaponID;
			tFlick.m_flLastFireTime = pWeapon->m_flLastFireTime();
			tFlick.m_iLastClip = pWeapon->m_iClip1();
			tFlick.m_iLastShots = pWeapon->m_iConsecutiveShots();
		}
		else
		{
			const float flLastFire = pWeapon->m_flLastFireTime();
			if (flLastFire > 0.f && flLastFire != tFlick.m_flLastFireTime)
			{
				bFired = true;
				tFlick.m_flLastFireTime = flLastFire;
			}

			const int iClip = pWeapon->m_iClip1();
			if (tFlick.m_iLastClip >= 0 && iClip >= 0 && iClip < tFlick.m_iLastClip)
				bFired = true;
			tFlick.m_iLastClip = iClip;

			const int iShots = pWeapon->m_iConsecutiveShots();
			if (tFlick.m_iLastShots >= 0 && iShots > tFlick.m_iLastShots)
				bFired = true;
			tFlick.m_iLastShots = iShots;
		}
	}

	const bool bLocal = pEntity->entindex() == I::EngineClient->GetLocalPlayer();
	if (bLocal)
	{
		if (G::Attacking == 1 || (G::CurrentUserCmd && (G::CurrentUserCmd->buttons & IN_ATTACK)))
			bFired = true;
	}

	const bool bAttacking = (pEntity->m_nButtons() & IN_ATTACK) || (bLocal && G::CurrentUserCmd && (G::CurrentUserCmd->buttons & IN_ATTACK));
	const Vec3 vCurrentAngle = (bLocal && G::CurrentUserCmd) ? G::CurrentUserCmd->viewangles : pEntity->GetEyeAngles();
	const Vec3 vCurrentShootPos = pEntity->m_vecOrigin() + pEntity->GetViewOffset();
	const float flCurrentSimTime = bLocal ? TICKS_TO_TIME(I::GlobalVars->tickcount) : pEntity->m_flSimulationTime();

	tFlick.m_vAngles.emplace_front(vCurrentAngle, vCurrentShootPos, flCurrentSimTime, I::GlobalVars->tickcount, bAttacking, bFired, false);
	if (tFlick.m_vAngles.size() > 32)
		tFlick.m_vAngles.pop_back();

	if (tFlick.m_vAngles.size() < 3)
		return false;

	const auto& recAfter = tFlick.m_vAngles[0];
	const auto& recShot = tFlick.m_vAngles[1];
	const auto& recBefore = (tFlick.m_vAngles.size() >= 4 && Math::CalcFov(recShot.m_vAngle, tFlick.m_vAngles[2].m_vAngle) < 1.0f)
		? tFlick.m_vAngles[3]
		: tFlick.m_vAngles[2];

	const float flSimDelta1 = recShot.m_flSimTime - recBefore.m_flSimTime;
	const float flSimDelta2 = recAfter.m_flSimTime - recShot.m_flSimTime;
	if (flSimDelta1 <= 0.f || flSimDelta2 <= 0.f || flSimDelta1 > 1.0f || flSimDelta2 > 1.0f)
		return false;

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

	const float flFovIn = Math::CalcFov(vBefore, vShot);
	const float flFovOut = Math::CalcFov(vShot, vAfter);
	const float flFovBase = Math::CalcFov(vBefore, vAfter);

	Vec3 vMid = vBefore + vDeltaBase * 0.5f;
	Math::ClampAngles(vMid);
	const float flExcursion = Math::CalcFov(vShot, vMid);

	const float flMinFov = std::min(flFovIn, flFovOut);
	const float flMaxFov = std::max(flFovIn, flFovOut);
	const float flRatio = flMinFov / (flMaxFov + 0.001f);

	const bool bReversal = flCos < -0.60f && flRatio >= 0.30f;
	if (!bReversal && flExcursion < 0.20f)
		return false;

	if (flExcursion < 0.12f)
		return false;

	const float flMaxNoise = Vars::CheatDetection::MaxNoise.Value * (TICK_INTERVAL / 0.015f);
	if (bReversal && flExcursion >= Vars::CheatDetection::MinFlick.Value && flFovBase <= flMaxNoise)
		return true;

	const Vec3 vShootPos = recShot.m_vShootPos.IsZero() ? (pEntity->m_vecOrigin() + pEntity->GetViewOffset()) : recShot.m_vShootPos;
	bool bAimedAtTarget = false;

	for (auto pTargetEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pTarget = pTargetEntity->As<CTFPlayer>();
		if (!pTarget || pTarget == pEntity || !pTarget->IsAlive() || pTarget->IsDormant() || pTarget->IsAGhost())
			continue;

		if (pTarget->m_iTeamNum() == pEntity->m_iTeamNum() && !SDK::FriendlyFire())
			continue;

		float flTargetMinFov = 999.f;
		float flBeforeMinFov = 999.f;
		float flAfterMinFov = 999.f;

		matrix3x4* aBones = F::Backtrack.GetBones(pTarget);
		if (aBones)
		{
			const int nHitboxes = std::min(pTarget->GetNumOfHitboxes(), 18);
			for (int n = 0; n < nHitboxes; n++)
			{
				const Vec3 vHitbox = pTarget->GetHitboxCenter(aBones, n);
				const Vec3 vAngleToHitbox = Math::CalcAngle(vShootPos, vHitbox);
				const float flFov = Math::CalcFov(vShot, vAngleToHitbox);
				if (flFov < flTargetMinFov)
				{
					flTargetMinFov = flFov;
					flBeforeMinFov = Math::CalcFov(vBefore, vAngleToHitbox);
					flAfterMinFov = Math::CalcFov(vAfter, vAngleToHitbox);
				}
			}
		}

		std::vector<TickRecord*> vTargetRecords = {};
		if (F::Backtrack.GetRecords(pTarget, vTargetRecords))
		{
			for (auto pRecord : vTargetRecords)
			{
				if (!pRecord || pRecord->m_bInvalid)
					continue;

				if (pTarget->GetNumOfHitboxes() > 0)
				{
					const Vec3 vRecordHead = pTarget->GetHitboxCenter(pRecord->m_aBones, 0);
					const Vec3 vAngleToHead = Math::CalcAngle(vShootPos, vRecordHead);
					const float flHeadFov = Math::CalcFov(vShot, vAngleToHead);
					if (flHeadFov < flTargetMinFov)
					{
						flTargetMinFov = flHeadFov;
						flBeforeMinFov = Math::CalcFov(vBefore, vAngleToHead);
						flAfterMinFov = Math::CalcFov(vAfter, vAngleToHead);
					}
				}

				const Vec3 vRecordCenter = pRecord->m_vOrigin + pTarget->GetViewOffset() * 0.5f;
				const Vec3 vAngleToRecord = Math::CalcAngle(vShootPos, vRecordCenter);
				const float flRecordFov = Math::CalcFov(vShot, vAngleToRecord);
				if (flRecordFov < flTargetMinFov)
				{
					flTargetMinFov = flRecordFov;
					flBeforeMinFov = Math::CalcFov(vBefore, vAngleToRecord);
					flAfterMinFov = Math::CalcFov(vAfter, vAngleToRecord);
				}
			}
		}

		const Vec3 vTargetEye = pTarget->GetEyePosition();
		const Vec3 vAngleToEye = Math::CalcAngle(vShootPos, vTargetEye);
		const float flEyeFov = Math::CalcFov(vShot, vAngleToEye);
		if (flEyeFov < flTargetMinFov)
		{
			flTargetMinFov = flEyeFov;
			flBeforeMinFov = Math::CalcFov(vBefore, vAngleToEye);
			flAfterMinFov = Math::CalcFov(vAfter, vAngleToEye);
		}

		const Vec3 vTargetCenter = pTarget->GetCenter();
		const Vec3 vAngleToCenter = Math::CalcAngle(vShootPos, vTargetCenter);
		const float flCenterFov = Math::CalcFov(vShot, vAngleToCenter);
		if (flCenterFov < flTargetMinFov)
		{
			flTargetMinFov = flCenterFov;
			flBeforeMinFov = Math::CalcFov(vBefore, vAngleToCenter);
			flAfterMinFov = Math::CalcFov(vAfter, vAngleToCenter);
		}

		const float flDistance = vShootPos.DistTo(vTargetCenter);
		const float flMaxHitboxAngle = std::clamp(Math::Rad2Deg(atan2f(28.f, std::max(flDistance, 32.f))), 0.5f, 6.0f);

		if (flTargetMinFov <= flMaxHitboxAngle && flTargetMinFov < flBeforeMinFov && flTargetMinFov < flAfterMinFov)
		{
			bAimedAtTarget = true;
			break;
		}
	}

	const bool bAction = recShot.m_bFired || recShot.m_bAttacking || recShot.m_bDamage;
	if (bAimedAtTarget)
	{
		if (bAction)
			return true;

		if (bReversal && flExcursion >= 0.25f && flFovBase <= flMinFov * 0.45f)
			return true;
	}
	else if (bAction && bReversal && flExcursion >= 0.40f && flFovBase <= flMinFov * 0.35f)
	{
		return true;
	}

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
		if (iIndex == I::EngineClient->GetLocalPlayer())
		{
			if (!Vars::CheatDetection::DetectLocal.Value)
				continue;
			flDeltaTime = TICK_INTERVAL;
		}
		else if (!flDeltaTime)
			continue;

		const int iDeltaTicks = TIME_TO_TICKS(flDeltaTime);

		if (!pPlayer->IsAlive() || pPlayer->IsAGhost()
			|| pResource->IsFakePlayer(iIndex) || (iIndex != I::EngineClient->GetLocalPlayer() && F::PlayerUtils.HasTag(iIndex, F::PlayerUtils.TagToIndex(CHEATER_TAG))))
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
	if (!Vars::CheatDetection::DetectLocal.Value && iAttacker == I::EngineClient->GetLocalPlayer())
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
			const size_t nMaxCheck = std::min(nAngles - 1, size_t(16));
			for (size_t i = 1; i < nMaxCheck; i++)
			{
				auto& recAfter = tFlick.m_vAngles[i - 1];
				auto& recShot = tFlick.m_vAngles[i];

				size_t nBeforeIndex = i + 1;
				if (nBeforeIndex + 1 < nAngles && Math::CalcFov(recShot.m_vAngle, tFlick.m_vAngles[nBeforeIndex].m_vAngle) < 1.0f)
					nBeforeIndex++;

				auto& recBefore = tFlick.m_vAngles[nBeforeIndex];

				const float flSimDelta1 = recShot.m_flSimTime - recBefore.m_flSimTime;
				const float flSimDelta2 = recAfter.m_flSimTime - recShot.m_flSimTime;
				if (flSimDelta1 <= 0.f || flSimDelta2 <= 0.f || flSimDelta1 > 1.0f || flSimDelta2 > 1.0f)
					continue;

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

				const float flFovIn = Math::CalcFov(vBefore, vShot);
				const float flFovOut = Math::CalcFov(vShot, vAfter);
				const float flMinFov = std::min(flFovIn, flFovOut);
				const float flMaxFov = std::max(flFovIn, flFovOut);
				const float flRatio = flMinFov / (flMaxFov + 0.001f);

				const bool bReversal = flCos < -0.60f && flRatio >= 0.30f;

				const Vec3 vDeltaBase = vAfter.DeltaAngle(vBefore);
				Vec3 vMid = vBefore + vDeltaBase * 0.5f;
				Math::ClampAngles(vMid);
				const float flExcursion = Math::CalcFov(vShot, vMid);

				if (!bReversal && flExcursion < 0.20f)
					continue;

				if (flExcursion < 0.12f)
					continue;

				if (pVictim)
					{
						const Vec3 vShootPos = recShot.m_vShootPos.IsZero() ? (pAttacker->m_vecOrigin() + pAttacker->GetViewOffset()) : recShot.m_vShootPos;
						float flVictimMinFov = 999.f;
						float flBeforeMinFov = 999.f;

						matrix3x4* aVictimBones = F::Backtrack.GetBones(pVictim);
						if (aVictimBones)
						{
							const int nHitboxes = std::min(pVictim->GetNumOfHitboxes(), 18);
							for (int n = 0; n < nHitboxes; n++)
							{
								const Vec3 vHitbox = pVictim->GetHitboxCenter(aVictimBones, n);
								const Vec3 vAngleToHitbox = Math::CalcAngle(vShootPos, vHitbox);
								const float flFov = Math::CalcFov(vShot, vAngleToHitbox);
								if (flFov < flVictimMinFov)
								{
									flVictimMinFov = flFov;
									flBeforeMinFov = Math::CalcFov(vBefore, vAngleToHitbox);
								}
							}
						}

						std::vector<TickRecord*> vVictimRecords = {};
						if (F::Backtrack.GetRecords(pVictim, vVictimRecords))
						{
							for (auto pRecord : vVictimRecords)
							{
								if (!pRecord || pRecord->m_bInvalid)
									continue;

								if (pVictim->GetNumOfHitboxes() > 0)
								{
									const Vec3 vRecordHead = pVictim->GetHitboxCenter(pRecord->m_aBones, 0);
									const Vec3 vAngleToHead = Math::CalcAngle(vShootPos, vRecordHead);
									const float flHeadFov = Math::CalcFov(vShot, vAngleToHead);
									if (flHeadFov < flVictimMinFov)
									{
										flVictimMinFov = flHeadFov;
										flBeforeMinFov = Math::CalcFov(vBefore, vAngleToHead);
									}
								}

								const Vec3 vRecordCenter = pRecord->m_vOrigin + pVictim->GetViewOffset() * 0.5f;
								const Vec3 vAngleToRecord = Math::CalcAngle(vShootPos, vRecordCenter);
								const float flRecordFov = Math::CalcFov(vShot, vAngleToRecord);
								if (flRecordFov < flVictimMinFov)
								{
									flVictimMinFov = flRecordFov;
									flBeforeMinFov = Math::CalcFov(vBefore, vAngleToRecord);
								}
							}
						}

						const Vec3 vVictimEye = pVictim->GetEyePosition();
						const Vec3 vAngleToEye = Math::CalcAngle(vShootPos, vVictimEye);
						const float flEyeFov = Math::CalcFov(vShot, vAngleToEye);
						if (flEyeFov < flVictimMinFov)
						{
							flVictimMinFov = flEyeFov;
							flBeforeMinFov = Math::CalcFov(vBefore, vAngleToEye);
						}

						const Vec3 vVictimCenter = pVictim->GetCenter();
						const Vec3 vAngleToCenter = Math::CalcAngle(vShootPos, vVictimCenter);
						const float flCenterFov = Math::CalcFov(vShot, vAngleToCenter);
						if (flCenterFov < flVictimMinFov)
						{
							flVictimMinFov = flCenterFov;
							flBeforeMinFov = Math::CalcFov(vBefore, vAngleToCenter);
						}

						const float flDistance = vShootPos.DistTo(vVictimCenter);
						const float flMaxHitboxAngle = std::clamp(Math::Rad2Deg(atan2f(32.f, std::max(flDistance, 32.f))), 0.5f, 7.0f);

						if (flVictimMinFov <= flMaxHitboxAngle && flVictimMinFov < flBeforeMinFov)
						{
							recShot.m_bDamage = true;
							tFlick.m_bInfract = true;
							break;
						}
					}
					else if (bReversal && flExcursion >= 0.35f)
					{
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
