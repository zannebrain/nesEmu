#include <windows.h>
#include <xaudio2.h>
#include "stdafx.h"
#include "wintendoApp.h"
#include "renderer_d3d12.h"
#include "audioEngine.h"
#include "..\wintendoCore\command.h"

#ifdef NDEBUG
#undef assert
#define assert(x) if( !(x) ) { PrintLog(#x##"\n"); }
#endif

HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

#if _DEBUG
static const uint32_t MaxSpinTime = 1;
#else
static const uint32_t MaxSpinTime = 100;
#endif

std::wstring nesFilePath( L"Games/Castlevania.nes" );

wtAppInterface_t	app;
wtRenderer			r;
wtAudioEngine		audio;
wtSystem			sys;

static const bool singleRenderThread = false; // Temp test variable

// Forward declare message handler from imgui_impl_win32.cpp
#ifdef IMGUI_ENABLE
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );
#endif

void WaitOnThread( const HANDLE& threadSemaphore, const uint32_t spinTime, const uint32_t waitMs )
{
	while ( true )
	{
		const DWORD result = WaitForSingleObject( threadSemaphore, spinTime );
		if ( ( result == WAIT_OBJECT_0 ) || ( waitMs == 0 ) ) {
			break;
		}
		Sleep( waitMs );
	}
}


void SignalThread( HANDLE threadSemaphore, const uint32_t releaseCount )
{
	ReleaseSemaphore( threadSemaphore, releaseCount, NULL );
}


DWORD WINAPI EmulatorThread( LPVOID lpParameter )
{
	wtSystem&		nesSystem		= *app.system;
	config_t&		systemConfig	= app.systemConfig;
	wtAppDebug_t&	debugData		= app.debugData;

	while( app.emulatorRunning )
	{
		const uint32_t waitFrame = app.frameIx;
		if ( app.reset )
		{
			for( uint32_t i = 0; i < FrameResultCount; ++i )
			{
				WaitOnThread( r.sync.frameSubmitReadLock[ i ], MaxSpinTime, 1 );
				WaitOnThread( r.sync.audioReadLock[ i ], MaxSpinTime, 1 );
			}

			wtSystem::InitConfig( app.systemConfig );
			nesSystem.Init( nesFilePath );
			nesSystem.SetConfig( systemConfig );
			nesSystem.GenerateRomDissambly( debugData.prgRomAsm );
			nesSystem.GenerateChrRomTables( debugData.chrRom );
			app.reset = false;
			app.pause = false;
			app.previousTime = chrono::steady_clock::now();
			app.frameIx = 0;
			r.frameResultIx = 0;
			r.lastFrameDrawn = 0;
			audio.emulatorFrame = 0;

			for ( uint32_t i = 0; i < FrameResultCount; ++i )
			{
				SignalThread( r.sync.frameSubmitReadLock[ i ], 1 );
				SignalThread( r.sync.audioReadLock[ i ], 1 );
			}

			PrintLog( "Reset" );
		}
		else
		{
			WaitOnThread( r.sync.frameSubmitReadLock[ waitFrame ], MaxSpinTime, 0 );
			WaitOnThread( r.sync.audioReadLock[ waitFrame ], MaxSpinTime, 0 );
		}

		if ( !app.pause )
		{
			nesSystem.SetConfig( systemConfig );
			if ( app.refreshChrRomTables )
			{
				nesSystem.GenerateChrRomTables( debugData.chrRom );
				app.refreshChrRomTables = false;
			}

			const timePoint_t currentTime = chrono::steady_clock::now();
			const std::chrono::nanoseconds elapsed = ( currentTime - app.previousTime );
			app.previousTime = currentTime;

			app.pause = !nesSystem.RunEpoch( elapsed );
		}

		wtFrameResult& fr = app.frameResult[ waitFrame ];
			
		app.t.copyTimer.Start();
		nesSystem.GetFrameResult( fr );
		app.t.copyTime = app.t.copyTimer.GetElapsedMs();

		app.frameIx = ( app.frameIx + 1 ) % FrameCount;

		//char s[ 128 ];
		//const uint64_t elapsedCycles = std::chrono::duration_cast<cpuCycle_t>( fr.dbgInfo.cycleEnd - fr.dbgInfo.cycleBegin ).count();
		//sprintf_s( s, "Emulator Thread: %llu, cycles=%llu, us=%i, elapsed=%i, result buffer=%i, fb-ix=%i\n", fr.currentFrame, elapsedCycles, fr.dbgInfo.frameTimeUs, fr.dbgInfo.elapsedTimeUs, waitFrame, fr.dbgFrameBufferIx );
		//PrintLog( s );

		if( singleRenderThread )
		{
			r.IssueTextureCopyCommands( waitFrame, r.currentFrameIx );
			r.SubmitFrame();
		}

		SignalThread( r.sync.frameSubmitWriteLock[ waitFrame ], 1 );
		SignalThread( r.sync.audioWriteLock[ waitFrame ], 1 );

		app.t.elapsedCopyTime = app.t.elapsedCopyTimer.GetElapsedMs();
		app.t.elapsedCopyTimer.Start();
	}

	return 0;
}


