#include "../main.h"
#include "../game/game.h"
#include "netgame.h"
#include "../chatwindow.h"
#include "../button.h"
#include "util/armhook.h"

#define IS_TARGETING(x) (x & 128)

extern CGame *pGame;
extern CNetGame *pNetGame;
extern CChatWindow *pChatWindow;
extern CButton *pButton;

CRemotePlayer::CRemotePlayer()
{
	m_PlayerID = INVALID_PLAYER_ID;
	m_VehicleID = INVALID_VEHICLE_ID;
	m_pPlayerPed = nullptr;
	m_bIsNPC = false;
	m_bIsAFK = true;
	m_dwMarkerID = 0;
	m_dwGlobalMarkerID = 0;
	m_byteState = PLAYER_STATE_NONE;
	m_byteUpdateFromNetwork = UPDATE_TYPE_NONE;
	m_bShowNameTag = true;
	
	ResetAllSyncAttributes();
	
	m_dwLastRecvTick = 0;
	m_dwUnkTime = 0;
	
	m_byteCurrentWeapon = 0;
	m_byteSpecialAction = 0;
}

CRemotePlayer::~CRemotePlayer()
{
	Remove();
}

void CRemotePlayer::Process()
{
	CPlayerPool *pPool = pNetGame->GetPlayerPool();
	CLocalPlayer *pLocalPlayer = pPool->GetLocalPlayer();
	MATRIX4X4 matPlayer, matVehicle;
	VECTOR vecMoveSpeed;

	if(IsActive())
	{
		// ---- ONFOOT NETWORK PROCESSING ----
		if(GetState() == PLAYER_STATE_ONFOOT && 
			m_byteUpdateFromNetwork == UPDATE_TYPE_ONFOOT && !m_pPlayerPed->IsInVehicle())
		{
			UpdateOnFootPositionAndSpeed(&m_ofSync.vecPos, &m_ofSync.vecMoveSpeed);
			UpdateOnFootTargetPosition();

			m_pPlayerPed->GiveWeapon((int)m_ofSync.byteCurrentWeapon, 9999);

			if(m_pPlayerPed->IsAdded() && m_pPlayerPed->GetCurrentWeapon() != m_ofSync.byteCurrentWeapon) 
			{
  				m_pPlayerPed->GiveWeapon((int)m_ofSync.byteCurrentWeapon, 9999);

  				if(m_pPlayerPed->GetCurrentWeapon() != m_ofSync.byteCurrentWeapon) 
  					m_pPlayerPed->GiveWeapon((int)m_ofSync.byteCurrentWeapon, 9999);
  			}
		}
		else if(GetState() == PLAYER_STATE_DRIVER &&
			m_byteUpdateFromNetwork == UPDATE_TYPE_INCAR && m_pPlayerPed->IsInVehicle())
		{
			if(!m_pCurrentVehicle || !GamePool_Vehicle_GetAt(m_pCurrentVehicle->m_dwGTAId))
				return;
			
			m_icSync.quat.Normalize();
			m_icSync.quat.GetMatrix(&matVehicle);
			matVehicle.pos.X = m_icSync.vecPos.X;
			matVehicle.pos.Y = m_icSync.vecPos.Y;
			matVehicle.pos.Z = m_icSync.vecPos.Z;

			if( m_pCurrentVehicle->GetModelIndex() == TRAIN_PASSENGER_LOCO &&
				m_pCurrentVehicle->GetModelIndex() == TRAIN_FREIGHT_LOCO &&
				m_pCurrentVehicle->GetModelIndex() == TRAIN_TRAM)
			{
				UpdateTrainDriverMatrixAndSpeed(&matVehicle,&vecMoveSpeed, m_icSync.fTrainSpeed);
			}
			else
			{
				UpdateInCarMatrixAndSpeed(&matVehicle, &m_icSync.vecPos, &m_icSync.vecMoveSpeed);
				UpdateInCarTargetPosition();
			}
			
			// TRAILER AUTOMATIC ATTACHMENT AS THEY MOVE INTO IT
			if (m_pCurrentVehicle->GetDistanceFromLocalPlayerPed() < LOCKING_DISTANCE)
			{
				if (m_icSync.TrailerID && m_icSync.TrailerID < MAX_VEHICLES)
				{
					CVehicle* pRealTrailer = m_pCurrentVehicle->GetTrailer();
					CVehiclePool *pVehiclePool = pNetGame->GetVehiclePool();
					CVehicle *pTrailerVehicle = pVehiclePool->GetAt(m_icSync.TrailerID);
					if (!pRealTrailer) {
						if (pTrailerVehicle) {
							m_pCurrentVehicle->SetTrailer(pTrailerVehicle);
							m_pCurrentVehicle->AttachTrailer();
						}
					}
				} 
				else
				{
					if (m_pCurrentVehicle->GetTrailer()) {
						m_pCurrentVehicle->DetachTrailer();
						m_pCurrentVehicle->SetTrailer(NULL);
					}
				}
			}
			else
			{
				m_pCurrentVehicle->SetTrailer(NULL);
			}
			m_pCurrentVehicle->SetSirenOn(m_icSync.byteSirenOn);
			m_pCurrentVehicle->SetLightsState(m_icSync.byteLightState);
			m_pCurrentVehicle->SetHealth(m_icSync.fCarHealth);
		}
		else if(GetState() == PLAYER_STATE_PASSENGER && 
			m_byteUpdateFromNetwork == UPDATE_TYPE_PASSENGER)
		{
			if(!m_pCurrentVehicle) return;
		}
		m_byteUpdateFromNetwork = UPDATE_TYPE_NONE;

		// ------ PROCESSED FOR ALL FRAMES ----- 
		if(GetState() == PLAYER_STATE_ONFOOT && !m_pPlayerPed->IsInVehicle())
		{
			SlerpRotation();
			m_pPlayerPed->SetKeys(m_ofSync.wKeys, m_ofSync.lrAnalog, m_ofSync.udAnalog);
			m_bPassengerDriveByMode = false;
			
			if( m_ofSync.vecMoveSpeed.X == 0.0f &&
				m_ofSync.vecMoveSpeed.Y == 0.0f &&
				m_ofSync.vecMoveSpeed.Z == 0.0f)
			{
				m_pPlayerPed->SetMoveSpeedVector(m_ofSync.vecMoveSpeed);
			}

			if((GetTickCount() - m_dwLastRecvTick) > 1500)
				m_bIsAFK = true;

			if(m_bIsAFK && ((GetTickCount() - m_dwLastRecvTick) > 3000))
			{
				m_ofSync.lrAnalog = 0;
				m_ofSync.udAnalog = 0;
				
				vecMoveSpeed.X = 0.0f;
				vecMoveSpeed.Y = 0.0f;
				vecMoveSpeed.Z = 0.0f;
				m_pPlayerPed->SetMoveSpeedVector(vecMoveSpeed);
				
				m_pPlayerPed->GetMatrix(&matPlayer);
				matPlayer.pos.X = m_ofSync.vecPos.X;
				matPlayer.pos.Y = m_ofSync.vecPos.Y;
				matPlayer.pos.Z = m_ofSync.vecPos.Z;
				m_pPlayerPed->SetMatrix(matPlayer);
			}
		}
		else if(GetState() == PLAYER_STATE_DRIVER && m_pPlayerPed->IsInVehicle())
		{
			if( m_pCurrentVehicle->GetModelIndex() != TRAIN_PASSENGER_LOCO &&
				m_pCurrentVehicle->GetModelIndex() != TRAIN_FREIGHT_LOCO &&
				m_pCurrentVehicle->GetModelIndex() != TRAIN_TRAM)
			{
				UpdateVehicleRotation();
			}
			
			if(	m_icSync.vecMoveSpeed.X == 0.0f &&
				m_icSync.vecMoveSpeed.Y == 0.0f &&
				m_icSync.vecMoveSpeed.Z == 0.0f)
			{
				m_pCurrentVehicle->SetMoveSpeedVector(m_icSync.vecMoveSpeed);
			}
			m_pPlayerPed->SetKeys(m_icSync.wKeys, m_icSync.lrAnalog, m_icSync.udAnalog);
			m_bPassengerDriveByMode = false;

			if((GetTickCount() - m_dwLastRecvTick) > 1500)
				m_bIsAFK = true;
			
			if(m_iIsInAModShop)	
			{
				VECTOR vec = {0.0f, 0.0f, 0.0f};
				m_pPlayerPed->SetKeys(0, 0, 0);
				if(m_pCurrentVehicle) m_pCurrentVehicle->SetMoveSpeedVector(vec);
			}else
			{
				m_pPlayerPed->SetKeys(m_icSync.wKeys, m_icSync.lrAnalog, m_icSync.udAnalog);
			}
		}
		else if(GetState() == PLAYER_STATE_PASSENGER)
		{
			if((GetTickCount() - m_dwLastRecvTick) >= 3000)
				m_bIsAFK = true;
		}
		else
		{
			m_pPlayerPed->SetKeys(0, 0, 0);
			vecMoveSpeed.X = 0.0f;
			vecMoveSpeed.Y = 0.0f;
			vecMoveSpeed.Z = 0.0f;
			m_bPassengerDriveByMode = false;
			m_pPlayerPed->SetMoveSpeedVector(vecMoveSpeed);
		}

		if(m_byteState != PLAYER_STATE_WASTED)
			m_pPlayerPed->SetHealth(100.0f);

		if((GetTickCount() - m_dwLastRecvTick) < 1500)
			m_bIsAFK = false;
	}
	else
	{
		if(m_pPlayerPed)
		{
			ResetAllSyncAttributes();
			pGame->RemovePlayer(m_pPlayerPed);
			m_pPlayerPed = nullptr;
		}
	}
}

