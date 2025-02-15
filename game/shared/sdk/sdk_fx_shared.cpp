//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "sdk_fx_shared.h"
#include "weapon_sdkbase.h"
#include "sdk_weapon_parse.h"

#ifndef CLIENT_DLL
	#include "ilagcompensationmanager.h"
#endif

#ifdef CLIENT_DLL
	#include "fx_impact.h"
	#include "c_in_player.h"

	class CGroupedSound
	{
		public:
			string_t m_SoundName;
			Vector m_vPos;
	};

	CUtlVector<CGroupedSound> g_GroupedSounds;

	// this is a cheap ripoff from CBaseCombatWeapon::WeaponSound():
	void FX_WeaponSound( int iPlayerIndex, WeaponSound_t sound_type, const Vector &vOrigin, const CSDK_WeaponInfo *pWeaponInfo )
	{

		// If we have some sounds from the weapon classname.txt file, play a random one of them
		const char *shootsound = pWeaponInfo->aShootSounds[ sound_type ]; 

		if ( !shootsound || !shootsound[0] )
			return;

		CBroadcastRecipientFilter filter; // this is client side only

		if ( !te->CanPredict() )
			return;
				
		CBaseEntity::EmitSound( filter, iPlayerIndex, shootsound, &vOrigin ); 
	}


	// Called by the ImpactSound function.
	void ShotgunImpactSoundGroup( const char *pSoundName, const Vector &vEndPos )
	{
		// Don't play the sound if it's too close to another impact sound.
		/*for ( int i=0; i < g_GroupedSounds.Count(); i++ )
		{
			CGroupedSound *pSound = &g_GroupedSounds[i];

			if ( vEndPos.DistToSqr( pSound->m_vPos ) < 300*300 )
			{
				if ( Q_stricmp( pSound->m_SoundName, pSoundName ) == 0 )
					return;
			}
		}*/

		// Ok, play the sound and add it to the list.
		CLocalPlayerFilter filter;
		C_BaseEntity::EmitSound( filter, NULL, pSoundName, &vEndPos );

		int j = g_GroupedSounds.AddToTail();
		g_GroupedSounds[j].m_SoundName	= pSoundName;
		g_GroupedSounds[j].m_vPos		= vEndPos;
	}


	void StartGroupingSounds()
	{
		Assert( g_GroupedSounds.Count() == 0 );
		SetImpactSoundRoute( ShotgunImpactSoundGroup );
	}


	void EndGroupingSounds()
	{
		g_GroupedSounds.Purge();
		SetImpactSoundRoute( NULL );
	}

#else

	#include "in_player.h"
	
	// Server doesn't play sounds anyway.
	void StartGroupingSounds() {}
	void EndGroupingSounds() {}
	void FX_WeaponSound ( int iPlayerIndex,
		WeaponSound_t sound_type,
		const Vector &vOrigin,
		const CSDK_WeaponInfo *pWeaponInfo ) {};

#endif

// This runs on both the client and the server.
// On the server, it only does the damage calculations.
// On the client, it does all the effects.
void FX_FireBullets( CBaseInPlayer *pPlayer, const Vector &vOrigin, const QAngle &vAngles, const CSDK_WeaponInfo *pWeaponInfo, int iMode, int iSeed, float flSpread )
{
	bool bDoEffects = true;
	iSeed++;

	int	iDamage		= pWeaponInfo->m_iDamage;
	int	iAmmoType	= pWeaponInfo->iAmmoType;

	WeaponSound_t pSoundType = SINGLE;

	if ( bDoEffects)
	{
		FX_WeaponSound( pPlayer->entindex(), pSoundType, vOrigin, pWeaponInfo );
	}
	
	// Que se reproduzcan los sonidos de las balas cercanas
	StartGroupingSounds();

#if !defined (CLIENT_DLL)
	// Lagcompensation
	lagcompensation->StartLagCompensation( pPlayer, LAG_COMPENSATE_HITBOXES );
#endif

	for ( int iBullet = 0; iBullet < pWeaponInfo->m_iBullets; iBullet++ )
	{
		RandomSeed( iSeed );

		// Get circular gaussian spread.
		float x, y;
		x = RandomFloat( -0.5, 0.5 ) + RandomFloat( -0.5, 0.5 );
		y = RandomFloat( -0.5, 0.5 ) + RandomFloat( -0.5, 0.5 );
	
		iSeed++;
		pPlayer->FireBullet( vOrigin, vAngles, flSpread, iDamage, iAmmoType, pPlayer, bDoEffects, x,y );
	}

#if !defined (CLIENT_DLL)
	// Lagcompensation
	lagcompensation->FinishLagCompensation( pPlayer );
#endif

	// Paren los sonidos de las balas cercanas
	EndGroupingSounds();
}

