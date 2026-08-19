#include "ESP.h"
#include <span>

#include "../Groups/Groups.h"
#include "../../Players/PlayerUtils.h"
#include "../../Spectate/Spectate.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"

static inline bool GetDistanceThing(Vector vTargetPos, Vector vLocalPos, Group_t* pGroup, float& flOut)
{
	const Vec3 vDelta = vTargetPos - vLocalPos;
	const float flDistSqr = vDelta.LengthSqr();
	const float flStart = pGroup->m_tESP.Start;
	const float flEnd = pGroup->m_tESP.End;
	if (flDistSqr < flStart * flStart || flDistSqr > flEnd * flEnd)
		return false;

	const float flDistance = flDistSqr > 0.f ? sqrtf(flDistSqr) : 0.f;
	flOut = pGroup->m_tColor.a;
	if (pGroup->m_tESP.SmoothAlpha)
	{
		flOut = Math::RemapVal(flDistance, flEnd - 256.f, flEnd, flOut, 0.f);
		if (flStart)
			flOut = Math::RemapVal(flDistance, flStart + 256.f, flStart, flOut, 0.f);
	}
	flOut /= 255.f;
	return true;
}

static inline void StorePlayer(CTFPlayer* pPlayer, CTFPlayer* pLocal, Group_t* pGroup, std::vector<std::pair<CBaseEntity*, PlayerCache_t>>& mCache)
{
	int iIndex = pPlayer->entindex();

	if (int iObserverMode = pLocal->m_iObserverMode(); iObserverMode == OBS_MODE_FIRSTPERSON || iObserverMode == OBS_MODE_THIRDPERSON
		? iObserverMode == OBS_MODE_FIRSTPERSON && pLocal->m_hObserverTarget().GetEntryIndex() == iIndex
		: !I::Input->CAM_IsThirdPerson() && iIndex == I::EngineClient->GetLocalPlayer())
		return;

	auto pWeapon = pPlayer->m_hActiveWeapon()->As<CTFWeaponBase>();
	auto pResource = H::Entities.GetResource();
	bool bLocal = pPlayer->entindex() == I::EngineClient->GetLocalPlayer();
	int iClassNum = pPlayer->m_iClass();

	float flAlpha;
	if (!GetDistanceThing(pPlayer->m_vecOrigin(), pLocal->m_vecOrigin(), pGroup, flAlpha)) 
		return;

	mCache.emplace_back(pPlayer, PlayerCache_t{});
	PlayerCache_t& tCache = mCache.back().second;
	tCache.m_vText.reserve(8);
	tCache.m_vBars.reserve(4);
	tCache.m_flAlpha = flAlpha;
	tCache.m_tColor = F::Groups.GetColor(pPlayer, pGroup).Alpha(255);
	tCache.m_bBox = pGroup->m_tESP.Draw & ESPEnum::Box;
	tCache.m_bBones = pGroup->m_tESP.Draw & ESPEnum::Bones;

	if (pGroup->m_tESP.Draw & ESPEnum::Distance && !bLocal)
	{
		Vec3 vDelta = pPlayer->m_vecOrigin() - pLocal->m_vecOrigin();
		{ char _buf[64]; snprintf(_buf, 64, "[%.0fM]", vDelta.Length2D() / 41); tCache.m_vText.emplace_back(ALIGN_BOTTOM, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }
	}

	if (pResource)
	{
		if (pGroup->m_tESP.Draw & ESPEnum::Name)
			tCache.m_vText.emplace_back(ALIGN_TOP, F::PlayerUtils.GetPlayerName(iIndex, pResource->GetName(iIndex)).c_str(), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, (pGroup->m_tESP.Draw & ESPEnum::NameBackground) ? pGroup->m_tESP.BackgroundOpacity : 0);

		if (pGroup->m_tESP.Draw & (ESPEnum::Labels | ESPEnum::Priority) && !pResource->IsFakePlayer(iIndex))
		{
			uint32_t uAccountID = pResource->m_iAccountID(iIndex);

			if (pGroup->m_tESP.Draw & ESPEnum::Priority)
			{
				if (auto pTag = F::PlayerUtils.GetSignificantTag(uAccountID, 1))
					tCache.m_vText.emplace_back(ALIGN_TOP, pTag->m_sName.c_str(), pTag->m_tColor, pTag->m_tColor.IsColorDark() ? Color_t(255, 255, 255) : Color_t(0, 0, 0));
			}

			if (pGroup->m_tESP.Draw & ESPEnum::Labels)
			{
				std::array<std::tuple<std::string, Color_t, int>, 16> vTagsArr = {};
				int nTags = 0;
				auto TagPush = [&](const std::string& s, Color_t c, int p) { if (nTags < 16) vTagsArr[nTags++] = { s, c, p }; };
				for (auto& iID : F::PlayerUtils.GetPlayerTags(uAccountID))
				{
					auto pTag = F::PlayerUtils.GetTag(iID);
					if (pTag && pTag->m_bLabel)
						TagPush(pTag->m_sName, pTag->m_tColor, pTag->m_iPriority);
				}
				if (H::Entities.IsFriend(uAccountID))
				{
					auto pTag = &F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(FRIEND_TAG)];
					if (pTag->m_bLabel)
						TagPush(pTag->m_sName, pTag->m_tColor, pTag->m_iPriority);
				}
				if (auto iParty = H::Entities.GetParty(uAccountID))
				{
					auto pTag = &F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(PARTY_TAG)];
					if (int iPartyCount = H::Entities.GetPartyCount() + 1; pTag->m_bLabel)
					{
						if (!--iParty)
							TagPush(pTag->m_sName, pTag->m_tColor, pTag->m_iPriority);
						else
							{ char _buf[64]; snprintf(_buf, 64, "%s: %d", pTag->m_sName.c_str(), iParty); TagPush(_buf, pTag->m_tColor.HueShift(iParty * 360.f / iPartyCount), pTag->m_iPriority); }
					}
				}
				if (H::Entities.IsF2P(uAccountID))
				{
					auto pTag = &F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(F2P_TAG)];
					if (pTag->m_bLabel)
						TagPush(pTag->m_sName, pTag->m_tColor, pTag->m_iPriority);
				}

				if (nTags > 0)
				{
					std::sort(vTagsArr.begin(), vTagsArr.begin() + nTags, [](const auto& a, const auto& b) -> bool
					{
						if (std::get<2>(a) != std::get<2>(b))
							return std::get<2>(a) > std::get<2>(b);
						return std::get<0>(a) < std::get<0>(b);
					});

					for (int i = 0; i < nTags; i++)
					{
						auto& [sName, tColor, _] = vTagsArr[i];
						tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, sName.c_str(), tColor, tColor.IsColorDark() ? Color_t(255, 255, 255) : Color_t(0, 0, 0));
					}
				}
			}
		}
	}

	float flHealth = pPlayer->m_iHealth(), flMaxHealth = pPlayer->GetMaxHealth();
	if (pGroup->m_tESP.Draw & ESPEnum::HealthBar)
	{
		tCache.m_flHealth = flHealth > flMaxHealth
			? 1.f + std::clamp((flHealth - flMaxHealth) / (floorf(flMaxHealth / 10.f) * 5), 0.f, 1.f)
			: std::clamp(flHealth / flMaxHealth, 0.f, 1.f);
			
		Color_t tColor = Vars::Colors::IndicatorBad.Value.Lerp(Vars::Colors::IndicatorGood.Value, std::clamp(tCache.m_flHealth, 0.f, 1.f), LerpEnum::HSV);
		Bar_t& tBar = tCache.m_vBars.emplace_back();
		tBar.m_iMode = ALIGN_LEFT;
		tBar.m_flPercent = tCache.m_flHealth;
		tBar.m_tColor = tColor;
		tBar.m_tOverfill = Vars::Colors::IndicatorMisc.Value;
		tBar.m_tBackground = Color_t(0, 0, 0, 120);
		tBar.m_bSmooth = true;
	}
	if (pGroup->m_tESP.Draw & ESPEnum::HealthText)
		{ char _buf[64]; snprintf(_buf, 64, "%.0f", static_cast<float>(flHealth)); tCache.m_vText.emplace_back(ALIGN_LEFT, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }

	if (pGroup->m_tESP.Draw & (ESPEnum::UberBar | ESPEnum::UberText) && iClassNum == TF_CLASS_MEDIC)
	{
		auto pMediGun = pPlayer->GetWeaponFromSlot(SLOT_SECONDARY);
		if (pMediGun && pMediGun->GetClassID() == ETFClassID::CWeaponMedigun)
		{
			float flUber = std::clamp(pMediGun->As<CWeaponMedigun>()->m_flChargeLevel(), 0.f, 1.f);
			if (pGroup->m_tESP.Draw & ESPEnum::UberBar)
			{
				Bar_t& bar = tCache.m_vBars.emplace_back();
				bar.m_iMode = ALIGN_BOTTOM;
				bar.m_flPercent = flUber;
				bar.m_tColor = Vars::Colors::IndicatorMisc.Value;
				bar.m_tBackground = Color_t(0, 0, 0, 120);
				bar.m_bSmooth = false;
			}
			if (pGroup->m_tESP.Draw & ESPEnum::UberText)
				{ char _buf[64]; snprintf(_buf, 64, "%.0f%%", flUber * 100.f); tCache.m_vText.emplace_back(ALIGN_BOTTOMRIGHT, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }
		}
	}

	if (pGroup->m_tESP.Draw & ESPEnum::ClassIcon)
		tCache.m_iClassIcon = iClassNum;
	if (pGroup->m_tESP.Draw & ESPEnum::ClassText)
		tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, SDK::GetClassByIndex(iClassNum, false), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

	if (pGroup->m_tESP.Draw & ESPEnum::WeaponIcon && pWeapon)
		tCache.m_pWeaponIcon = pWeapon->GetWeaponIcon();
	if (pGroup->m_tESP.Draw & ESPEnum::WeaponText && pWeapon)
		tCache.m_vText.emplace_back(ALIGN_BOTTOM, SDK::ConvertWideToUTF8(pWeapon->GetWeaponName()).c_str(), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

	if (pGroup->m_tESP.Draw & ESPEnum::LagCompensation && !pPlayer->IsDormant() && !bLocal)
	{
		if (H::Entities.GetLagCompensation(iIndex))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Lagcomp", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
	}

	if (pGroup->m_tESP.Draw & ESPEnum::Ping && pResource && !bLocal)
	{
		int iPing = pResource->m_iPing(iIndex);
		if (iPing && (iPing >= 200 || iPing <= 5))
			{ char _buf[64]; snprintf(_buf, 64, "%dMS", iPing); tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, _buf, Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value); }
	}

	if (pGroup->m_tESP.Draw & ESPEnum::KDR && pResource && !bLocal)
	{
		int iKills = pResource->m_iScore(iIndex), iDeaths = pResource->m_iDeaths(iIndex);
		if (iKills >= 20)
		{
			int iKDR = iKills / std::max(iDeaths, 1);
			if (iKDR >= 10)
				{ char _buf[64]; snprintf(_buf, 64, "High KD [%d / %d]", iKills, iDeaths); tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, _buf, Vars::Colors::IndicatorTextMid.Value, Vars::Menu::Theme::Background.Value); }
		}
	}

	// Add the Mafia Works feature implementation
	if (pGroup->m_tESP.Draw & ESPEnum::ThatsHowMafiaWorks && pResource && !bLocal)
	{
		int iKills = pResource->m_iScore(iIndex);
		int iDeaths = pResource->m_iDeaths(iIndex);
		int iDamage = pResource->m_iDamage(iIndex);

		// Calculate player level based on stats
		int iLevel = 1;
		if (iKills >= 30 && iDamage >= 10000) iLevel = 6;
		else if (iKills >= 20 && iDamage >= 5000) iLevel = 5;
		else if (iKills >= 15 && iDamage >= 3000) iLevel = 4;
		else if (iKills >= 10 && iDamage >= 2000) iLevel = 3;
		else if (iKills >= 5 && iDamage >= 500) iLevel = 2;

		// Define title based on level
		std::string sTitle;
		Color_t tTitleColor;
		switch (iLevel)
		{
		case 1:
			sTitle = "Lv.1 Crook";
			tTitleColor = Color_t(150, 150, 150, 255); // Grey
			break;
		case 2:
			sTitle = "Lv.10 Gangster";
			tTitleColor = Color_t(76, 175, 80, 255); // Green
			break;
		case 3:
			sTitle = "Lv.35 Hitman";
			tTitleColor = Color_t(33, 150, 243, 255); // Blue
			break;
		case 4:
			sTitle = "Lv.50 Boss";
			tTitleColor = Color_t(156, 39, 176, 255); // Purple
			break;
		case 5:
			sTitle = "Lv.80 Godfather";
			tTitleColor = Color_t(211, 47, 47, 255); // Red
			break;
		case 6:
			sTitle = "Lv.100 BOSS OF ALL BOSSES";
			tTitleColor = Color_t(255, 193, 7, 255); // Gold
			break;
		}

		tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, sTitle.c_str(), tTitleColor, Color_t(0, 0, 0, 200));
	}

	// Buffs
	if (pGroup->m_tESP.Draw & ESPEnum::Buffs)
	{
		if (pPlayer->InCond(TF_COND_INVULNERABLE) ||
			pPlayer->InCond(TF_COND_INVULNERABLE_HIDE_UNLESS_DAMAGED) ||
			pPlayer->InCond(TF_COND_INVULNERABLE_USER_BUFF) ||
			pPlayer->InCond(TF_COND_INVULNERABLE_CARD_EFFECT))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Uber", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		else if (pPlayer->InCond(TF_COND_MEGAHEAL))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Megaheal", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		else if (pPlayer->InCond(TF_COND_PHASE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Bonk", Vars::Colors::IndicatorTextMid.Value, Vars::Menu::Theme::Background.Value);

		bool bCrits = pPlayer->IsCritBoosted(), bMiniCrits = pPlayer->IsMiniCritBoosted();
		if (pWeapon)
		{
			if (bMiniCrits && SDK::AttribHookValue(0, "minicrits_become_crits", pWeapon)
				|| SDK::AttribHookValue(0, "crit_while_airborne", pWeapon) && pPlayer->InCond(TF_COND_BLASTJUMPING))
				bCrits = true, bMiniCrits = false;
			if (bCrits && SDK::AttribHookValue(0, "crits_become_minicrits", pWeapon))
				bCrits = false, bMiniCrits = true;
		}
		if (bCrits)
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Crits", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		else if (bMiniCrits)
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Mini-crits", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);

		/* vaccinator effects */
		if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) || pPlayer->InCond(TF_COND_BULLET_IMMUNE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Bullet+", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		else if (pPlayer->InCond(TF_COND_MEDIGUN_SMALL_BULLET_RESIST))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Bullet", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST) || pPlayer->InCond(TF_COND_BLAST_IMMUNE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Blast+", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		else if (pPlayer->InCond(TF_COND_MEDIGUN_SMALL_BLAST_RESIST))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Blast", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST) || pPlayer->InCond(TF_COND_FIRE_IMMUNE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Fire+", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		else if (pPlayer->InCond(TF_COND_MEDIGUN_SMALL_FIRE_RESIST))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Fire", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_OFFENSEBUFF))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Banner", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_DEFENSEBUFF))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Battalions", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_REGENONDAMAGEBUFF))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Conch", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_RUNE_STRENGTH))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Strength", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_HASTE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Haste", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_REGEN))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Regen", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_RESIST))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Resistance", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_VAMPIRE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Vampire", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_REFLECT))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Reflect", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_PRECISION))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Precision", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_AGILITY))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Agility", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_KNOCKOUT))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Knockout", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_KING))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "King", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_PLAGUE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Plague", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_RUNE_SUPERNOVA))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Supernova", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		if (pPlayer->InCond(TF_COND_POWERUPMODE_DOMINANT))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Dominant", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		for (int i = 0; i < MAX_WEAPONS; i++)
		{
			auto pWeapon = pPlayer->GetWeaponFromSlot(i)->As<CTFSpellBook>();
			if (!pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_SPELLBOOK || !pWeapon->m_iSpellCharges())
				continue;

			switch (pWeapon->m_iSelectedSpellIndex())
			{
			case 0: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Fireball", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 1: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Bats", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 2: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Heal", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 3: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Pumpkins", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 4: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Jump", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 5: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Stealth", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 6: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Teleport", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 7: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Lightning", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 8: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Minify", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 9: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Meteors", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 10: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Monoculus", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 11: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Skeletons", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 12: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Glove", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 13: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Parachute", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 14: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Heal", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			case 15: tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Bomb", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); break;
			}
		}

		if (pPlayer->InCond(TF_COND_RADIUSHEAL) ||
			pPlayer->InCond(TF_COND_HEALTH_BUFF) ||
			pPlayer->InCond(TF_COND_RADIUSHEAL_ON_DAMAGE) ||
			pPlayer->InCond(TF_COND_HALLOWEEN_QUICK_HEAL) ||
			pPlayer->InCond(TF_COND_HALLOWEEN_HELL_HEAL) ||
			pPlayer->InCond(TF_COND_KING_BUFFED))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Heal", Vars::Colors::IndicatorTextGood.Value, Vars::Menu::Theme::Background.Value);
		else if (pPlayer->InCond(TF_COND_HEALTH_OVERHEALED))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "HP+", Vars::Colors::IndicatorTextGood.Value, Vars::Menu::Theme::Background.Value);

		//if (pPlayer->InCond(TF_COND_BLASTJUMPING))
		//	tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Blastjump", Vars::Colors::IndicatorTextMid.Value, Vars::Menu::Theme::Background.Value);
	}

	// Debuffs
	if (pGroup->m_tESP.Draw & ESPEnum::Debuffs)
	{
		if (pPlayer->InCond(TF_COND_MARKEDFORDEATH)
			|| pPlayer->InCond(TF_COND_MARKEDFORDEATH_SILENT)
			|| pPlayer->InCond(TF_COND_PASSTIME_PENALTY_DEBUFF))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Marked", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_URINE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Jarate", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_MAD_MILK))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Milk", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_STUNNED))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Stun", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_BURNING))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Burn", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_BLEEDING))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Bleed", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
	}

	// Misc
	if (pGroup->m_tESP.Draw & ESPEnum::Flags)
	{
		if (pPlayer->m_bFeignDeathReady())
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "DR", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		else if (pPlayer->InCond(TF_COND_FEIGN_DEATH))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Feign", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (float flInvis = pPlayer->GetEffectiveInvisibilityLevel())
			{ char _buf[64]; snprintf(_buf, 64, "Invis %.0f%%", flInvis * 100.f); tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }

		if (pPlayer->InCond(TF_COND_DISGUISED))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Disguise", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (pPlayer->InCond(TF_COND_AIMING) || pPlayer->InCond(TF_COND_ZOOMED))
		{
			switch (pWeapon ? pWeapon->GetWeaponID() : -1)
			{
			case TF_WEAPON_MINIGUN:
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Rev", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
				break;
			case TF_WEAPON_SNIPERRIFLE:
			case TF_WEAPON_SNIPERRIFLE_CLASSIC:
			case TF_WEAPON_SNIPERRIFLE_DECAP:
			{
				if (bLocal)
				{
					{ char _buf[64]; snprintf(_buf, 64, "Charging %.0f%%", Math::RemapVal(pWeapon->As<CTFSniperRifle>()->m_flChargedDamage(), 0.f, 150.f, 0.f, 100.f)); tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }
					break;
				}
				else
				{
					auto fGetSniperDot = [](CBaseEntity* pEntity) -> CSniperDot*
					{
						for (auto pDot : H::Entities.GetGroup(EntityEnum::SniperDots))
						{
							if (pDot->m_hOwnerEntity().Get() == pEntity)
								return pDot->As<CSniperDot>();
						}
						return nullptr;
					};
					if (CSniperDot* pPlayerDot = fGetSniperDot(pPlayer))
					{
						float flChargeTime = std::max(SDK::AttribHookValue(3.f, "mult_sniper_charge_per_sec", pWeapon), 1.5f);
						{ char _buf[64]; snprintf(_buf, 64, "Charging %.0f%%", Math::RemapVal(TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick) - pPlayerDot->m_flChargeStartTime() - 0.3f, 0.f, flChargeTime, 0.f, 100.f)); tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }
						break;
					}
				}
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Charging", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
				break;
			}
			case TF_WEAPON_COMPOUND_BOW:
				if (bLocal)
				{
					{ char _buf[64]; snprintf(_buf, 64, "Charging %.0f%%", Math::RemapVal(TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick) - pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime(), 0.f, 1.f, 0.f, 100.f)); tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }
					break;
				}
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Charging", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
				break;
			default:
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Charging", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
			}
		}

		if (pPlayer->InCond(TF_COND_SHIELD_CHARGE))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Charging", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (Vars::Visuals::Removals::Taunts.Value && pPlayer->InCond(TF_COND_TAUNTING))
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Taunt", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);

		if (Vars::Debug::Info.Value && !bLocal /*&& !pPlayer->IsDormant()*/)
		{
			int iAverage = TIME_TO_TICKS(F::MoveSim.GetPredictedDelta(pPlayer));
			int iCurrent = H::Entities.GetChoke(iIndex);
			{ char _buf[64]; snprintf(_buf, 64, "Lag %d, %d", iAverage, iCurrent); tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }
		}
	}
}