void CRemotePlayer::SlerpRotation()
{
	MATRIX4X4 mat;
	CQuaternion quatPlayer, quatResult;

	if(m_pPlayerPed)
	{
		m_pPlayerPed->GetMatrix(&mat);

		quatPlayer.SetFromMatrix(mat);

		quatResult.Slerp(&m_ofSync.quat, &quatPlayer, 0.75f);
		quatResult.GetMatrix(&mat);
		m_pPlayerPed->SetMatrix(mat);

		float fZ = atan2(-mat.up.X, mat.up.Y) * 57.295776; /* rad to deg */
		if(fZ > 360.0f) fZ -= 360.0f;
		if(fZ < 0.0f) fZ += 360.0f;
		m_pPlayerPed->SetRotation(fZ);
	}
}

void CRemotePlayer::UpdateInCarMatrixAndSpeed(MATRIX4X4* mat, VECTOR* pos, VECTOR* speed)
{
	m_InCarQuaternion.SetFromMatrix(*mat);

	m_vecInCarTargetPos.X = pos->X;
	m_vecInCarTargetPos.Y = pos->Y;
	m_vecInCarTargetPos.Z = pos->Z;

	m_vecInCarTargetSpeed.X = speed->X;
	m_vecInCarTargetSpeed.Y = speed->Y;
	m_vecInCarTargetSpeed.Z = speed->Z;

	m_pCurrentVehicle->SetMoveSpeedVector(*speed);
}

