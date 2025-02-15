//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Implements all the functions exported by the GameUI dll
//
// $NoKeywords: $
//===========================================================================//

#if !defined( _X360 )
#include <windows.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <io.h>
#include <tier0/dbg.h>
#include <direct.h>

#ifdef SendMessage
#undef SendMessage
#endif
																
#include "FileSystem.h"
#include "GameUI_Interface.h"
#include "Sys_Utils.h"
#include "string.h"
#include "tier0/icommandline.h"

// interface to engine
#include "EngineInterface.h"

#include "VGuiSystemModuleLoader.h"
#include "bitmap/TGALoader.h"

#include "GameConsole.h"
#include "LoadingDialog.h"
#include "CDKeyEntryDialog.h"
#include "ModInfo.h"
#include "game/client/IGameClientExports.h"
#include "materialsystem/imaterialsystem.h"
#include "matchmaking/imatchframework.h"
#include "ixboxsystem.h"
#include "iachievementmgr.h"
#include "IGameUIFuncs.h"
#include "IEngineVGUI.h"

// vgui2 interface
// note that GameUI project uses ..\vgui2\include, not ..\utils\vgui\include
#include "vgui/Cursor.h"
#include "tier1/KeyValues.h"
#include "vgui/ILocalize.h"
#include "vgui/IPanel.h"
#include "vgui/IScheme.h"
#include "vgui/IVGui.h"
#include "vgui/ISystem.h"
#include "vgui/ISurface.h"
#include "vgui_controls/Menu.h"
#include "vgui_controls/PHandle.h"
#include "tier3/tier3.h"
#include "matsys_controls/matsyscontrols.h"
#include "steam/steam_api.h"
#include "protocol.h"

#if defined( GAMEUI_CEF )

    #include "in/cef_basepanel.h"
    #include "in/cef_gameui.h"
    //#include "in/cef_thread.h"
    typedef CWebGameUI GAME_UI_PANEL;
    inline GAME_UI_PANEL & GetGameUiPanel() { return *TheGameUiPanel; }
    inline GAME_UI_PANEL & ConstructGameUIPanel() { return * new GAME_UI_PANEL(); }
    class IMatchExtSwarm *g_pMatchExtSwarm = NULL;

#elif defined( GAMEUI_AWESOMIUM )

    #include "in/awe_gameuiwebpanel.h"
    typedef CWebGameUI GAME_UI_PANEL;
    inline GAME_UI_PANEL & GetGameUiPanel() { return *TheGameUIPanel; }
    inline GAME_UI_PANEL & ConstructGameUIPanel() { return * new GAME_UI_PANEL(); }
    class IMatchExtSwarm *g_pMatchExtSwarm = NULL;

#elif defined(GAMEUI_ENGINE)

#include "sdk/basemodpanel.h"
#include "sdk/basemodui.h"
typedef BaseModUI::CBaseModPanel GAME_UI_PANEL;
inline GAME_UI_PANEL & GetGameUiPanel() {
	return GAME_UI_PANEL::GetSingleton();
}
inline GAME_UI_PANEL & ConstructGameUIPanel() {
	return *new GAME_UI_PANEL();
}
class IMatchExtSwarm *g_pMatchExtSwarm = NULL;

#else

#include "vgui_basepanel.h"
typedef CBasePanel UI_BASEMOD_PANEL_CLASS;
inline UI_BASEMOD_PANEL_CLASS & GetUiBaseModPanelClass() {
	return *BasePanel();
}
inline UI_BASEMOD_PANEL_CLASS & ConstructUiBaseModPanelClass() {
	return *BasePanelSingleton();
}

#endif

#ifdef _X360
    #include "xbox/xbox_win32stubs.h"
#endif // _X360

#include "tier0/dbg.h"
#include "engine/IEngineSound.h"
#include "gameui_util.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IEngineVGui *enginevguifuncs = NULL;

#ifdef _X360
IXOnline  *xonline = NULL;			// 360 only
#endif

vgui::ISurface *enginesurfacefuncs = NULL;
IAchievementMgr *achievementmgr = NULL;

class CGameUI;
CGameUI *g_pGameUI = NULL;