static inline void StoreBuilding(CBaseObject* pBuilding, CTFPlayer* pLocal, Group_t* pGroup, std::vector<std::pair<CBaseEntity*, BuildingCache_t>>& mCache)
{
	auto pOwner = pBuilding->m_hBuilder().Get();
	int iIndex = pOwner ? pOwner->entindex() : -1;

	bool bIsMini = pBuilding->m_bMiniBuilding();

	float flAlpha;
	if (!GetDistanceThing(pBuilding->m_vecOrigin(), pLocal->m_vecOrigin(), pGroup, flAlpha)) 
		return;

	mCache.emplace_back(pBuilding, BuildingCache_t{});
	BuildingCache_t& tCache = mCache.back().second;
	tCache.m_vText.reserve(6);
	tCache.m_vBars.reserve(2);
	tCache.m_flAlpha = flAlpha;
	tCache.m_tColor = F::Groups.GetColor(pOwner ? pOwner : pBuilding, pGroup).Alpha(255);
	tCache.m_bBox = pGroup->m_tESP.Draw & ESPEnum::Box;

	if (pGroup->m_tESP.Draw & ESPEnum::Distance)
	{
		Vec3 vDelta = pBuilding->m_vecOrigin() - pLocal->m_vecOrigin();
		{ char _buf[64]; snprintf(_buf, 64, "[%.0fM]", vDelta.Length2D() / 41); tCache.m_vText.emplace_back(ALIGN_BOTTOM, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }
	}

	if (pGroup->m_tESP.Draw & ESPEnum::Name)
	{
		const char* sName = "Building";
		switch (pBuilding->GetClassID())
		{
		case ETFClassID::CObjectSentrygun: sName = bIsMini ? "Mini-Sentry" : "Sentry"; break;
		case ETFClassID::CObjectDispenser: sName = "Dispenser"; break;
		case ETFClassID::CObjectTeleporter: sName = pBuilding->m_iObjectMode() ? "Teleporter Exit" : "Teleporter Entrance";
		}
		tCache.m_vText.emplace_back(ALIGN_TOP, sName.c_str(), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, (pGroup->m_tESP.Draw & ESPEnum::NameBackground) ? pGroup->m_tESP.BackgroundOpacity : 0);
	}

	float flHealth = pBuilding->m_iHealth(), flMaxHealth = pBuilding->m_iMaxHealth();
	if (pGroup->m_tESP.Draw & ESPEnum::HealthBar)
	{
		tCache.m_flHealth = std::clamp(flHealth / flMaxHealth, 0.f, 1.f);
		
		Color_t tColor = Vars::Colors::IndicatorBad.Value.Lerp(Vars::Colors::IndicatorGood.Value, std::clamp(tCache.m_flHealth, 0.f, 1.f), LerpEnum::HSV);
		Bar_t& tBar = tCache.m_vBars.emplace_back();
		tBar.m_iMode = ALIGN_LEFT;
		tBar.m_flPercent = tCache.m_flHealth;
		tBar.m_tColor = tColor;
		tBar.m_tOverfill = Vars::Colors::IndicatorMisc.Value;
		tBar.m_tBackground = Color_t(0, 0, 0, 120);
		tBar.m_bSmooth = true;
	}
	if (pGroup->m_tESP.Draw & ESPEnum::HealthText)
		{ char _buf[64]; snprintf(_buf, 64, "%.0f", static_cast<float>(flHealth)); tCache.m_vText.emplace_back(ALIGN_LEFT, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }

	if (pGroup->m_tESP.Draw & (ESPEnum::AmmoBars | ESPEnum::AmmoText) && pBuilding->IsSentrygun() && !pBuilding->m_bBuilding())
	{
		int iShells, iMaxShells, iRockets, iMaxRockets; pBuilding->As<CObjectSentrygun>()->GetAmmoCount(iShells, iMaxShells, iRockets, iMaxRockets);

		if (pGroup->m_tESP.Draw & ESPEnum::AmmoBars)
		{
			Bar_t& shellBar = tCache.m_vBars.emplace_back();
			shellBar.m_iMode = ALIGN_BOTTOM;
			shellBar.m_flPercent = float(iShells) / iMaxShells;
			shellBar.m_tColor = Vars::Menu::Theme::Inactive.Value;
			shellBar.m_bSmooth = false;
			
			if (iMaxRockets)
			{
				Bar_t& rocketBar = tCache.m_vBars.emplace_back();
				rocketBar.m_iMode = ALIGN_BOTTOM;
				rocketBar.m_flPercent = float(iRockets) / iMaxRockets;
				rocketBar.m_tColor = Vars::Menu::Theme::Inactive.Value;
				rocketBar.m_bSmooth = false;
			}
		}
		if (pGroup->m_tESP.Draw & ESPEnum::AmmoText)
		{
			{ char _buf[64]; snprintf(_buf, 64, "%d", iShells); tCache.m_vText.emplace_back(ALIGN_BOTTOMRIGHT, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }
			if (iMaxRockets)
				{ char _rbuf[32]; snprintf(_rbuf, 32, ", %d", iRockets); strncat_s(tCache.m_vText.back().m_sText, sizeof(tCache.m_vText.back().m_sText), _rbuf, _TRUNCATE); }
		}
	}

	if (pGroup->m_tESP.Draw & ESPEnum::Owner && !pBuilding->m_bWasMapPlaced() && pOwner)
	{
		if (auto pResource = H::Entities.GetResource(); pResource)
			tCache.m_vText.emplace_back(ALIGN_TOP, F::PlayerUtils.GetPlayerName(iIndex, pResource->GetName(iIndex)).c_str(), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
	}

	if (pGroup->m_tESP.Draw & ESPEnum::Level && !bIsMini)
		{ char _buf[64]; snprintf(_buf, 64, "Level %d", pBuilding->m_iUpgradeLevel()); tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }

	if (pGroup->m_tESP.Draw & ESPEnum::Flags)
	{
		if (!pBuilding->IsDormant() && pBuilding->m_bBuilding())
			{ char _buf[64]; snprintf(_buf, 64, "%.0f%%", pBuilding->m_flPercentageConstructed() * 100.f); tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }

		if (pBuilding->IsSentrygun() && pBuilding->As<CObjectSentrygun>()->m_bPlayerControlled())
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Wrangled", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);

		if (pBuilding->m_bHasSapper())
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Sapped", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
		else if (pBuilding->m_bDisabled())
			tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Disabled", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
	}
}

static inline const char* GetProjectileName(CBaseEntity* pProjectile)
{
	const char* sReturn = "Projectile";
	switch (pProjectile->GetClassID())
	{
	case ETFClassID::CTFWeaponBaseMerasmusGrenade: sReturn = "Bomb"; break;
	case ETFClassID::CTFGrenadePipebombProjectile: sReturn = pProjectile->As<CTFGrenadePipebombProjectile>()->HasStickyEffects() ? "Sticky" : "Pipe"; break;
	case ETFClassID::CTFStunBall: sReturn = "Baseball"; break;
	case ETFClassID::CTFBall_Ornament: sReturn = "Bauble"; break;
	case ETFClassID::CTFProjectile_Jar: sReturn = "Jarate"; break;
	case ETFClassID::CTFProjectile_Cleaver: sReturn = "Cleaver"; break;
	case ETFClassID::CTFProjectile_JarGas: sReturn = "Gas"; break;
	case ETFClassID::CTFProjectile_JarMilk:
	case ETFClassID::CTFProjectile_ThrowableBreadMonster: sReturn = "Milk"; break;
	case ETFClassID::CTFProjectile_SpellBats:
	case ETFClassID::CTFProjectile_SpellKartBats: sReturn = "Bats"; break;
	case ETFClassID::CTFProjectile_SpellMeteorShower: sReturn = "Meteors"; break;
	case ETFClassID::CTFProjectile_SpellMirv:
	case ETFClassID::CTFProjectile_SpellPumpkin: sReturn = "Pumpkin"; break;
	case ETFClassID::CTFProjectile_SpellSpawnBoss: sReturn = "Monoculus"; break;
	case ETFClassID::CTFProjectile_SpellSpawnHorde:
	case ETFClassID::CTFProjectile_SpellSpawnZombie: sReturn = "Skeleton"; break;
	case ETFClassID::CTFProjectile_SpellTransposeTeleport: sReturn = "Teleport"; break;
	case ETFClassID::CTFProjectile_Arrow: sReturn = pProjectile->As<CTFProjectile_Arrow>()->m_iProjectileType() == TF_PROJECTILE_BUILDING_REPAIR_BOLT ? "Repair" : "Arrow"; break;
	case ETFClassID::CTFProjectile_GrapplingHook: sReturn = "Grapple"; break;
	case ETFClassID::CTFProjectile_HealingBolt: sReturn = "Heal"; break;
	case ETFClassID::CTFProjectile_Rocket:
	case ETFClassID::CTFProjectile_EnergyBall:
	case ETFClassID::CTFProjectile_SentryRocket: sReturn = "Rocket"; break;
	case ETFClassID::CTFProjectile_BallOfFire: sReturn = "Fire"; break;
	case ETFClassID::CTFProjectile_MechanicalArmOrb: sReturn = "Short circuit"; break;
	case ETFClassID::CTFProjectile_SpellFireball: sReturn = "Fireball"; break;
	case ETFClassID::CTFProjectile_SpellLightningOrb: sReturn = "Lightning"; break;
	case ETFClassID::CTFProjectile_SpellKartOrb: sReturn = "Fist"; break;
	case ETFClassID::CTFProjectile_Flare: sReturn = "Flare"; break;
	case ETFClassID::CTFProjectile_EnergyRing: sReturn = "Energy"; break;
	}
	return sReturn;
}
static inline void StoreProjectile(CBaseEntity* pProjectile, CTFPlayer* pLocal, Group_t* pGroup, std::vector<std::pair<CBaseEntity*, EntityCache_t>>& mCache)
{
	auto pOwner = F::ProjSim.GetEntities(pProjectile).second;
	int iIndex = pOwner ? pOwner->entindex() : -1;

	float flAlpha;
	if (!GetDistanceThing(pProjectile->m_vecOrigin(), pLocal->m_vecOrigin(), pGroup, flAlpha)) 
		return;

	mCache.emplace_back(pProjectile, EntityCache_t{});
	EntityCache_t& tCache = mCache.back().second;
	tCache.m_vText.reserve(4);
	tCache.m_flAlpha = flAlpha;
	tCache.m_tColor = F::Groups.GetColor(pOwner ? pOwner : pProjectile, pGroup);
	tCache.m_bBox = pGroup->m_tESP.Draw & ESPEnum::Box;

	if (pGroup->m_tESP.Draw & ESPEnum::Distance)
	{
		Vec3 vDelta = pProjectile->m_vecOrigin() - pLocal->m_vecOrigin();
		{ char _buf[64]; snprintf(_buf, 64, "[%.0fM]", vDelta.Length2D() / 41); tCache.m_vText.emplace_back(ALIGN_BOTTOM, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }
	}

	if (pGroup->m_tESP.Draw & ESPEnum::Name)
		tCache.m_vText.emplace_back(ALIGN_TOP, GetProjectileName(pProjectile).c_str(), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, (pGroup->m_tESP.Draw & ESPEnum::NameBackground) ? pGroup->m_tESP.BackgroundOpacity : 0);

	if (pGroup->m_tESP.Draw & ESPEnum::Owner && pOwner)
	{
		if (auto pResource = H::Entities.GetResource(); pResource)
			tCache.m_vText.emplace_back(ALIGN_TOP, F::PlayerUtils.GetPlayerName(iIndex, pResource->GetName(iIndex)).c_str(), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
	}

	if (pGroup->m_tESP.Draw & ESPEnum::Flags)
	{
		switch (pProjectile->GetClassID())
		{
		case ETFClassID::CTFWeaponBaseGrenadeProj:
		case ETFClassID::CTFWeaponBaseMerasmusGrenade:
		case ETFClassID::CTFGrenadePipebombProjectile:
		case ETFClassID::CTFStunBall:
		case ETFClassID::CTFBall_Ornament:
		case ETFClassID::CTFProjectile_Jar:
		case ETFClassID::CTFProjectile_Cleaver:
		case ETFClassID::CTFProjectile_JarGas:
		case ETFClassID::CTFProjectile_JarMilk:
		case ETFClassID::CTFProjectile_SpellBats:
		case ETFClassID::CTFProjectile_SpellKartBats:
		case ETFClassID::CTFProjectile_SpellMeteorShower:
		case ETFClassID::CTFProjectile_SpellMirv:
		case ETFClassID::CTFProjectile_SpellPumpkin:
		case ETFClassID::CTFProjectile_SpellSpawnBoss:
		case ETFClassID::CTFProjectile_SpellSpawnHorde:
		case ETFClassID::CTFProjectile_SpellSpawnZombie:
		case ETFClassID::CTFProjectile_SpellTransposeTeleport:
		case ETFClassID::CTFProjectile_Throwable:
		case ETFClassID::CTFProjectile_ThrowableBreadMonster:
		case ETFClassID::CTFProjectile_ThrowableBrick:
		case ETFClassID::CTFProjectile_ThrowableRepel:
			if (pProjectile->As<CTFWeaponBaseGrenadeProj>()->m_bCritical())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Crit", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			if (pProjectile->As<CTFWeaponBaseGrenadeProj>()->m_iDeflected() && (pProjectile->GetClassID() != ETFClassID::CTFGrenadePipebombProjectile || !pProjectile->GetAbsVelocity().IsZero()))
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Reflected", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			break;
		case ETFClassID::CTFProjectile_Arrow:
		case ETFClassID::CTFProjectile_GrapplingHook:
		case ETFClassID::CTFProjectile_HealingBolt:
			if (pProjectile->As<CTFProjectile_Arrow>()->m_bCritical())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Crit", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			if (pProjectile->As<CTFBaseRocket>()->m_iDeflected())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Reflected", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			if (pProjectile->As<CTFProjectile_Arrow>()->m_bArrowAlight())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Alight", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			break;
		case ETFClassID::CTFProjectile_Rocket:
		case ETFClassID::CTFProjectile_BallOfFire:
		case ETFClassID::CTFProjectile_MechanicalArmOrb:
		case ETFClassID::CTFProjectile_SentryRocket:
		case ETFClassID::CTFProjectile_SpellFireball:
		case ETFClassID::CTFProjectile_SpellLightningOrb:
		case ETFClassID::CTFProjectile_SpellKartOrb:
			if (pProjectile->As<CTFProjectile_Rocket>()->m_bCritical())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Crit", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			if (pProjectile->As<CTFBaseRocket>()->m_iDeflected())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Reflected", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			break;
		case ETFClassID::CTFProjectile_EnergyBall:
			if (pProjectile->As<CTFProjectile_EnergyBall>()->m_bChargedShot())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Charge", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			if (pProjectile->As<CTFBaseRocket>()->m_iDeflected())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Reflected", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			break;
		case ETFClassID::CTFProjectile_Flare:
			if (pProjectile->As<CTFProjectile_Flare>()->m_bCritical())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Crit", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			if (pProjectile->As<CTFBaseRocket>()->m_iDeflected())
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Reflected", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			break;
		}
	}
}

static inline void StoreObjective(CBaseEntity* pObjective, CTFPlayer* pLocal, Group_t* pGroup, std::vector<std::pair<CBaseEntity*, EntityCache_t>>& mCache)
{
	auto pOwner = pObjective->m_hOwnerEntity()->As<CTFPlayer>();
	if (pOwner == pLocal)
		return;

	float flAlpha;
	if (!GetDistanceThing(pObjective->m_vecOrigin(), pLocal->m_vecOrigin(), pGroup, flAlpha)) 
		return;

	mCache.emplace_back(pObjective, EntityCache_t{});
	EntityCache_t& tCache = mCache.back().second;
	tCache.m_vText.reserve(4);
	tCache.m_flAlpha = flAlpha;
	tCache.m_tColor = F::Groups.GetColor(pObjective, pGroup);
	tCache.m_bBox = pGroup->m_tESP.Draw & ESPEnum::Box;

	if (pGroup->m_tESP.Draw & ESPEnum::Distance)
	{
		Vec3 vDelta = pObjective->m_vecOrigin() - pLocal->m_vecOrigin();
		{ char _buf[64]; snprintf(_buf, 64, "[%.0fM]", vDelta.Length2D() / 41); tCache.m_vText.emplace_back(ALIGN_BOTTOM, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }
	}

	switch (pObjective->GetClassID())
	{
	case ETFClassID::CCaptureFlag:
	{
		auto pIntel = pObjective->As<CCaptureFlag>();

		if (pGroup->m_tESP.Draw & ESPEnum::Name)
			tCache.m_vText.emplace_back(ALIGN_TOP, "Intel", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, (pGroup->m_tESP.Draw & ESPEnum::NameBackground) ? pGroup->m_tESP.BackgroundOpacity : 0);

		if (pGroup->m_tESP.Draw & ESPEnum::Flags)
		{
			switch (pIntel->m_nFlagStatus())
			{
			case TF_FLAGINFO_HOME:
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Home", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
				break;
			case TF_FLAGINFO_DROPPED:
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Dropped", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value);
				break;
			default:
				tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, "Stolen", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value);
			}
		}

		if (pGroup->m_tESP.Draw & ESPEnum::IntelReturnTime && pIntel->m_nFlagStatus() == TF_FLAGINFO_DROPPED)
		{
			float flReturnTime = std::max(pIntel->m_flResetTime() - TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick), 0.f);
			{ char _buf[64]; snprintf(_buf, 64, "Return %.1fs", pIntel->m_flResetTime() - TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick)); tCache.m_vText.emplace_back(ALIGN_TOPRIGHT, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }
		}

		break;
	}
	}
}

static inline void StoreMisc(CBaseEntity* pEntity, CTFPlayer* pLocal, Group_t* pGroup, std::vector<std::pair<CBaseEntity*, EntityCache_t>>& mCache)
{
	float flAlpha;
	if (!GetDistanceThing(pEntity->m_vecOrigin(), pLocal->m_vecOrigin(), pGroup, flAlpha)) 
		return;

	mCache.emplace_back(pEntity, EntityCache_t{});
	EntityCache_t& tCache = mCache.back().second;
	tCache.m_vText.reserve(4);
	tCache.m_flAlpha = flAlpha;
	tCache.m_tColor = F::Groups.GetColor(pEntity, pGroup);
	tCache.m_bBox = pGroup->m_tESP.Draw & ESPEnum::Box;

	if (pGroup->m_tESP.Draw & ESPEnum::Distance)
	{
		Vec3 vDelta = pEntity->m_vecOrigin() - pLocal->m_vecOrigin();
		{ char _buf[64]; snprintf(_buf, 64, "[%.0fM]", vDelta.Length2D() / 41); tCache.m_vText.emplace_back(ALIGN_BOTTOM, _buf, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value); }
	}

	if (pGroup->m_tESP.Draw & ESPEnum::Name)
	{
		const char* sName = "Unknown";
		switch (pEntity->GetClassID())
		{
		case ETFClassID::CTFBaseBoss: sName = "NPC"; break;
		case ETFClassID::CTFTankBoss: sName = "Tank"; break;
		case ETFClassID::CMerasmus: sName = "Merasmus"; break;
		case ETFClassID::CEyeballBoss: sName = "Monoculus"; break;
		case ETFClassID::CHeadlessHatman: sName = "Horseless Headless Horsemann"; break;
		case ETFClassID::CZombie: sName = "Skeleton"; break;
		case ETFClassID::CBaseAnimating:
		{
			auto uHash = H::Entities.GetModel(pEntity->entindex());
			if (H::Entities.IsHealth(uHash))
				sName = "Health";
			else if (H::Entities.IsAmmo(uHash))
				sName = "Ammo";
			else if (H::Entities.IsSpellbook(uHash))
				sName = "Spellbook";
			else if (H::Entities.IsPowerup(uHash))
			{
				sName = "Powerup";
				switch (uHash)
				{
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_agility.mdl"): sName = "Agility"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_crit.mdl"): sName = "Revenge"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_defense.mdl"): sName = "Resistance"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_haste.mdl"): sName = "Haste"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_king.mdl"): sName = "King"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_knockout.mdl"): sName = "Knockout"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_plague.mdl"): sName = "Plague"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_precision.mdl"): sName = "Precision"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_reflect.mdl"): sName = "Reflect"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_regen.mdl"): sName = "Regeneration"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_strength.mdl"): sName = "Strength"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_supernova.mdl"): sName = "Supernova"; break;
				case FNV1A::Hash32Const("models/pickups/pickup_powerup_vampire.mdl"): sName = "Vampire";
				}
			}
			break;
		}
		case ETFClassID::CTFAmmoPack: sName = "Ammo"; break;
		case ETFClassID::CCurrencyPack: sName = "Money"; break;
		case ETFClassID::CTFGenericBomb:
		case ETFClassID::CTFPumpkinBomb: sName = "Bomb"; break;
		case ETFClassID::CHalloweenGiftPickup: sName = "Gargoyle"; break;
		}

		tCache.m_vText.emplace_back(ALIGN_TOP, sName.c_str(), pGroup->m_tColor, Vars::Menu::Theme::Background.Value, (pGroup->m_tESP.Draw & ESPEnum::NameBackground) ? pGroup->m_tESP.BackgroundOpacity : 0);
	}
}