void CRemotePlayer::UpdateTrainDriverMatrixAndSpeed(MATRIX4X4 *matWorld,VECTOR *vecMoveSpeed, float fTrainSpeed)
{
	MATRIX4X4 matVehicle;
	VECTOR vecInternalMoveSpeed;
	bool bTeleport = false;
	float fDif;

	if(!m_pPlayerPed || !m_pCurrentVehicle) return;

	m_pCurrentVehicle->GetMatrix(&matVehicle);

	if(matWorld->pos.X >= matVehicle.pos.X) {
		fDif = matWorld->pos.X - matVehicle.pos.X;
	} else {
		fDif = matVehicle.pos.X - matWorld->pos.X;
	}
	if(fDif > 10.0f) bTeleport = true;

	if(matWorld->pos.Y >= matVehicle.pos.Y) {
		fDif = matWorld->pos.Y - matVehicle.pos.Y;
	} else {
		fDif = matVehicle.pos.Y - matWorld->pos.Y;
	}
	if(fDif > 10.0f) bTeleport = true;

	if(bTeleport) m_pCurrentVehicle->TeleportTo(matWorld->pos.X,matWorld->pos.Y,matWorld->pos.Z);
	
	m_pCurrentVehicle->GetMoveSpeedVector(&vecInternalMoveSpeed);
	vecInternalMoveSpeed.X = vecMoveSpeed->X;
	vecInternalMoveSpeed.Y = vecMoveSpeed->Y;
	vecInternalMoveSpeed.Z = vecMoveSpeed->Z;
	m_pCurrentVehicle->SetMoveSpeedVector(vecInternalMoveSpeed);
	m_pCurrentVehicle->SetTrainSpeed(fTrainSpeed);
}