#ifdef GAMEUI_ENGINE
class CLoadingDialog;
vgui::DHANDLE<CLoadingDialog> g_hLoadingDialog;
vgui::VPANEL g_hLoadingBackgroundDialog = NULL;
#endif

static CGameUI g_GameUI;
static WHANDLE g_hMutex = NULL;
static WHANDLE g_hWaitMutex = NULL;

static IGameClientExports *g_pGameClientExports = NULL;

IGameClientExports *GameClientExports()
{
	return g_pGameClientExports;
}

//-----------------------------------------------------------------------------
// Purpose: singleton accessor
//-----------------------------------------------------------------------------
CGameUI &GameUI()
{
	return g_GameUI;
}

//-----------------------------------------------------------------------------
// Purpose: hack function to give the module loader access to the main panel handle
//			only used in VguiSystemModuleLoader
//-----------------------------------------------------------------------------
vgui::VPANEL GetGameUIBasePanel()
{
	return GetGameUiPanel().GetVPanel();
}

EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CGameUI, IGameUI, GAMEUI_INTERFACE_VERSION, g_GameUI);

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CGameUI::CGameUI()
{
	g_pGameUI = this;
	m_bTryingToLoadFriends = false;
	m_iFriendsLoadPauseFrames = 0;
	m_iGameIP = 0;
	m_iGameConnectionPort = 0;
	m_iGameQueryPort = 0;
	m_bActivatedUI = false;
	m_szPreviousStatusText[0] = 0;
	m_bIsConsoleUI = false;
	m_bHasSavedThisMenuSession = false;
	m_bOpenProgressOnStart = false;
	m_iPlayGameStartupSound = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CGameUI::~CGameUI()
{
	g_pGameUI = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Initialization
//-----------------------------------------------------------------------------
void CGameUI::Initialize( CreateInterfaceFn factory )
{
	MEM_ALLOC_CREDIT();
	ConnectTier1Libraries( &factory, 1 );
	ConnectTier2Libraries( &factory, 1 );
	ConVar_Register( FCVAR_CLIENTDLL );
	ConnectTier3Libraries( &factory, 1 );

	enginesound = (IEngineSound *)factory(IENGINESOUND_CLIENT_INTERFACE_VERSION, NULL);
	engine = (IVEngineClient *)factory( VENGINE_CLIENT_INTERFACE_VERSION, NULL );
	bik = (IBik*)factory( BIK_INTERFACE_VERSION, NULL );

#ifndef _X360
#ifndef NO_STEAM
	SteamAPI_InitSafe();
	steamapicontext->Init();
#endif
#endif

	CGameUIConVarRef var( "gameui_xbox" );
	m_bIsConsoleUI = var.IsValid() && var.GetBool();

	vgui::VGui_InitInterfacesList( "GameUI", &factory, 1 );
	vgui::VGui_InitMatSysInterfacesList( "GameUI", &factory, 1 );

	// load localization file
	g_pVGuiLocalize->AddFile( "Resource/gameui_%language%.txt", "GAME", true );

	// load mod info
	ModInfo().LoadCurrentGameInfo();

	// load localization file for kb_act.lst
	g_pVGuiLocalize->AddFile( "Resource/valve_%language%.txt", "GAME", true );

	bool bFailed = false;
	enginevguifuncs = (IEngineVGui *)factory( VENGINE_VGUI_VERSION, NULL );
	enginesurfacefuncs = (vgui::ISurface *)factory(VGUI_SURFACE_INTERFACE_VERSION, NULL);
	gameuifuncs = (IGameUIFuncs *)factory( VENGINE_GAMEUIFUNCS_VERSION, NULL );
	xboxsystem = (IXboxSystem *)factory( XBOXSYSTEM_INTERFACE_VERSION, NULL );
#ifdef _X360
	xonline = (IXOnline *)factory( XONLINE_INTERFACE_VERSION, NULL ); 
#endif
#ifdef SWARM_DLL
    g_pMatchExtSwarm = (IMatchExtSwarm *)factory( IMATCHEXT_SWARM_INTERFACE, NULL );
#elif GAMEUI_ENGINE
	g_pMatchExtSwarm = ( IMatchExtSwarm * ) factory( IMATCHEXT_SWARM_INTERFACE, NULL );
#endif
	bFailed = !enginesurfacefuncs || !gameuifuncs || !enginevguifuncs ||
		!xboxsystem ||
#ifdef _X360
		!xonline ||
#endif
#ifdef SWARM_DLL
		!g_pMatchExtSwarm ||
#endif
#ifdef GAMEUI_ENGINE
		!g_pMatchExtSwarm ||
#endif
		!g_pMatchFramework;
	if ( bFailed )
	{
		Error( "CGameUI::Initialize() failed to get necessary interfaces\n" );
	}

#ifdef GAMEUI_CEF
    // Creamos el subproceso que se encargara de CEF
    //m_nCefThread = CreateSimpleThread( CefThread::ExecThread, NULL );

    // Esperamos unos segundos...
    //ThreadSleep( 10000.0f );    

    {
        DevMsg( "Starting Chromium Embedded Framework...\n" );

        // Creamos un nuevo objeto para controlar la aplicaci�n
        CefRefPtr<CCefApplication> app( new CCefApplication() );

        CefMainArgs args;
        int cef_result = CefExecuteProcess( args, app.get(), NULL );

        // 
        if ( cef_result >= 0 )
        {
            Error( "CefThread::Start() failed to execute CEF process.\n" );
            return;
        }

        const char *userAgent = "InSource - Chromium Embedded Framework";

        CefSettings cef_settings;
        cef_settings.single_process = true;
        cef_settings.no_sandbox = true;
        cef_settings.multi_threaded_message_loop = false;
        cef_settings.windowless_rendering_enabled = true;
        cef_settings.command_line_args_disabled = true;
        cef_settings.ignore_certificate_errors = true;

        CefString( &cef_settings.locale ).FromASCII( "es" );

        if ( IsDebug() )
        {
            cef_settings.log_severity = LOGSEVERITY_WARNING;
            cef_settings.remote_debugging_port = 1337;
        }
        else
        {
            CefString( &cef_settings.user_agent ).FromASCII( userAgent );
        }

        bool cef_init = CefInitialize( args, cef_settings, app.get(), NULL );

        if ( !cef_init )
        {
            Error( "CefThread::Start() failed to initialize CEF.\n" );
            return;
        }

        DevMsg( "Chromium Embedded Framework ready.\n" );
    }
#endif

    // Obtenemos el tama�o de la pantalla
    int wide, tall;
    vgui::surface()->GetScreenSize( wide, tall );

	// Creamos el panel para el men�
	GAME_UI_PANEL &GameUIBasePanel = ConstructGameUIPanel();

    // Configuramos
	GameUIBasePanel.SetBounds( 0, 0, wide, tall );
	GameUIBasePanel.SetPaintBorderEnabled( false );
	GameUIBasePanel.SetPaintBackgroundEnabled( true );
	GameUIBasePanel.SetPaintEnabled( true );
	GameUIBasePanel.SetVisible( true );
    GameUIBasePanel.SetEnabled( true );
	GameUIBasePanel.SetMouseInputEnabled( IsPC() );
	GameUIBasePanel.SetKeyBoardInputEnabled( true );

#ifdef GAMEUI_AWESOMIUM
    GameUIBasePanel.Init();
#elif GAMEUI_CEF
    GameUIBasePanel.Init();
#else
    vgui::VPANEL rootpanel = enginevguifuncs->GetPanel( PANEL_GAMEUIDLL );
    GameUIBasePanel.SetParent( rootpanel );
#endif
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CGameUI::PostInit()
{
	if ( IsX360() )
	{
		enginesound->PrecacheSound( "UI/buttonrollover.wav", true, true );
		enginesound->PrecacheSound( "UI/buttonclick.wav", true, true );
		enginesound->PrecacheSound( "UI/buttonclickrelease.wav", true, true );
		enginesound->PrecacheSound( "player/suit_denydevice.wav", true, true );

		enginesound->PrecacheSound( "UI/menu_accept.wav", true, true );
		enginesound->PrecacheSound( "UI/menu_focus.wav", true, true );
		enginesound->PrecacheSound( "UI/menu_invalid.wav", true, true );
		enginesound->PrecacheSound( "UI/menu_back.wav", true, true );
		enginesound->PrecacheSound( "UI/menu_countdown.wav", true, true );
	}

#ifdef SDK_CLIENT_DLL
	// to know once client dlls have been loaded
	BaseModUI::CUIGameData::Get()->OnGameUIPostInit();
#endif
}

//-----------------------------------------------------------------------------
// Purpose: connects to client interfaces
//-----------------------------------------------------------------------------
void CGameUI::Connect( CreateInterfaceFn gameFactory )
{
    g_pGameClientExports = (IGameClientExports *)gameFactory( GAMECLIENTEXPORTS_INTERFACE_VERSION, NULL );

    achievementmgr = engine->GetAchievementMgr();

    if ( !g_pGameClientExports )
    {
        Error( "CGameUI::Initialize() failed to get necessary interfaces\n" );
    }

    m_GameFactory = gameFactory;
}

//-----------------------------------------------------------------------------
// Purpose: Callback function; sends platform Shutdown message to specified window
//-----------------------------------------------------------------------------
int __stdcall SendShutdownMsgFunc(WHANDLE hwnd, int lparam)
{
	Sys_PostMessage(hwnd, Sys_RegisterWindowMessage("ShutdownValvePlatform"), 0, 1);
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: Searches for GameStartup*.mp3 files in the sound/ui folder and plays one
//-----------------------------------------------------------------------------
void CGameUI::PlayGameStartupSound()
{
    return;

	if ( IsX360() )
		return;

	if ( CommandLine()->FindParm( "-nostartupsound" ) )
		return;

	FileFindHandle_t fh;

	CUtlVector<char *> fileNames;

	char path[ 512 ];
	Q_snprintf( path, sizeof( path ), "sound/ui/gamestartup*.mp3" );
	Q_FixSlashes( path );

	char const *fn = g_pFullFileSystem->FindFirstEx( path, "MOD", &fh );
	if ( fn )
	{
		do
		{
			char ext[ 10 ];
			Q_ExtractFileExtension( fn, ext, sizeof( ext ) );

			if ( !Q_stricmp( ext, "mp3" ) )
			{
				char temp[ 512 ];
				Q_snprintf( temp, sizeof( temp ), "ui/%s", fn );

				char *found = new char[ strlen( temp ) + 1 ];
				Q_strncpy( found, temp, strlen( temp ) + 1 );

				Q_FixSlashes( found );
				fileNames.AddToTail( found );
			}
	
			fn = g_pFullFileSystem->FindNext( fh );

		} while ( fn );

		g_pFullFileSystem->FindClose( fh );
	}

	// did we find any?
	if ( fileNames.Count() > 0 )
	{
		SYSTEMTIME SystemTime;
		GetSystemTime( &SystemTime );
		int index = SystemTime.wMilliseconds % fileNames.Count();

		if ( fileNames.IsValidIndex( index ) && fileNames[index] )
		{
			char found[ 512 ];

			// escape chars "*#" make it stream, and be affected by snd_musicvolume
			Q_snprintf( found, sizeof( found ), "play *#%s", fileNames[index] );

			engine->ClientCmd_Unrestricted( found );
		}

		fileNames.PurgeAndDeleteElements();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called to setup the game UI
//-----------------------------------------------------------------------------
void CGameUI::Start()
{
	// determine Steam location for configuration
	if ( !FindPlatformDirectory( m_szPlatformDir, sizeof( m_szPlatformDir ) ) )
		return;

	if ( IsPC() )
	{
		// setup config file directory
		char szConfigDir[512];
		Q_strncpy( szConfigDir, m_szPlatformDir, sizeof( szConfigDir ) );
		Q_strncat( szConfigDir, "config", sizeof( szConfigDir ), COPY_ALL_CHARACTERS );

		Msg( "Steam config directory: %s\n", szConfigDir );

		g_pFullFileSystem->AddSearchPath(szConfigDir, "CONFIG");
		g_pFullFileSystem->CreateDirHierarchy("", "CONFIG");

		// user dialog configuration
		vgui::system()->SetUserConfigFile("InGameDialogConfig.vdf", "CONFIG");

		g_pFullFileSystem->AddSearchPath( "platform", "PLATFORM" );
	}

	// localization
	g_pVGuiLocalize->AddFile( "Resource/platform_%language%.txt");
	g_pVGuiLocalize->AddFile( "Resource/vgui_%language%.txt");

	Sys_SetLastError( SYS_NO_ERROR );

	if ( IsPC() )
	{
		g_hMutex = Sys_CreateMutex( "ValvePlatformUIMutex" );
		g_hWaitMutex = Sys_CreateMutex( "ValvePlatformWaitMutex" );
		if ( g_hMutex == NULL || g_hWaitMutex == NULL || Sys_GetLastError() == SYS_ERROR_INVALID_HANDLE )
		{
			// error, can't get handle to mutex
			if (g_hMutex)
			{
				Sys_ReleaseMutex(g_hMutex);
			}
			if (g_hWaitMutex)
			{
				Sys_ReleaseMutex(g_hWaitMutex);
			}
			g_hMutex = NULL;
			g_hWaitMutex = NULL;
			Error("Steam Error: Could not access Steam, bad mutex\n");
			return;
		}
		unsigned int waitResult = Sys_WaitForSingleObject(g_hMutex, 0);
		if (!(waitResult == SYS_WAIT_OBJECT_0 || waitResult == SYS_WAIT_ABANDONED))
		{
			// mutex locked, need to deactivate Steam (so we have the Friends/ServerBrowser data files)
			// get the wait mutex, so that Steam.exe knows that we're trying to acquire ValveTrackerMutex
			waitResult = Sys_WaitForSingleObject(g_hWaitMutex, 0);
			if (waitResult == SYS_WAIT_OBJECT_0 || waitResult == SYS_WAIT_ABANDONED)
			{
				Sys_EnumWindows(SendShutdownMsgFunc, 1);
			}
		}

		// Delay playing the startup music until two frames
		// this allows cbuf commands that occur on the first frame that may start a map
		m_iPlayGameStartupSound = 2;

		// now we are set up to check every frame to see if we can friends/server browser
		m_bTryingToLoadFriends = true;
		m_iFriendsLoadPauseFrames = 1;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called to Shutdown the game UI system
//-----------------------------------------------------------------------------
void CGameUI::Shutdown()
{
#ifdef GAMEUI_CEF
    // Liberamos el hilo
    if ( m_nCefThread )
    {
        ReleaseThreadHandle( m_nCefThread );
        m_nCefThread = NULL;
    }

    // Apagamos CEF
    CefShutdown();
#endif

    // notify all the modules of Shutdown
    g_VModuleLoader.ShutdownPlatformModules();

    // unload the modules them from memory
    g_VModuleLoader.UnloadPlatformModules();

    ModInfo().FreeModInfo();

    // release platform mutex
    // close the mutex
    if ( g_hMutex )
    {
        Sys_ReleaseMutex( g_hMutex );
    }
    if ( g_hWaitMutex )
    {
        Sys_ReleaseMutex( g_hWaitMutex );
    }

#ifndef NO_STEAM
    steamapicontext->Clear();
#ifndef _X360
    // SteamAPI_Shutdown(); << Steam shutdown is controlled by engine
#endif
#endif

    ConVar_Unregister();
    DisconnectTier3Libraries();
    DisconnectTier2Libraries();
    DisconnectTier1Libraries();
}

//-----------------------------------------------------------------------------
// Purpose: paints all the vgui elements
//-----------------------------------------------------------------------------
void CGameUI::RunFrame()
{
#ifdef GAMEUI_CEF
    CefDoMessageLoopWork();
#endif

    int wide, tall;
#if defined( TOOLFRAMEWORK_VGUI_REFACTOR )
    // resize the background panel to the screen size
    vgui::VPANEL clientDllPanel = enginevguifuncs->GetPanel( PANEL_ROOT );

    int x, y;
    vgui::ipanel()->GetPos( clientDllPanel, x, y );
    vgui::ipanel()->GetSize( clientDllPanel, wide, tall );
    staticPanel->SetBounds( x, y, wide, tall );
#else
    vgui::surface()->GetScreenSize( wide, tall );
    GetGameUiPanel().SetSize( wide, tall );
#endif

    // Run frames
    g_VModuleLoader.RunFrame();
    GetGameUiPanel().RunFrame();

    // Play the start-up music the first time we run frame
    if ( IsPC() && m_iPlayGameStartupSound > 0 )
    {
        m_iPlayGameStartupSound--;
        if ( !m_iPlayGameStartupSound )
        {
            PlayGameStartupSound();
        }
    }

    if ( IsPC() && m_bTryingToLoadFriends && m_iFriendsLoadPauseFrames-- < 1 && g_hMutex && g_hWaitMutex )
    {
        // try and load Steam platform files
        unsigned int waitResult = Sys_WaitForSingleObject( g_hMutex, 0 );
        if ( waitResult == SYS_WAIT_OBJECT_0 || waitResult == SYS_WAIT_ABANDONED )
        {
            // we got the mutex, so load Friends/Serverbrowser
            // clear the loading flag
            m_bTryingToLoadFriends = false;
            g_VModuleLoader.LoadPlatformModules( &m_GameFactory, 1, false );

            // release the wait mutex
            Sys_ReleaseMutex( g_hWaitMutex );

            // notify the game of our game name
            const char *fullGamePath = engine->GetGameDirectory();
            const char *pathSep = strrchr( fullGamePath, '/' );
            if ( !pathSep )
            {
                pathSep = strrchr( fullGamePath, '\\' );
            }
            if ( pathSep )
            {
                KeyValues *pKV = new KeyValues( "ActiveGameName" );
                pKV->SetString( "name", pathSep + 1 );
                pKV->SetInt( "appid", engine->GetAppID() );
                KeyValues *modinfo = new KeyValues( "ModInfo" );
                if ( modinfo->LoadFromFile( g_pFullFileSystem, "gameinfo.txt" ) )
                {
                    pKV->SetString( "game", modinfo->GetString( "game", "" ) );
                }
                modinfo->deleteThis();

                g_VModuleLoader.PostMessageToAllModules( pKV );
            }

            // notify the ui of a game connect if we're already in a game
            if ( m_iGameIP )
            {
                SendConnectedToGameMessage();
            }
        }
    }
}

//-----------------------------------------------------------------------------
// Purpose: Activate the game UI
//-----------------------------------------------------------------------------
void CGameUI::OnGameUIActivated()
{
    bool bWasActive = m_bActivatedUI;
    m_bActivatedUI = true;

    // Lock the UI to a particular player
    if ( !bWasActive )
    {
        SetGameUIActiveSplitScreenPlayerSlot( engine->GetActiveSplitScreenPlayerSlot() );
    }

    // pause the server in case it is pausable
    engine->ClientCmd_Unrestricted( "setpause nomsg" );

    SetSavedThisMenuSession( false );

    bool bNeedActivation = true;

    if ( GetGameUiPanel().IsVisible() )
    {
        // Already visible, maybe don't need activation
        if ( !IsInLevel() && IsInBackgroundLevel() )
            bNeedActivation = false;
    }
    if ( bNeedActivation )
    {
        GetGameUiPanel().OnGameUIActivated();
    }
}

//-----------------------------------------------------------------------------
// Purpose: Hides the game ui, in whatever state it's in
//-----------------------------------------------------------------------------
void CGameUI::OnGameUIHidden()
{
    bool bWasActive = m_bActivatedUI;
    m_bActivatedUI = false;

    // unpause the game when leaving the UI
    engine->ClientCmd_Unrestricted( "unpause nomsg" );

    GetGameUiPanel().OnGameUIHidden();

    // Restore to default
    if ( bWasActive )
    {
        SetGameUIActiveSplitScreenPlayerSlot( 0 );
    }
}

//-----------------------------------------------------------------------------
// Purpose: Called when the game connects to a server
//-----------------------------------------------------------------------------
void CGameUI::OLD_OnConnectToServer( const char *game, int IP, int port )
{
    // Nobody should use this anymore because the query port and the connection port can be different.
    // Use OnConnectToServer2 instead.
    Assert( false );
    OnConnectToServer2( game, IP, port, port );
}

//-----------------------------------------------------------------------------
// Purpose: activates the loading dialog on level load start
//-----------------------------------------------------------------------------
void CGameUI::OnLevelLoadingStarted( const char *levelName, bool bShowProgressDialog )
{
    g_VModuleLoader.PostMessageToAllModules( new KeyValues( "LoadingStarted" ) );
    GetGameUiPanel().OnLevelLoadingStarted( levelName, bShowProgressDialog );

    // Don't play the start game sound if this happens before we get to the first frame
    m_iPlayGameStartupSound = 0;
}

//-----------------------------------------------------------------------------
// Purpose: closes any level load dialog
//-----------------------------------------------------------------------------
void CGameUI::OnLevelLoadingFinished( bool bError, const char *failureReason, const char *extendedReason )
{
    // notify all the modules
    g_VModuleLoader.PostMessageToAllModules( new KeyValues( "LoadingFinished" ) );

#ifdef GAMEUI_ENGINE
	GetGameUiPanel().OnLevelLoadingFinished( new KeyValues( "LoadingFinished" ) );
#else
    GetGameUiPanel().OnLevelLoadingFinished( bError, failureReason, extendedReason );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Updates progress bar
// Output : Returns true if screen should be redrawn
//-----------------------------------------------------------------------------
bool CGameUI::UpdateProgressBar( float progress, const char *statusText )
{
    return GetGameUiPanel().UpdateProgressBar( progress, statusText );
}

//-----------------------------------------------------------------------------
// Purpose: Returns prev settings
//-----------------------------------------------------------------------------
bool CGameUI::SetShowProgressText( bool show )
{
    return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CGameUI::SetProgressLevelName( const char *levelName )
{
}

//-----------------------------------------------------------------------------
// Purpose: Sets the specified panel as the background panel for the loading
//		dialog.  If NULL, default background is used.  If you set a panel,
//		it should be full-screen with an opaque background, and must be a VGUI popup.
//-----------------------------------------------------------------------------
void CGameUI::SetLoadingBackgroundDialog( vgui::VPANEL panel )
{
}

//-----------------------------------------------------------------------------
// Purpose: Called when the game connects to a server
//-----------------------------------------------------------------------------
void CGameUI::OnConnectToServer2( const char *game, int IP, int connectionPort, int queryPort )
{
    m_iGameIP = IP;
    m_iGameConnectionPort = connectionPort;
    m_iGameQueryPort = queryPort;

    SendConnectedToGameMessage();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CGameUI::SetProgressOnStart()
{
    m_bOpenProgressOnStart = true;
}

//-----------------------------------------------------------------------------
// Purpose: Called when the game disconnects from a server
//-----------------------------------------------------------------------------
void CGameUI::OnDisconnectFromServer( uint8 eSteamLoginFailure )
{
    m_iGameIP = 0;
    m_iGameConnectionPort = 0;
    m_iGameQueryPort = 0;

    g_VModuleLoader.PostMessageToAllModules( new KeyValues( "DisconnectedFromGame" ) );

#ifndef GAMEUI_ENGINE
    GetGameUiPanel().OnDisconnectFromServer( eSteamLoginFailure );
#endif

#ifdef GAMEUI_ENGINE
    if ( eSteamLoginFailure == STEAMLOGINFAILURE_NOSTEAMLOGIN )
    {
        if ( g_hLoadingDialog )
        {
            g_hLoadingDialog->DisplayNoSteamConnectionError();
        }
    }
    else if ( eSteamLoginFailure == STEAMLOGINFAILURE_VACBANNED )
    {
        if ( g_hLoadingDialog )
        {
            g_hLoadingDialog->DisplayVACBannedError();
        }
    }
    else if ( eSteamLoginFailure == STEAMLOGINFAILURE_LOGGED_IN_ELSEWHERE )
    {
        if ( g_hLoadingDialog )
        {
            g_hLoadingDialog->DisplayLoggedInElsewhereError();
        }
    }
#endif
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CGameUI::NeedConnectionProblemWaitScreen()
{
#ifdef SDK_CLIENT_DLL
    BaseModUI::CUIGameData::Get()->NeedConnectionProblemWaitScreen();
#endif
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CGameUI::ShowPasswordUI( char const *pchCurrentPW )
{
#ifdef SDK_CLIENT_DLL
    BaseModUI::CUIGameData::Get()->ShowPasswordUI( pchCurrentPW );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Validates the user has a cdkey in the registry
//-----------------------------------------------------------------------------
void CGameUI::ValidateCDKey()
{
    Assert( false );
}

//-----------------------------------------------------------------------------
// Purpose: Finds which directory the platform resides in
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CGameUI::FindPlatformDirectory(char *platformDir, int bufferSize)
{
	platformDir[0] = '\0';

	if ( platformDir[0] == '\0' )
	{
		// we're not under steam, so setup using path relative to game
		if ( IsPC() )
		{
			if ( ::GetModuleFileName( ( HINSTANCE )GetModuleHandle( NULL ), platformDir, bufferSize ) )
			{
				char *lastslash = strrchr(platformDir, '\\'); // this should be just before the filename
				if ( lastslash )
				{
					*lastslash = 0;
					Q_strncat(platformDir, "\\platform\\", bufferSize, COPY_ALL_CHARACTERS );
					return true;
				}
			}
		}
		else
		{
			// xbox fetches the platform path from exisiting platform search path
			// path to executeable is not correct for xbox remote configuration
			if ( g_pFullFileSystem->GetSearchPath( "PLATFORM", false, platformDir, bufferSize ) )
			{
				char *pSeperator = strchr( platformDir, ';' );
				if ( pSeperator )
					*pSeperator = '\0';
				return true;
			}
		}

		Error( "Unable to determine platform directory\n" );
		return false;
	}

	return (platformDir[0] != 0);
}

//-----------------------------------------------------------------------------
// Purpose: just wraps an engine call to activate the gameUI
//-----------------------------------------------------------------------------
void CGameUI::ActivateGameUI()
{
	engine->ExecuteClientCmd("gameui_activate");

	// Lock the UI to a particular player
	SetGameUIActiveSplitScreenPlayerSlot( engine->GetActiveSplitScreenPlayerSlot() );
}

//-----------------------------------------------------------------------------
// Purpose: just wraps an engine call to hide the gameUI
//-----------------------------------------------------------------------------
void CGameUI::HideGameUI()
{
	engine->ExecuteClientCmd("gameui_hide");
}

//-----------------------------------------------------------------------------
// Purpose: Toggle allowing the engine to hide the game UI with the escape key
//-----------------------------------------------------------------------------
void CGameUI::PreventEngineHideGameUI()
{
	engine->ExecuteClientCmd("gameui_preventescape");
}

//-----------------------------------------------------------------------------
// Purpose: Toggle allowing the engine to hide the game UI with the escape key
//-----------------------------------------------------------------------------
void CGameUI::AllowEngineHideGameUI()
{
	engine->ExecuteClientCmd("gameui_allowescape");
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CGameUI::SendConnectedToGameMessage()
{
    MEM_ALLOC_CREDIT();
    KeyValues *kv = new KeyValues( "ConnectedToGame" );
    kv->SetInt( "ip", m_iGameIP );
    kv->SetInt( "connectionport", m_iGameConnectionPort );
    kv->SetInt( "queryport", m_iGameQueryPort );

    g_VModuleLoader.PostMessageToAllModules( kv );
}

//-----------------------------------------------------------------------------
// Purpose: returns true if we're currently playing the game
//-----------------------------------------------------------------------------
bool CGameUI::IsInLevel()
{
	const char *levelName = engine->GetLevelName();
	if (levelName && levelName[0] && !engine->IsLevelMainMenuBackground())
	{
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: returns true if we're at the main menu and a background level is loaded
//-----------------------------------------------------------------------------
bool CGameUI::IsInBackgroundLevel()
{
	const char *levelName = engine->GetLevelName();
	if (levelName && levelName[0] && engine->IsLevelMainMenuBackground())
	{
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: returns true if we're in a multiplayer game
//-----------------------------------------------------------------------------
bool CGameUI::IsInMultiplayer()
{
	return (IsInLevel() && engine->GetMaxClients() > 1);
}

//-----------------------------------------------------------------------------
// Purpose: returns true if we're console ui
//-----------------------------------------------------------------------------
bool CGameUI::IsConsoleUI()
{
	return m_bIsConsoleUI;
}

//-----------------------------------------------------------------------------
// Purpose: returns true if we've saved without closing the menu
//-----------------------------------------------------------------------------
bool CGameUI::HasSavedThisMenuSession()
{
	return m_bHasSavedThisMenuSession;
}

void CGameUI::SetSavedThisMenuSession( bool bState )
{
	m_bHasSavedThisMenuSession = bState;
}

#if defined( _X360 ) && defined( _DEMO )
void CGameUI::OnDemoTimeout()
{
	GetGameUiPanel().OnDemoTimeout();
}
#endif