void CESP::Store(CTFPlayer* pLocal)
{
	m_vPlayerCache.clear();
	m_vBuildingCache.clear();
	m_vEntityCache.clear();

	if (m_vPlayerCache.capacity() < 64) m_vPlayerCache.reserve(64);
	if (m_vBuildingCache.capacity() < 64) m_vBuildingCache.reserve(64);
	if (m_vEntityCache.capacity() < 128) m_vEntityCache.reserve(128);

	Math::AngleVectors(I::EngineClient->GetViewAngles(), &m_vViewForward);
	m_vViewOrigin = pLocal->GetAbsOrigin();
	if (!pLocal || !F::Groups.GroupsActive())
		return;

	for (auto& [pEntity, pGroup] : F::Groups.GetGroup(false))
	{
		if (!pGroup->m_tESP.Draw)
			continue;

		if (pEntity->IsDormant())
			continue;

		if (!FrustumCull(pEntity->m_vecOrigin(), m_vViewOrigin))
			continue;

		if (pEntity->IsPlayer())
			StorePlayer(pEntity->As<CTFPlayer>(), pLocal, pGroup, m_vPlayerCache);
		else if (pEntity->IsBuilding())
			StoreBuilding(pEntity->As<CBaseObject>(), pLocal, pGroup, m_vBuildingCache);
		else if (pEntity->IsProjectile())
			StoreProjectile(pEntity, pLocal, pGroup, m_vEntityCache);
		else if (pEntity->GetClassID() == ETFClassID::CCaptureFlag)
			StoreObjective(pEntity, pLocal, pGroup, m_vEntityCache);
		else
			StoreMisc(pEntity, pLocal, pGroup, m_vEntityCache);
	}
}