void CRemotePlayer::UpdateInCarTargetPosition()
{
	MATRIX4X4 matEnt;
	VECTOR vec = { 0.0f, 0.0f, 0.0f };

	float delta = 0.0f;

	if(!m_pCurrentVehicle) return;

	m_pCurrentVehicle->GetMatrix(&matEnt);

	if(m_iIsInAModShop)
	{
		m_pPlayerPed->SetKeys(0, 0, 0);
		m_pCurrentVehicle->SetMoveSpeedVector(vec);
		m_icSync.udAnalog = 0;
		m_icSync.lrAnalog = 0;
		m_icSync.wKeys = 0;
		m_icSync.vecMoveSpeed.X = 0.0f;
		m_icSync.vecMoveSpeed.Y = 0.0f;
		m_icSync.vecMoveSpeed.Z = 0.0f;
		return;
	}
	
	if(m_pCurrentVehicle->IsAdded())
	{
		m_vecPosOffset.X = FloatOffset(m_vecInCarTargetPos.X, matEnt.pos.X);
		m_vecPosOffset.Y = FloatOffset(m_vecInCarTargetPos.Y, matEnt.pos.Y);
		m_vecPosOffset.Z = FloatOffset(m_vecInCarTargetPos.Z, matEnt.pos.Z);

		if(m_vecPosOffset.X > 0.05f || m_vecPosOffset.Y > 0.05f || m_vecPosOffset.Z > 0.05f)
		{
			delta = 0.5f;
			if( m_pCurrentVehicle->GetVehicleSubtype() == VEHICLE_SUBTYPE_BOAT ||
				m_pCurrentVehicle->GetVehicleSubtype() == VEHICLE_SUBTYPE_PLANE ||
				m_pCurrentVehicle->GetVehicleSubtype() == VEHICLE_SUBTYPE_HELI)
			{
				delta = 2.0f;
			}

			if(m_vecPosOffset.X > 8.0f || m_vecPosOffset.Y > 8.0f || m_vecPosOffset.Z > delta)
			{
				matEnt.pos.X = m_vecInCarTargetPos.X;
				matEnt.pos.Y = m_vecInCarTargetPos.Y;
				matEnt.pos.Z = m_vecInCarTargetPos.Z;
				m_pCurrentVehicle->SetMatrix(matEnt);
				m_pCurrentVehicle->SetMoveSpeedVector(m_vecInCarTargetSpeed);
			}
			else
			{
				m_pCurrentVehicle->GetMoveSpeedVector(&vec);
				if(m_vecPosOffset.X > 0.05f)
					vec.X += (m_vecInCarTargetPos.X - matEnt.pos.X) * 0.06f;
				if(m_vecPosOffset.Y > 0.05f)
					vec.Y += (m_vecInCarTargetPos.Y - matEnt.pos.Y) * 0.06f;
				if(m_vecPosOffset.Z > 0.05f)
					vec.Z += (m_vecInCarTargetPos.Z - matEnt.pos.Z) * 0.06f;

				if( FloatOffset(vec.X, 0.0f) > 0.01f ||
					FloatOffset(vec.Y, 0.0f) > 0.01f ||
					FloatOffset(vec.Z, 0.0f) > 0.01f)
				{
					m_pCurrentVehicle->SetMoveSpeedVector(vec);
				}
			}
		}
	}
	else
	{
		matEnt.pos.X = m_vecInCarTargetPos.X;
		matEnt.pos.Y = m_vecInCarTargetPos.Y;
		matEnt.pos.Z = m_vecInCarTargetPos.Z;
		m_pCurrentVehicle->SetMatrix(matEnt);
	}
}

void CRemotePlayer::UpdateVehicleRotation()
{
	CQuaternion quat, qresult;
	MATRIX4X4 matEnt;
	VECTOR vec = { 0.0f, 0.0f, 0.0f };

	if(!m_pCurrentVehicle) return;

	m_pCurrentVehicle->GetTurnSpeedVector(&vec);
	if(vec.X <= 0.02f)
	{
		if(vec.X < -0.02f) vec.X = -0.02f;
	}
	else vec.X = 0.02f;

	if(vec.Y <= 0.02f)
	{
		if(vec.Y < -0.02f) vec.Y = -0.02f;
	}
	else vec.Y = 0.02f;

	if(vec.Z <= 0.02f)
	{
		if(vec.Z < -0.02f) vec.Z = -0.02f;
	}
	else vec.Z = 0.02f;

	m_pCurrentVehicle->SetTurnSpeedVector(vec);

	m_pCurrentVehicle->GetMatrix(&matEnt);
	quat.SetFromMatrix(matEnt);
	qresult.Slerp(&m_InCarQuaternion, &quat, 0.75f);
	qresult.Normalize();
	qresult.GetMatrix(&matEnt);
	m_pCurrentVehicle->SetMatrix(matEnt);
}

