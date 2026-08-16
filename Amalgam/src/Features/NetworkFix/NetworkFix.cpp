#include "NetworkFix.h"

using CLReadPacketsFn = void(__fastcall*)(bool);

void CReadPacketState::Store()
{
	m_flFrameTimeClientState = I::ClientState->m_frameTime;
	m_flFrameTime = I::GlobalVars->frametime;
	m_flCurTime = I::GlobalVars->curtime;
	m_nTickCount = I::GlobalVars->tickcount;
	m_nClientTick = I::ClientState->m_ClockDriftMgr.m_nClientTick;
	m_nServerTick = I::ClientState->m_ClockDriftMgr.m_nServerTick;
	m_nOldTickCount = I::ClientState->oldtickcount;
	m_flTickRemainder = I::ClientState->m_tickRemainder;
}

void CReadPacketState::Restore()
{
	I::ClientState->m_frameTime = m_flFrameTimeClientState;
	I::GlobalVars->frametime = m_flFrameTime;
	I::GlobalVars->curtime = m_flCurTime;
	I::GlobalVars->tickcount = m_nTickCount;
	I::ClientState->m_ClockDriftMgr.m_nClientTick = m_nClientTick;
	I::ClientState->m_ClockDriftMgr.m_nServerTick = m_nServerTick;
	I::ClientState->oldtickcount = m_nOldTickCount;
	I::ClientState->m_tickRemainder = m_flTickRemainder;
}

void CNetworkFix::FixInputDelay(bool bFinalTick)
{
	if (!Vars::Misc::Game::NetworkFix.Value || !I::EngineClient->IsInGame() || SDK::IsLoopback())
		return;

	m_tBackup.Store();

	static auto CL_ReadPackets = U::Hooks.m_mHooks["CL_ReadPackets"];
	CL_ReadPackets->As<CLReadPacketsFn>()(bFinalTick);

	m_tState.Store();
	m_tBackup.Restore();
}

bool CNetworkFix::ShouldReadPackets()
{
	if (!Vars::Misc::Game::NetworkFix.Value || !I::EngineClient->IsInGame() || SDK::IsLoopback())
		return true;

	m_tState.Restore();
	return false;
}