DWORD WINAPI RenderThread( LPVOID lpParameter )
{
	r.app = &app;
	r.InitD3D12();

	while ( app.emulatorRunning )
	{
		const uint32_t waitFrame = r.frameResultIx;
		WaitOnThread( r.sync.frameSubmitWriteLock[ waitFrame ], MaxSpinTime, 0 );
		wtFrameResult* fr = &app.frameResult[ waitFrame ];
		if( ( fr->currentFrame > 1 && fr->frameBuffer != nullptr ) /*&& ( fr->dbgFrameBufferIx != r.lastFrameDrawn )*/ )
		{
			//char s[128];
			//sprintf_s( s, "Render Thread: %llu, processing=%i, fb-ix=%i\n", fr->currentFrame, waitFrame, fr->dbgFrameBufferIx );
			//PrintLog( s );

			if ( !singleRenderThread )
			{
				r.IssueTextureCopyCommands( waitFrame, r.currentFrameIx );
				r.SubmitFrame();
			}
			r.lastFrameDrawn = fr->dbgFrameBufferIx;
			r.frameResultIx = ( r.frameResultIx + 1 ) % FrameResultCount;
		}
		//PrintLog( "Render Thread: Finished.\n" );
		SignalThread( r.sync.frameSubmitReadLock[ waitFrame ], 1 );
	}
	r.DestroyD3D12();

	return 0;
}


DWORD WINAPI AudioThread( LPVOID lpParameter )
{
	audio.Init();

	while ( app.emulatorRunning )
	{
		if( !audio.enableSound )
		{
			if( audio.pSourceVoice != nullptr )
			{
				audio.pSourceVoice->FlushSourceBuffers();
				audio.pSourceVoice->Stop();
				audio.audioStopped = true;
			}
			Sleep( 1000 );
		}
		else if( audio.audioStopped )
		{
			audio.pSourceVoice->Start( 0, XAUDIO2_COMMIT_ALL );
			audio.audioStopped = false;
		}

		const uint32_t waitFrame = app.frameIx;
		wtFrameResult& fr = app.frameResult[ waitFrame ];

		const bool log = app.audio->logSnd && !( waitFrame % 60 );
		if ( log ) {
			LogApu( fr );
		}

		WaitOnThread( r.sync.audioWriteLock[ waitFrame ], MaxSpinTime, 0 );
		if( fr.soundOutput != nullptr )
		{
			audio.EncodeSamples( fr.soundOutput->master );
			assert( fr.soundOutput->master.IsEmpty() );
			++audio.emulatorFrame;
		}
		audio.frameResultIx = ( audio.frameResultIx + 1 ) % FrameResultCount;

		SignalThread( r.sync.audioReadLock[ waitFrame ], 1 );
		
		if( audio.AudioSubmit() )
		{
			app.t.audioSubmitTime = app.t.audioSubmitTimer.GetElapsedMs();
			app.t.audioSubmitTimer.Start();
		}
	}

	return 0;
}