static matrix3x4 s_aBones[MAXSTUDIOBONES];
static matrix3x4 s_mTransform = {};

void CESP::Draw()
{
	m_sBarsSeenThisFrame.clear();
	Math::AngleMatrix({ 0.f, I::EngineClient->GetViewAngles().y, 0.f }, s_mTransform, false);

	DrawWorld();
	DrawBuildings();
	DrawPlayers();
	CleanupSmoothedBars();
}

void CESP::DrawPlayers()
{
	if (m_vPlayerCache.empty())
		return;

	const auto& fFont = H::Fonts.GetFont(FONT_ESP);
	const int nTall = fFont.m_nTall + H::Draw.Scale(2);
	for (auto& [pEntity, tCache] : m_vPlayerCache)
	{
		float x, y, w, h;
		if (!GetDrawBounds(pEntity, x, y, w, h))
			continue;

		int l = x - H::Draw.Scale(6), r = x + w + H::Draw.Scale(6), m = x + w / 2;
		int t = y - H::Draw.Scale(5), b = y + h + H::Draw.Scale(5);
		int lOffset = 0, rOffset = 0, bOffset = 0, tOffset = 0;
		I::MatSystemSurface->DrawSetAlphaMultiplier(tCache.m_flAlpha);
		
		if (tCache.m_bBox)
			H::Draw.LineRectOutline(x, y, w, h, tCache.m_tColor, { 0, 0, 0, 255 });

		if (tCache.m_bBones)
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			if (pPlayer->SetupBones(s_aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime))
			{
				int iHead = pPlayer->GetBaseToHitbox(HITBOX_HEAD);
				int iSpine2 = pPlayer->GetBaseToHitbox(HITBOX_SPINE2);
				int iPelvis = pPlayer->GetBaseToHitbox(HITBOX_PELVIS);
				int iLeftUpperarm = pPlayer->GetBaseToHitbox(HITBOX_LEFT_UPPERARM);
				int iLeftForearm = pPlayer->GetBaseToHitbox(HITBOX_LEFT_FOREARM);
				int iLeftHand = pPlayer->GetBaseToHitbox(HITBOX_LEFT_HAND);
				int iRightUpperarm = pPlayer->GetBaseToHitbox(HITBOX_RIGHT_UPPERARM);
				int iRightForearm = pPlayer->GetBaseToHitbox(HITBOX_RIGHT_FOREARM);
				int iRightHand = pPlayer->GetBaseToHitbox(HITBOX_RIGHT_HAND);
				int iLeftThigh = pPlayer->GetBaseToHitbox(HITBOX_LEFT_THIGH);
				int iLeftCalf = pPlayer->GetBaseToHitbox(HITBOX_LEFT_CALF);
				int iLeftFoot = pPlayer->GetBaseToHitbox(HITBOX_LEFT_FOOT);
				int iRightThigh = pPlayer->GetBaseToHitbox(HITBOX_RIGHT_THIGH);
				int iRightCalf = pPlayer->GetBaseToHitbox(HITBOX_RIGHT_CALF);
				int iRightFoot = pPlayer->GetBaseToHitbox(HITBOX_RIGHT_FOOT);

				DrawBones(pPlayer, s_aBones, { iHead, iSpine2, iPelvis }, tCache.m_tColor);
				DrawBones(pPlayer, s_aBones, { iSpine2, iLeftUpperarm, iLeftForearm, iLeftHand }, tCache.m_tColor);
				DrawBones(pPlayer, s_aBones, { iSpine2, iRightUpperarm, iRightForearm, iRightHand }, tCache.m_tColor);
				DrawBones(pPlayer, s_aBones, { iPelvis, iLeftThigh, iLeftCalf, iLeftFoot }, tCache.m_tColor);
				DrawBones(pPlayer, s_aBones, { iPelvis, iRightThigh, iRightCalf, iRightFoot }, tCache.m_tColor);
			}
		}

		size_t iBarIndex = 0;
		for (auto& bar : tCache.m_vBars)
		{
			float flPercent = bar.m_flPercent;
			if (bar.m_bSmooth)
			{
				BarKey tKey{ pEntity, static_cast<uint32_t>(iBarIndex) };
				flPercent = SmoothBarValue(tKey, flPercent);
				bar.m_flPercent = flPercent;
				tCache.m_flHealth = flPercent;
			}

			auto fDrawBar = [&](int x, int y, int w, int h, EAlign eAlign = ALIGN_LEFT)
				{
					if (bar.m_tBackground.a)
						H::Draw.FillRect(x - 1, y - 1, w + 2, h + 2, bar.m_tBackground);

					auto fDrawSegment = [&](float flAmount, const Color_t& tColor)
					{
						if (flAmount <= 0.f)
							return;
						int segX = x;
						int segY = y;
						int segW = w;
						int segH = h;
						auto scaleLength = [&](int length) -> int
						{
							return std::clamp(static_cast<int>(std::round(length * flAmount)), 0, length);
						};
						switch (eAlign)
						{
						case ALIGN_RIGHT:
						{
							segW = scaleLength(w);
							if (!segW)
								return;
							segX += w - segW;
							break;
						}
						case ALIGN_TOP:
						{
							segH = scaleLength(h);
							if (!segH)
								return;
							break;
						}
						case ALIGN_BOTTOM:
						{
							segH = scaleLength(h);
							if (!segH)
								return;
							segY += h - segH;
							break;
						}
						case ALIGN_LEFT:
						default:
						{
							segW = scaleLength(w);
							if (!segW)
								return;
							break;
						}
						}
						H::Draw.FillRect(segX, segY, segW, segH, tColor);
					};

					if (flPercent > 1.f)
					{
						fDrawSegment(1.f, bar.m_tColor);
						fDrawSegment(flPercent - 1.f, bar.m_tOverfill);
					}
					else
					{
						fDrawSegment(flPercent, bar.m_tColor);
					}
				};

			int iSpace = H::Draw.Scale(4);
			int iThickness = H::Draw.Scale(2, Scale_Round);
			switch (bar.m_iMode)
			{
			case ALIGN_LEFT:
				fDrawBar(x - iSpace - iThickness - lOffset, y, iThickness, h, ALIGN_BOTTOM);
				lOffset += iSpace + iThickness;
				break;
			case ALIGN_BOTTOM:
				fDrawBar(x, y + h + iSpace + bOffset, w, iThickness);
				bOffset += iSpace + iThickness;
				break;
			}
			++iBarIndex;
		}

		for (auto& [iMode, sText, tColor, tOutline, m_ucBackgroundAlpha] : tCache.m_vText)
		{
			switch (iMode)
			{
			case ALIGN_TOP:
				if (m_ucBackgroundAlpha)
				{
					Color_t tBackgroundOutline = tOutline;
					tBackgroundOutline.a = m_ucBackgroundAlpha;
					
					H::Draw.StringWithBackground(fFont, m, t - tOffset, tColor, tBackgroundOutline, ALIGN_BOTTOM, sText);
				}
				else
					H::Draw.StringOutlined(fFont, m, t - tOffset, tColor, tOutline, ALIGN_BOTTOM, sText);
				tOffset += nTall;
				break;
			case ALIGN_BOTTOM:
				H::Draw.StringOutlined(fFont, m, b + bOffset, tColor, tOutline, ALIGN_TOP, sText);
				bOffset += nTall;
				break;
			case ALIGN_LEFT:
				H::Draw.StringOutlined(fFont, l - lOffset, y - H::Draw.Scale(2) + h - h * std::min(tCache.m_flHealth, 1.f), tColor, tOutline, ALIGN_TOPRIGHT, sText);
				break;
			case ALIGN_TOPRIGHT:
				H::Draw.StringOutlined(fFont, r, y - H::Draw.Scale(2) + rOffset, tColor, tOutline, ALIGN_TOPLEFT, sText);
				rOffset += nTall;
				break;
			case ALIGN_BOTTOMRIGHT:
				H::Draw.StringOutlined(fFont, r, y + h, tColor, tOutline, ALIGN_TOPLEFT, sText);
				break;
			}
		}

		if (tCache.m_iClassIcon)
		{
			const char* sTexture = "vgui/glyph_multiplayer.vtf";
			switch (tCache.m_iClassIcon)
			{
			case TF_CLASS_SCOUT: sTexture = "hud/leaderboard_class_scout.vtf"; break;
			case TF_CLASS_SOLDIER: sTexture = "hud/leaderboard_class_soldier.vtf"; break;
			case TF_CLASS_PYRO: sTexture = "hud/leaderboard_class_pyro.vtf"; break;
			case TF_CLASS_DEMOMAN: sTexture = "hud/leaderboard_class_demo.vtf"; break;
			case TF_CLASS_HEAVY: sTexture = "hud/leaderboard_class_heavy.vtf"; break;
			case TF_CLASS_ENGINEER: sTexture = "hud/leaderboard_class_engineer.vtf"; break;
			case TF_CLASS_MEDIC: sTexture = "hud/leaderboard_class_medic.vtf"; break;
			case TF_CLASS_SNIPER: sTexture = "hud/leaderboard_class_sniper.vtf"; break;
			case TF_CLASS_SPY: sTexture = "hud/leaderboard_class_spy.vtf"; break;
			}
			int iSize = H::Draw.Scale(18, Scale_Round);
			H::Draw.Texture(sTexture, m, t - tOffset, iSize, iSize, ALIGN_BOTTOM);
		}

		if (tCache.m_pWeaponIcon)
		{
			float flW = tCache.m_pWeaponIcon->Width(), flH = tCache.m_pWeaponIcon->Height();
			float flScale = H::Draw.Scale(std::min((w + 40) / 2.f, 80.f) / std::max(flW, flH * 2));
			H::Draw.DrawHudTexture(m - flW / 2.f * flScale, b + bOffset, flScale, tCache.m_pWeaponIcon, Vars::Menu::Theme::Active.Value);
		}
	}

	I::MatSystemSurface->DrawSetAlphaMultiplier(1.f);
}