bool CRemotePlayer::Spawn(uint8_t byteTeam, unsigned int iSkin, VECTOR *vecPos, float fRotation, 
	uint32_t dwColor, uint8_t byteFightingStyle, bool bVisible)
{
	if(m_pPlayerPed)
	{
		pGame->RemovePlayer(m_pPlayerPed);
		m_pPlayerPed = nullptr;
	}

	CPlayerPed *pPlayer = pGame->NewPlayer(iSkin, vecPos->X, vecPos->Y, vecPos->Z, fRotation);

	if(pPlayer)
	{
		if(dwColor != 0) SetPlayerColor(dwColor);

		if(m_dwMarkerID)
		{
			pGame->DisableMarker(m_dwMarkerID);
			m_dwMarkerID = 0;
		}

		if(pNetGame->m_iShowPlayerMarkers) 
			pPlayer->ShowMarker(m_PlayerID);

		m_pPlayerPed = pPlayer;
		m_fReportedHealth = 100.0f;
		if(byteFightingStyle != 4)
			m_pPlayerPed->SetFightingStyle(byteFightingStyle);

		SetState(PLAYER_STATE_SPAWNED);
		return true;
	}

	SetState(PLAYER_STATE_NONE);
	return false;
}

void CRemotePlayer::Remove()
{
	if(m_dwMarkerID)
	{
		pGame->DisableMarker(m_dwMarkerID);
		m_dwMarkerID = 0;
	}

	if(m_dwGlobalMarkerID)
	{
		pGame->DisableMarker(m_dwGlobalMarkerID);
		m_dwGlobalMarkerID = 0;
	}

	if(m_pPlayerPed)
	{
		pGame->RemovePlayer(m_pPlayerPed);
		m_pPlayerPed = nullptr;
	}
}

void CRemotePlayer::ResetAllSyncAttributes()
{	
	m_VehicleID = 0;
	memset(&m_ofSync, 0, sizeof(ONFOOT_SYNC_DATA));
	memset(&m_icSync, 0, sizeof(INCAR_SYNC_DATA));
	memset(&m_psSync, 0, sizeof(PASSENGER_SYNC_DATA));
	memset(&m_aimSync, 0, sizeof(AIM_SYNC_DATA));
	memset(&m_bulletSync, 0, sizeof(BULLET_SYNC_DATA));
	m_bPassengerDriveByMode = false;
	m_pCurrentVehicle = nullptr;
	m_fReportedHealth = 0.0f;
	m_fReportedArmour = 0.0f;
	m_iIsInAModShop = 0;
	m_byteSeatID = 0;
}

void CRemotePlayer::HandleDeath()
{
	CPlayerPool *pPlayerPool = pNetGame->GetPlayerPool();
	CLocalPlayer *pLocalPlayer = NULL;

	if (pPlayerPool) pLocalPlayer = pPlayerPool->GetLocalPlayer();

	if (pLocalPlayer) {
		if (pLocalPlayer->IsSpectating() && pLocalPlayer->m_SpectateID == m_PlayerID) {
				//pLocalPlayer->ToggleSpectating(FALSE);
		}
	}

	if(m_pPlayerPed) {
		m_pPlayerPed->SetKeys(0,0,0);
		m_pPlayerPed->SetDead();
	}

	// Dead weapon pickups
	if (m_pPlayerPed && m_pPlayerPed->GetDistanceFromLocalPlayerPed() <= 100.0f)
	{
		MATRIX4X4 matPlayer;
		m_pPlayerPed->GetMatrix(&matPlayer);

		int iWeaponID = m_pPlayerPed->GetCurrentWeapon();
		uint8_t ammo = m_pPlayerPed->GetAmmo();

		if (iWeaponID != 0 &&
			iWeaponID != WEAPON_GRENADE &&
			iWeaponID != WEAPON_TEARGAS &&
			iWeaponID != WEAPON_MOLTOV)
		{
			pGame->CreateWeaponPickup(iWeaponID, ammo, matPlayer.pos.X, matPlayer.pos.Y, matPlayer.pos.Z);
		}
	}

	SetState(PLAYER_STATE_WASTED);
	ResetAllSyncAttributes();
}

void CRemotePlayer::HandleVehicleEntryExit()
{
	CVehiclePool *pVehiclePool = pNetGame->GetVehiclePool();

	if(!m_pPlayerPed) return;

	if(!m_pPlayerPed->IsInVehicle())
	{
		if(pVehiclePool->GetAt(m_VehicleID))
		{
			int iCarID = pVehiclePool->FindGtaIDFromID(m_VehicleID);
			m_pPlayerPed->PutDirectlyInVehicle(iCarID, m_byteSeatID);
		}
	}
}

