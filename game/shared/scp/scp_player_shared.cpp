//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
// Authors: 
// Iv�n Bravo Bravo (linkedin.com/in/ivanbravobravo), 2017

#include "cbase.h"
#include "in_gamerules.h"

#ifdef CLIENT_DLL
	#include "c_scp_player.h"
    #include "prediction.h"

	#define CSCP_Player C_SCP_Player
#else
    #include "scp_player.h"
#endif

#include "movevars_shared.h"
#include "sound_instance.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//================================================================================
// Commands
//================================================================================

extern ConVar sv_player_walk_speed;
extern ConVar sv_player_speed;
extern ConVar sv_player_sprint_speed;

// SCP-173 Speed
DECLARE_NOTIFY_CMD( sv_player_scp173_walk_speed, "100.0", "" )

//================================================================================
//================================================================================
void CSCP_Player::CreateAnimationSystem()
{
    // SCP-173 has no animations
    if ( GetPlayerClass() == PLAYER_CLASS_SCP_173 )
        return;

	// Informaci�n predeterminada
	MultiPlayerMovementData_t data;
    data.Init();

    data.m_flRunSpeed  = 190.0f;
    data.m_flWalkSpeed = 1.0f;

    if ( IsSoldier() )
    {
        data.m_flBodyYawRate = 120.0f;
    }
    else
    {
        data.m_flBodyYawRate = 160.0f;
    }

    data.m_nAimPitchPoseName = "aim_pitch";
    data.m_nAimYawPoseName = "aim_yaw";

    m_pAnimationSystem = CreatePlayerAnimationSystem( this, data );
}

//====================================================================
//====================================================================
Activity CSCP_Player::TranslateActivity( Activity actBase )
{
    // TODO
    // SCP-173 no tiene animaciones
    //if ( IsMonster() && TheMonsterPointer()->Is173() )
        //return ACT_IDLE;

    // Estados especiales
    switch ( actBase )
    {
        // Muerte
        case ACT_DIERAGDOLL:
        {
            if ( IsSurvivor() || IsSoldier() )
            {
                if ( GetPlayerStatus() == PLAYER_STATUS_DEJECTED )
                    return ACT_DIE_INCAP;

                return ACT_DIE_STANDING;
            }

            return ACT_DIESIMPLE;
            break;
        }

        // Flinch Head
        case ACT_MP_GESTURE_FLINCH_HEAD:
        {
            return ACT_FLINCH_HEAD;
            break;
        }

        // Flinch Stomach
        case ACT_MP_GESTURE_FLINCH_STOMACH:
        {
            return ACT_FLINCH_STOMACH;
            break;
        }

        case ACT_MP_GESTURE_FLINCH_LEFTARM:
        {
            return ACT_FLINCH_SHOULDER_LEFT;
            break;
        }

        case ACT_MP_GESTURE_FLINCH_RIGHTARM:
        {
            return ACT_FLINCH_SHOULDER_RIGHT;
            break;
        }

        // Flinch
        case ACT_MP_GESTURE_FLINCH_CHEST:
        case ACT_MP_GESTURE_FLINCH_LEFTLEG:
        case ACT_MP_GESTURE_FLINCH_RIGHTLEG:
        {
            if ( GetPlayerStatus() == PLAYER_STATUS_CLIMBING )
                return ACT_TERROR_FLINCH_LEDGE;

            return ACT_FLINCH;
            break;
        }
    }

    // Tenemos un arma
    if ( GetActiveWeapon() )
    {
        switch ( actBase )
        {
            case ACT_MP_IDLE_CALM:
            {
                return ACT_HL2MP_IDLE_PASSIVE;
                break;
            }

            case ACT_MP_RUN_CALM:
            {
                return ACT_HL2MP_RUN_PASSIVE;
                break;
            }

            case ACT_MP_WALK_CALM:
            {
                return ACT_HL2MP_WALK_PASSIVE;
                break;
            }
        }

        Activity weapActivity = GetActiveWeapon()->ActivityOverride( actBase, false );

        if ( weapActivity != actBase )
            return weapActivity;
    }

    // Animaciones sin arma
    switch ( actBase )
    {
        // Normal
    case ACT_MP_STAND_IDLE:
    case ACT_MP_IDLE_CALM:
    case ACT_MP_IDLE_INJURED:
        if ( IsSurvivor() || IsSoldier() )
            return ACT_HL2MP_IDLE;

        return ACT_IDLE;
        break;

        // Corriendo
    case ACT_MP_RUN:
    case ACT_MP_RUN_CALM:
    case ACT_MP_RUN_INJURED:
        if ( IsSurvivor() || IsSoldier() )
            return ACT_HL2MP_RUN;

        return ACT_RUN;
        break;

        // Caminando
    case ACT_MP_WALK:
    case ACT_MP_WALK_CALM:
    case ACT_MP_WALK_INJURED:
        if ( IsSurvivor() || IsSoldier() )
            return ACT_HL2MP_WALK;

        return ACT_WALK;
        break;

        // Agachado normal
    case ACT_MP_CROUCH_IDLE:
        if ( IsSurvivor() || IsSoldier() )
            return ACT_HL2MP_IDLE_CROUCH;

        return ACT_CROUCHIDLE;
        break;

        // Agachado saltando
    case ACT_MP_CROUCHWALK:
        if ( IsSurvivor() || IsSoldier() )
            return ACT_HL2MP_WALK_CROUCH;

        return ACT_RUN_CROUCH;
        break;

        // Saltando
    case ACT_MP_JUMP:
        if ( IsSurvivor() || IsSoldier() )
            return ACT_JUMP_RIFLE;

        return ACT_JUMP;
        break;
    }

    // TODO
    return actBase;
}

//================================================================================
// Devuelve la velocidad inicial del jugador
//================================================================================
float CSCP_Player::GetSpeed()
{
    float flSpeed = BaseClass::GetSpeed();

    if ( GetPlayerClass() == PLAYER_CLASS_SCP_173 )
    {
        flSpeed = sv_player_scp173_walk_speed.GetFloat();
    }

    return flSpeed;
}

//================================================================================
// Reproduce el sonido de movimiento del SCP
//================================================================================
void CSCP_Player::UpdateStepSound( surfacedata_t *psurface, const Vector &vecOrigin, const Vector &vecVelocity )
{
    // Solo queremos reemplazar el c�digo para SCP-173
    if ( GetPlayerClass() != PLAYER_CLASS_SCP_173 )
    {
        BaseClass::UpdateStepSound( psurface, vecOrigin, vecVelocity );
        return;
    }

    if ( !sv_footsteps.GetBool() )
        return;

#if defined( CLIENT_DLL )
    // during prediction play footstep sounds only once
    if ( prediction->InPrediction() && !prediction->IsFirstTimePredicted() )
        return;
#endif

    float speed = VectorLength( vecVelocity );

    // No se esta moviendo
    if ( speed <= 30.0f )
    {
        m_pMovementSound->Fadeout( 0.1f );
        return;
    }

    // Pitch predeterminado
    float pitch = m_pMovementSound->m_nInfo.m_nPitch;
    float volume = m_pMovementSound->m_nInfo.m_flVolume;

    volume = clamp( volume, 0.1f, 1.0f );

    if ( m_pMovementSound->IsPlaying() )
    {
        m_pMovementSound->SetPitch( pitch, 0.01f );
        m_pMovementSound->SetVolume( volume, 0.01f );
    }
    else
    {
        m_pMovementSound->Play( volume, pitch );
    }
}