void CESP::DrawBuildings()
{
	if (m_vBuildingCache.empty())
		return;

	const auto& fFont = H::Fonts.GetFont(FONT_ESP);
	const int nTall = fFont.m_nTall + H::Draw.Scale(2);
	for (auto& [pEntity, tCache] : m_vBuildingCache)
	{
		float x, y, w, h;
		if (!GetDrawBounds(pEntity, x, y, w, h))
			continue;

		int l = x - H::Draw.Scale(6), r = x + w + H::Draw.Scale(6), m = x + w / 2;
		int t = y - H::Draw.Scale(5), b = y + h + H::Draw.Scale(5);
		int lOffset = 0, rOffset = 0, bOffset = 0, tOffset = 0;
		I::MatSystemSurface->DrawSetAlphaMultiplier(tCache.m_flAlpha);

		if (tCache.m_bBox)
			H::Draw.LineRectOutline(x, y, w, h, tCache.m_tColor, { 0, 0, 0, 255 });
		size_t iBarIndex = 0;
		for (auto& bar : tCache.m_vBars)
		{
			float flPercent = bar.m_flPercent;
			if (bar.m_bSmooth)
			{
				BarKey tKey{ pEntity, static_cast<uint32_t>(iBarIndex) };
				flPercent = SmoothBarValue(tKey, flPercent);
				bar.m_flPercent = flPercent;
				tCache.m_flHealth = flPercent;
			}

			auto fDrawBar = [&](int x, int y, int w, int h, EAlign eAlign = ALIGN_LEFT)
				{
					if (bar.m_tBackground.a)
						H::Draw.FillRect(x - 1, y - 1, w + 2, h + 2, bar.m_tBackground);

					auto fDrawSegment = [&](float flAmount, const Color_t& tColor)
					{
						if (flAmount <= 0.f)
							return;
						int segX = x;
						int segY = y;
						int segW = w;
						int segH = h;
						auto scaleLength = [&](int length) -> int
						{
							return std::clamp(static_cast<int>(std::round(length * flAmount)), 0, length);
						};
						switch (eAlign)
						{
						case ALIGN_RIGHT:
						{
							segW = scaleLength(w);
							if (!segW)
								return;
							segX += w - segW;
							break;
						}
						case ALIGN_TOP:
						{
							segH = scaleLength(h);
							if (!segH)
								return;
							break;
						}
						case ALIGN_BOTTOM:
						{
							segH = scaleLength(h);
							if (!segH)
								return;
							segY += h - segH;
							break;
						}
						case ALIGN_LEFT:
						default:
						{
							segW = scaleLength(w);
							if (!segW)
								return;
							break;
						}
						}
						H::Draw.FillRect(segX, segY, segW, segH, tColor);
					};

					if (flPercent > 1.f)
					{
						fDrawSegment(1.f, bar.m_tColor);
						fDrawSegment(flPercent - 1.f, bar.m_tOverfill);
					}
					else
					{
						fDrawSegment(flPercent, bar.m_tColor);
					}
				};

			int iSpace = H::Draw.Scale(4);
			int iThickness = H::Draw.Scale(2, Scale_Round);
			switch (bar.m_iMode)
			{
			case ALIGN_LEFT:
				fDrawBar(x - iSpace - iThickness - lOffset, y, iThickness, h, ALIGN_BOTTOM);
				lOffset += iSpace + iThickness;
				break;
			case ALIGN_BOTTOM:
				fDrawBar(x, y + h + iSpace + bOffset, w, iThickness);
				bOffset += iSpace + iThickness;
				break;
			}
			++iBarIndex;
		}

		for (auto& [iMode, sText, tColor, tOutline, m_ucBackgroundAlpha] : tCache.m_vText)
		{
			switch (iMode)
			{
			case ALIGN_TOP:
				if (m_ucBackgroundAlpha)
				{
					Color_t tBackgroundOutline = tOutline;
					tBackgroundOutline.a = m_ucBackgroundAlpha;
					
					H::Draw.StringWithBackground(fFont, m, t - tOffset, tColor, tBackgroundOutline, ALIGN_BOTTOM, sText);
				}
				else
					H::Draw.StringOutlined(fFont, m, t - tOffset, tColor, tOutline, ALIGN_BOTTOM, sText);
				tOffset += nTall;
				break;
			case ALIGN_BOTTOM:
				H::Draw.StringOutlined(fFont, m, b + bOffset, tColor, tOutline, ALIGN_TOP, sText);
				bOffset += nTall;
				break;
			case ALIGN_LEFT:
				H::Draw.StringOutlined(fFont, l - lOffset, y - H::Draw.Scale(2) + h - h * std::min(tCache.m_flHealth, 1.f), tColor, tOutline, ALIGN_TOPRIGHT, sText);
				break;
			case ALIGN_TOPRIGHT:
				H::Draw.StringOutlined(fFont, r, y - H::Draw.Scale(2) + rOffset, tColor, tOutline, ALIGN_TOPLEFT, sText);
				rOffset += nTall;
				break;
			case ALIGN_BOTTOMRIGHT:
				H::Draw.StringOutlined(fFont, r, y + h, tColor, tOutline, ALIGN_TOPLEFT, sText);
				break;
			}
		}
	}

	I::MatSystemSurface->DrawSetAlphaMultiplier(1.f);
}