void CRemotePlayer::EnterVehicle(VEHICLEID VehicleID, bool bPassenger)
{
	CVehiclePool *pVehiclePool = pNetGame->GetVehiclePool();

	if( m_pPlayerPed &&
		pVehiclePool->GetAt(VehicleID) &&
		!m_pPlayerPed->IsInVehicle())
	{
		int iGtaVehicleID = pVehiclePool->FindGtaIDFromID(VehicleID);
		if(iGtaVehicleID && iGtaVehicleID != INVALID_VEHICLE_ID)
		{
			m_pPlayerPed->SetKeys(0, 0, 0);
			m_pPlayerPed->EnterVehicle(iGtaVehicleID, bPassenger);
		}
	}
}

void CRemotePlayer::ExitVehicle()
{
	if(m_pPlayerPed && m_pPlayerPed->IsInVehicle())
	{
		m_pPlayerPed->SetKeys(0, 0, 0);
		m_pPlayerPed->ExitCurrentVehicle();
	}
}

void CRemotePlayer::UpdateOnFootPositionAndSpeed(VECTOR *vecPos, VECTOR *vecMove)
{
	m_vecOnFootTargetPos.X = vecPos->X;
	m_vecOnFootTargetPos.Y = vecPos->Y;
	m_vecOnFootTargetPos.Z = vecPos->Z;
	m_vecOnFootTargetSpeed.X = vecMove->X;
	m_vecOnFootTargetSpeed.Y = vecMove->Y;
	m_vecOnFootTargetSpeed.Z = vecMove->Z;

	m_pPlayerPed->SetMoveSpeedVector(m_vecOnFootTargetSpeed);
}

void CRemotePlayer::UpdateOnFootTargetPosition()
{
	MATRIX4X4 mat;
	VECTOR vec;

	if(!m_pPlayerPed) return;
	m_pPlayerPed->GetMatrix(&mat);

	if(!m_pPlayerPed->IsAdded())
	{
		mat.pos.X = m_vecOnFootTargetPos.X;
		mat.pos.Y = m_vecOnFootTargetPos.Y;
		mat.pos.Z = m_vecOnFootTargetPos.Z;

		m_pPlayerPed->SetMatrix(mat);
		return;
	}

	m_vecPosOffset.X = FloatOffset(m_vecOnFootTargetPos.X, mat.pos.X);
	m_vecPosOffset.Y = FloatOffset(m_vecOnFootTargetPos.Y, mat.pos.Y);
	m_vecPosOffset.Z = FloatOffset(m_vecOnFootTargetPos.Z, mat.pos.Z);

	if(m_vecPosOffset.X > 0.00001f || m_vecPosOffset.Y > 0.00001f || m_vecPosOffset.Z > 0.00001f)
	{
		if(m_vecPosOffset.X > 2.0f || m_vecPosOffset.Y > 2.0f || m_vecPosOffset.Z > 1.0f)
		{
			mat.pos.X = m_vecOnFootTargetPos.X;
			mat.pos.Y = m_vecOnFootTargetPos.Y;
			mat.pos.Z = m_vecOnFootTargetPos.Z;
			m_pPlayerPed->SetMatrix(mat);
			return;
		}

		m_pPlayerPed->GetMoveSpeedVector(&vec);
		if(m_vecPosOffset.X > 0.00001f)
			vec.X += (m_vecOnFootTargetPos.X - mat.pos.X) * 0.1f;
		if(m_vecPosOffset.Y > 0.00001f)
			vec.Y += (m_vecOnFootTargetPos.Y - mat.pos.Y) * 0.1f;
		if(m_vecPosOffset.Z > 0.00001f)
			vec.Z += (m_vecOnFootTargetPos.Z - mat.pos.Z) * 0.1f;

		m_pPlayerPed->SetMoveSpeedVector(vec);
	}
}

void CRemotePlayer::SetPlayerColor(uint32_t dwColor)
{
	SetRadarColor(m_PlayerID, dwColor);
}

void CRemotePlayer::Say(unsigned char* szText)
{
	CPlayerPool *pPlayerPool = pNetGame->GetPlayerPool();

	if (pPlayerPool) 
	{
		char * szPlayerName = pPlayerPool->GetPlayerName(m_PlayerID);
		pChatWindow->AddChatMessage(szPlayerName, GetPlayerColor(), (char*)szText);
	}
}