DWORD WINAPI WorkerThread( LPVOID lpParameter )
{
	wtSystem& nesSystem = *app.system;

	while( true )
	{
		wtFrameResult& fr = app.frameResult[ r.currentFrameIx ];
		if ( ( fr.dbgLog != nullptr ) && !app.logUnpacked && fr.dbgLog->IsFinished() )
		{
			app.traceLog.resize( 0 );
			app.traceLog.reserve( 400 * fr.dbgLog->GetRecordCount() ); // Assume 400 characters per log line
			fr.dbgLog->ToString( app.traceLog, 0, fr.dbgLog->GetRecordCount(), true );
			app.logUnpacked = true;

		}

		if( fr.currentFrame > 0 )
		{
			nesSystem.UpdateDebugImages();
		}
		
		Sleep( 1000 );
	}
}


int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
	{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

	using namespace std;

	unsigned int sharedData = 0;

	app.emulatorRunning	= true;
	app.pause			= false;
	app.reset			= true;
	app.r				= &r;
	app.audio			= &audio;
	app.system			= &sys;

	app.t.runTime.Start();

	const uint32_t threadCount = 4;
	DWORD threadIDs[ threadCount ];
	HANDLE threadHdls[ threadCount ];
	threadHdls[ 0 ] = CreateThread( 0, 0, EmulatorThread,	&sharedData, 0, &threadIDs[ 0 ] );
	threadHdls[ 1 ] = CreateThread( 0, 0, RenderThread,		&sharedData, 0, &threadIDs[ 1 ] );
	threadHdls[ 2 ] = CreateThread( 0, 0, AudioThread,		&sharedData, 0, &threadIDs[ 2 ] );
	threadHdls[ 3 ] = CreateThread( 0, 0, WorkerThread,		&sharedData, 0, &threadIDs[ 3 ] );

	for( uint32_t i = 0; i < threadCount; ++i )
	{
		if ( threadHdls[ i ] <= 0 )
		{
			PrintLog( "Thread creation failed.\n" );
			return 0;
		}
	}

    LoadStringW( hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING );
    LoadStringW( hInstance, IDC_WINTENDOAPP, szWindowClass, MAX_LOADSTRING );
	RegisterWindow( hInstance, szWindowClass );

    if ( !InitAppInstance ( hInstance, nCmdShow ) )
    {
		PrintLog( "Window creation failed.\n" );
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators( hInstance, MAKEINTRESOURCE( IDC_WINTENDOAPP ) );

    MSG msg;
	while ( GetMessage( &msg, nullptr, 0, 0 ) )
	{
		if ( !TranslateAccelerator( msg.hwnd, hAccelTable, &msg ) )
		{
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}
	}

	app.emulatorRunning = false;
	for ( uint32_t i = 0; i < threadCount; ++i )
	{
		DWORD exitCode;
		WaitForSingleObject( threadHdls[ i ], 1000 );
		TerminateThread( threadHdls[ i ], GetExitCodeThread( threadHdls[ i ], &exitCode ) );
		CloseHandle( threadHdls[ i ] );
	}

    return (int)msg.wParam;
}


void InitKeyBindings()
{
	wtInput* input = app.system->GetInput();

	input->BindKey( 'A', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_LEFT );
	input->BindKey( 'D', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_RIGHT );
	input->BindKey( 'W', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_UP );
	input->BindKey( 'S', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_DOWN );
	input->BindKey( 'G', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_SELECT );
	input->BindKey( 'H', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_START );
	input->BindKey( 'J', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_B );
	input->BindKey( 'K', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_A );

	input->BindKey( '1', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_LEFT );
	input->BindKey( '2', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_RIGHT );
	input->BindKey( '3', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_UP );
	input->BindKey( '4', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_DOWN );
	input->BindKey( '5', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_SELECT );
	input->BindKey( '6', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_START );
	input->BindKey( '7', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_B );
	input->BindKey( '8', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_A );
}


BOOL InitAppInstance( HINSTANCE hInstance, int nCmdShow )
{
	hInst = hInstance;

	InitKeyBindings();

	int dwStyle = ( WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX /*| WS_THICKFRAME*/ );

	RECT wr = { 0, 0, r.view.defaultWidth, r.view.defaultHeight };
	AdjustWindowRect( &wr, dwStyle, TRUE );

	r.hWnd = CreateWindowW( szWindowClass, szTitle, dwStyle,
		CW_USEDEFAULT, CW_USEDEFAULT, ( wr.right - wr.left ), ( wr.bottom - wr.top ), nullptr, nullptr, hInstance, nullptr );

	if ( !r.hWnd )
	{
		return FALSE;
	}

	ShowWindow( r.hWnd, nCmdShow );
	UpdateWindow( r.hWnd );

	return TRUE;
}


LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
#ifdef IMGUI_ENABLE
	if ( ImGui_ImplWin32_WndProcHandler( hWnd, message, wParam, lParam ) )
		return true;
#endif

	switch ( message )
	{
	case WM_CREATE:
	{
		
	}
	break;

	case WM_COMMAND:
	{
		int wmId = LOWORD( wParam );
		// Parse the menu selections:
		switch ( wmId )
		{
		case IDM_ABOUT:
			DialogBox( hInst, MAKEINTRESOURCE( IDD_ABOUTBOX ), hWnd, About );
			break;
		case ID_FILE_OPEN:
			OpenNesGame( nesFilePath );
			app.reset = true;
			app.pause = true;
			break;
		case ID_FILE_RESET:
			app.reset = true;
			break;
		case ID_FILE_LOADSTATE:
			{
				sysCmd_t traceCmd;
				traceCmd.type = sysCmdType_t::LOAD_STATE;
				app.system->SubmitCommand( traceCmd );
			}
			break;
		case ID_FILE_SAVESTATE:
			{
				sysCmd_t traceCmd;
				traceCmd.type = sysCmdType_t::SAVE_STATE;
				app.system->SubmitCommand( traceCmd );
			}
			break;
		case IDM_EXIT:
			DestroyWindow( hWnd );
			break;
		default:
			return DefWindowProc( hWnd, message, wParam, lParam );
		}
	}
	break;

	case WM_PAINT:
	{
	}
	break;

	case WM_LBUTTONDOWN:
	{
		POINT p;
		if ( GetCursorPos( &p ) )
		{
			//cursor position now in p.x and p.y
		}
		if ( ScreenToClient( hWnd, &p ) )
		{
			//p.x and p.y are now relative to hwnd's client area
		}

		RECT rc;
		GetClientRect( r.hWnd, &rc );

		const int32_t clientWidth = ( rc.right - rc.left );
		const int32_t clientHeight = ( rc.bottom - rc.top );
		const int32_t displayAreaX = r.view.displayScalar * r.view.nesWidth;
		const int32_t displayAreaY = clientHeight; // height minus title bar

		if( ( p.x >= 0 ) && ( p.x < displayAreaX ) && (p.y >= 0 ) && ( p.y < displayAreaY ) )
		{
			p.x /= r.view.displayScalar;
			p.y /= r.view.displayScalar;
			app.system->GetInput()->StoreMouseClick( wtPoint{ p.x, p.y } );
		}
	}
	break;

	case WM_LBUTTONUP:
	{
	//	ClearMouseClick();
	}
	break;

	case WM_SIZE:
	{
		InvalidateRect( hWnd, NULL, FALSE );
	}
	break;

	case WM_KEYDOWN:
	{
		const uint32_t capKey = toupper( (int)wParam );
		if( capKey == 'T' )	{
			app.pause = true;
		}

		app.system->GetInput()->StoreKey( capKey );
	}
	break;

	case WM_KEYUP:
	{
		const uint32_t capKey = toupper( (int)wParam );
		if ( capKey == 'T' ) {
			app.pause = false;
		}

		app.system->GetInput()->ReleaseKey( capKey );
	}
	break;

	case WM_DESTROY:
	{
		PostQuitMessage( 0 );
	}
	break;

	default:
		return DefWindowProc( hWnd, message, wParam, lParam );
	}
	return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam )
{
    UNREFERENCED_PARAMETER(lParam);
    switch ( message )
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if ( LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL )
        {
            EndDialog( hDlg, LOWORD(wParam) );
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}