void CESP::DrawWorld()
{
	if (m_vEntityCache.empty())
		return;

	const auto& fFont = H::Fonts.GetFont(FONT_ESP);
	const int nTall = fFont.m_nTall + H::Draw.Scale(2);
	for (auto& [pEntity, tCache] : m_vEntityCache)
	{
		float x, y, w, h;
		if (!GetDrawBounds(pEntity, x, y, w, h))
			continue;

		int l = x - H::Draw.Scale(6), r = x + w + H::Draw.Scale(6), m = x + w / 2;
		int t = y - H::Draw.Scale(5), b = y + h + H::Draw.Scale(5);
		int lOffset = 0, rOffset = 0, bOffset = 0, tOffset = 0;
		I::MatSystemSurface->DrawSetAlphaMultiplier(tCache.m_flAlpha);

		if (tCache.m_bBox)
			H::Draw.LineRectOutline(x, y, w, h, tCache.m_tColor, { 0, 0, 0, 255 });


		for (auto& [iMode, sText, tColor, tOutline, m_ucBackgroundAlpha] : tCache.m_vText)
		{
			switch (iMode)
			{
			case ALIGN_TOP:
				if (m_ucBackgroundAlpha)
				{
					Color_t tBackgroundOutline = tOutline;
					tBackgroundOutline.a = m_ucBackgroundAlpha;
					
					H::Draw.StringWithBackground(fFont, m, t - tOffset, tColor, tBackgroundOutline, ALIGN_BOTTOM, sText);
				}
				else
					H::Draw.StringOutlined(fFont, m, t - tOffset, tColor, tOutline, ALIGN_BOTTOM, sText);
				tOffset += nTall;
				break;
			case ALIGN_BOTTOM:
				H::Draw.StringOutlined(fFont, m, b + bOffset, tColor, tOutline, ALIGN_TOP, sText);
				bOffset += nTall;
				break;
			case ALIGN_TOPRIGHT:
				H::Draw.StringOutlined(fFont, r, y - H::Draw.Scale(2) + rOffset, tColor, tOutline, ALIGN_TOPLEFT, sText);
				rOffset += nTall;
				break;
			}
		}
	}

	I::MatSystemSurface->DrawSetAlphaMultiplier(1.f);
}