void CRemotePlayer::StoreOnFootFullSyncData(ONFOOT_SYNC_DATA *pofSync, uint32_t dwTime)
{
	if( !dwTime || (dwTime - m_dwUnkTime) >= 0 )
	{
		m_dwUnkTime = dwTime;

		m_dwLastRecvTick = GetTickCount();
		memcpy(&m_ofSync, pofSync, sizeof(ONFOOT_SYNC_DATA));
		m_fReportedHealth = (float)pofSync->byteHealth;
		m_fReportedArmour = (float)pofSync->byteArmour;
		m_byteSpecialAction = pofSync->byteSpecialAction;

		m_byteCurrentWeapon = pofSync->byteCurrentWeapon;

		if(m_byteCurrentWeapon) NOP(g_libGTASA+0x434D94, 6);

		m_byteUpdateFromNetwork = UPDATE_TYPE_ONFOOT;

		if(m_pPlayerPed)
		{
			if(m_pPlayerPed->IsInVehicle())
			{
				if( m_byteSpecialAction != SPECIAL_ACTION_ENTER_VEHICLE && 
				m_byteSpecialAction != SPECIAL_ACTION_EXIT_VEHICLE /*&& !sub_100A6F00()*/)
					RemoveFromVehicle();
			}
		}

		SetState(PLAYER_STATE_ONFOOT);
	}
}

void CRemotePlayer::StoreAimFullSyncData(AIM_SYNC_DATA *paSync, uint32_t dwTime)
{
	if( !dwTime || (dwTime - m_dwUnkTime) >= 0 )
	{
		m_dwUnkTime = dwTime;

		m_dwLastRecvTick = GetTickCount();
		memcpy(&m_aimSync, paSync, sizeof(AIM_SYNC_DATA));

		// Cam mode
		m_byteCamMode = paSync->byteCamMode;

		// Aimf1
		m_vecAimf1.X = paSync->vecAimf1.X;
		m_vecAimf1.Y = paSync->vecAimf1.Y;
		m_vecAimf1.Z = paSync->vecAimf1.Z;

		// Position
		m_vecAimPos.X = paSync->vecAimPos.X;
		m_vecAimPos.Y = paSync->vecAimPos.Y;
		m_vecAimPos.Z = paSync->vecAimPos.Z;

		// AimZ
		m_fAimZ = paSync->fAimZ;

		// Zoom
		m_byteCamExtZoom = paSync->byteCamExtZoom;

		// Ratio
		m_byteAspectRatio = paSync->byteAspectRatio;

		// State
		m_byteWeaponState = WS_MORE_BULLETS;

		if(m_pPlayerPed)
		{
			if(m_pPlayerPed->IsInVehicle())
			{
				if( m_byteSpecialAction != SPECIAL_ACTION_ENTER_VEHICLE && 
				m_byteSpecialAction != SPECIAL_ACTION_EXIT_VEHICLE /*&& !sub_100A6F00()*/)
					RemoveFromVehicle();
			}
		}

		SetState(PLAYER_STATE_ONFOOT);
	}
}

void CRemotePlayer::StoreTrailerFullSyncData(TRAILER_SYNC_DATA *trSync)
{
	VEHICLEID TrailerID = m_icSync.TrailerID;
	if (!TrailerID) return;

	CVehiclePool *pVehiclePool = pNetGame->GetVehiclePool();
	CVehicle *pVehicle = NULL;

	if (pVehiclePool) pVehicle = pVehiclePool->GetAt(TrailerID);
	if (pVehicle) 
	{
		MATRIX4X4 matWorld;

		memcpy(&matWorld.pos, &trSync->vecPos, sizeof(VECTOR));
		DecompressNormalVector(&matWorld.up, &trSync->cvecDirection);
		DecompressNormalVector(&matWorld.right, &trSync->cvecRoll);
		
		pVehicle->SetMatrix(matWorld);
		pVehicle->SetMoveSpeedVector(trSync->vecMoveSpeed);
	}
}

void CRemotePlayer::StoreBulletFullSyncData(BULLET_SYNC_DATA *pbSync, uint32_t dwTime)
{
	if( !dwTime || (dwTime - m_dwUnkTime) >= 0 )
	{
		m_dwUnkTime = dwTime;

		m_dwLastRecvTick = GetTickCount();
		memcpy(&m_bulletSync, pbSync, sizeof(BULLET_SYNC_DATA));

		// target type
		m_targetType = pbSync->targetType;

		// target id
		m_targetId = pbSync->targetId;

		// origin
		m_vecOrigin.X = pbSync->vecOrigin.X;
		m_vecOrigin.Y = pbSync->vecOrigin.Y;
		m_vecOrigin.Z = pbSync->vecOrigin.Z;

		// target
		m_vecTarget.X = pbSync->vecTarget.X;
		m_vecTarget.Y = pbSync->vecTarget.Y;
		m_vecTarget.Z = pbSync->vecTarget.Z;

		// center
		m_vecCenter.X = pbSync->vecCenter.X;
		m_vecCenter.Y = pbSync->vecCenter.Y;
		m_vecCenter.Z = pbSync->vecCenter.Z;

		// weapon
		m_weaponId = pbSync->weaponId;

		if(m_pPlayerPed)
		{
			if(m_pPlayerPed->IsInVehicle())
			{
				if( m_byteSpecialAction != SPECIAL_ACTION_ENTER_VEHICLE && 
				m_byteSpecialAction != SPECIAL_ACTION_EXIT_VEHICLE /*&& !sub_100A6F00()*/)
					RemoveFromVehicle();
			}
		}

		SetState(PLAYER_STATE_ONFOOT);
	}
}