float CESP::SmoothBarValue(const BarKey& tKey, float flTarget)
{
	if (!tKey.m_pEntity)
		return flTarget;

	m_sBarsSeenThisFrame.insert(tKey);

	for (auto& [key, val] : m_vBarSmoothing)
	{
		if (key.m_pEntity == tKey.m_pEntity && key.m_uIndex == tKey.m_uIndex)
		{
			const float flStep = std::clamp(I::GlobalVars->frametime * 10.f, 0.f, 1.f);
			val = Math::Lerp(val, flTarget, flStep);
			if (std::fabs(val - flTarget) <= 0.001f)
				val = flTarget;
			return val;
		}
	}

	m_vBarSmoothing.emplace_back(tKey, flTarget);
	return flTarget;
}

void CESP::CleanupSmoothedBars()
{
	m_vBarSmoothing.erase(
		std::remove_if(m_vBarSmoothing.begin(), m_vBarSmoothing.end(),
			[this](const auto& kv) { return m_sBarsSeenThisFrame.find(kv.first) == m_sBarsSeenThisFrame.end(); }),
		m_vBarSmoothing.end());
}

bool CESP::FrustumCull(const Vec3& vOrigin, const Vec3& vViewOrigin) const
{
	Vec3 vDelta = vOrigin - vViewOrigin;
	return m_vViewForward.Dot(vDelta) > -128.f;
}

bool CESP::GetDrawBounds(CBaseEntity* pEntity, float& x, float& y, float& w, float& h)
{
	if (pEntity->IsDormant())
		return false;

	Vec3 vOrigin = pEntity->GetAbsOrigin();
	Vec3 vMins = pEntity->m_vecMins(), vMaxs = pEntity->m_vecMaxs();

	Vec3 vTop = vOrigin + Vec3(0.f, 0.f, vMaxs.z);
	Vec3 vBot = vOrigin + Vec3(0.f, 0.f, vMins.z);
	Vec3 sTop, sBot;
	if (!SDK::W2S(vTop, sTop) && !SDK::W2S(vBot, sBot))
		return false;

	Math::MatrixInitialize(s_mTransform, vOrigin, false);

	float flLeft, flRight, flTop, flBottom;
	if (!SDK::IsOnScreen(pEntity, s_mTransform, &flLeft, &flRight, &flTop, &flBottom, true))
		return false;

	x = flLeft;
	y = flBottom;
	w = flRight - flLeft;
	h = flTop - flBottom;

	switch (pEntity->GetClassID())
	{
	case ETFClassID::CTFPlayer:
	case ETFClassID::CObjectSentrygun:
	case ETFClassID::CObjectDispenser:
	case ETFClassID::CObjectTeleporter:
		x += w * 0.125f;
		w *= 0.75f;
	}

	return !(x > H::Draw.m_nScreenW || x + w < 0 || y > H::Draw.m_nScreenH || y + h < 0);
}

void CESP::DrawBones(CTFPlayer* pPlayer, matrix3x4* aBones, std::initializer_list<int> vBones, Color_t tColor)
{
	const int* pBones = vBones.begin();
	const size_t nCount = vBones.size();
	for (size_t n = 1; n < nCount; n++)
	{
		if (pBones[n] < 0 || pBones[n - 1] < 0)
			continue;

		auto vBone1 = pPlayer->GetHitboxCenter(aBones, pBones[n]);
		auto vBone2 = pPlayer->GetHitboxCenter(aBones, pBones[n - 1]);

		Vec3 vScreen1, vScreen2;
		if (SDK::W2S(vBone1, vScreen1) && SDK::W2S(vBone2, vScreen2))
			H::Draw.Line(vScreen1.x, vScreen1.y, vScreen2.x, vScreen2.y, tColor);
	}
}