void CRemotePlayer::StoreInCarFullSyncData(INCAR_SYNC_DATA *picSync, uint32_t dwTime)
{
	if(!dwTime || (dwTime - m_dwUnkTime >= 0))
	{
		m_dwUnkTime = dwTime;

		CVehiclePool *pVehiclePool = pNetGame->GetVehiclePool();

		memcpy(&m_icSync, picSync, sizeof(INCAR_SYNC_DATA));
		m_VehicleID = picSync->VehicleID;
		if(pVehiclePool) m_pCurrentVehicle = pVehiclePool->GetAt(m_VehicleID);

		m_byteSeatID = 0;
		m_fReportedHealth = (float)picSync->bytePlayerHealth;
		m_fReportedArmour = (float)picSync->bytePlayerArmour;
		m_byteUpdateFromNetwork = UPDATE_TYPE_INCAR;
		m_dwLastRecvTick = GetTickCount();

		m_byteSpecialAction = 0;

		if(m_pPlayerPed)
		{
			m_pPlayerPed->m_byteCurrentWeapon = (uint8_t)picSync->byteCurrentWeapon;
		}
		
		if(m_pPlayerPed && !m_pPlayerPed->IsInVehicle())
			HandleVehicleEntryExit();

		SetState(PLAYER_STATE_DRIVER);
	}
}

void CRemotePlayer::StorePassengerFullSyncData(PASSENGER_SYNC_DATA *ppsSync)
{
	CVehiclePool *pVehiclePool = pNetGame->GetVehiclePool();

	memcpy(&m_psSync, ppsSync, sizeof(PASSENGER_SYNC_DATA));
	m_VehicleID = ppsSync->VehicleID;
	m_byteSeatID = ppsSync->byteSeatFlags & 127;
	if(pVehiclePool) m_pCurrentVehicle = pVehiclePool->GetAt(m_VehicleID);
	m_fReportedHealth = (float)ppsSync->bytePlayerHealth;
	m_fReportedArmour = (float)ppsSync->bytePlayerArmour;
	m_byteUpdateFromNetwork = UPDATE_TYPE_PASSENGER;
	m_dwLastRecvTick = GetTickCount();
		
	if(m_pPlayerPed && !m_pPlayerPed->IsInVehicle())
		HandleVehicleEntryExit();

	SetState(PLAYER_STATE_PASSENGER);
}

void CRemotePlayer::RemoveFromVehicle()
{
	MATRIX4X4 mat;

	if(m_pPlayerPed)
	{
		if(m_pPlayerPed->IsInVehicle())
		{
			m_pPlayerPed->GetMatrix(&mat);
			m_pPlayerPed->RemoveFromVehicleAndPutAt(mat.pos.X, mat.pos.Y, mat.pos.Z);
		}
	}
}

uint32_t CRemotePlayer::GetPlayerColor()
{
	return TranslateColorCodeToRGBA(m_PlayerID);
}

void CRemotePlayer::ShowGlobalMarker(short sX, short sY, short sZ)
{
	if(m_dwGlobalMarkerID)
	{
		pGame->DisableMarker(m_dwGlobalMarkerID);
		m_dwGlobalMarkerID = 0;
	}

	if(!m_pPlayerPed)
	{
		m_dwGlobalMarkerID = pGame->CreateRadarMarkerIcon(0, (float)sX, (float)sY, (float)sZ, m_PlayerID, 0);
	}
}

void CRemotePlayer::HideGlobalMarker()
{
	if(m_dwGlobalMarkerID)
	{
		pGame->DisableMarker(m_dwGlobalMarkerID);
		m_dwGlobalMarkerID = 0;
	}
}

void CRemotePlayer::StateChange(uint8_t byteNewState, uint8_t byteOldState)
{
	
}