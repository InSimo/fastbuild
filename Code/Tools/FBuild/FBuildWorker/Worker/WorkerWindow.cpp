// WorkerWindow
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "WorkerWindow.h"

#include "Tools/FBuild/FBuildWorker/Worker/WorkerSettings.h"
#include "Tools/FBuild/FBuildCore/FBuildVersion.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/WorkerPool/JobQueueRemote.h"

// OSUI
#include "OSUI/OSDropDown.h"
#include "OSUI/OSFont.h"
#include "OSUI/OSLabel.h"
#include "OSUI/OSEdit.h"
#include "OSUI/OSListView.h"
#include "OSUI/OSMenu.h"
#include "OSUI/OSSplitter.h"
#include "OSUI/OSTrayIcon.h"
#include "OSUI/OSWindow.h"

// Core
#include "Core/Env/Assert.h"
#include "Core/Env/Env.h"
#include "Core/Network/Network.h"
#include "Core/Strings/AString.h"
#include "Core/Strings/AStackString.h"

// system
#include <stdio.h> // for sscanf

// Defines
//------------------------------------------------------------------------------

// CONSTRUCTOR
//------------------------------------------------------------------------------
WorkerWindow::WorkerWindow()
    : OSWindow()
    , m_WantToQuit( false )
    , m_TrayIcon( nullptr )
    , m_Font( nullptr )
    , m_ModeLabel( nullptr )
    , m_ResourcesLabel( nullptr )
    , m_ThreadList( nullptr )
    , m_ModeDropDown( nullptr )
    , m_ResourcesDropDown( nullptr )
    , m_GracePeriodLabel( nullptr )
    , m_GracePeriodEdit( nullptr )
    , m_BlockingLabel( nullptr )
    , m_BlockingEdit( nullptr )
    , m_BlockingGracePeriodLabel( nullptr )
    , m_BlockingGracePeriodEdit( nullptr )
    , m_Splitter( nullptr )
    , m_Menu( nullptr )
{
    // obtain host name
    Network::GetHostName(m_HostName);

    // center the window on screen
    const uint32_t w = 700;
    const uint32_t h = 350;
    const int32_t x = (int32_t)( GetPrimaryScreenWidth() - w );
    const int32_t y = 0;

    // Create the window
    Init( x, y, w, h );

    // Create the tray icon
    AStackString<> toolTip;
    toolTip.Format( "FBuildWorker %s", FBUILD_VERSION_STRING );
    m_TrayIcon = FNEW( OSTrayIcon( this, toolTip ) );

    // listview
    m_ThreadList = FNEW( OSListView( this ) );
    #if defined( __WINDOWS__ )
        // get main window dimensions for positioning/sizing child controls
        RECT rcClient; // The parent window's client area.
        GetClientRect( (HWND)GetHandle(), &rcClient );
        m_ThreadList->Init( 0, 60, (uint32_t)( rcClient.right - rcClient.left) , (uint32_t)( ( rcClient.bottom - rcClient.top ) - 30 ) );
    #elif defined( __OSX__ )
        m_ThreadList->Init( 4, 30, w - 8, h - 38 );
    #endif
    m_ThreadList->AddColumn( "CPU", 0, 35 );
    m_ThreadList->AddColumn( "Host", 1, 100 );
    m_ThreadList->AddColumn( "Status", 2, 530 );
    size_t numWorkers = JobQueueRemote::Get().GetNumWorkers();
    m_ThreadList->SetItemCount( (uint32_t)numWorkers );
    for ( size_t i=0; i<numWorkers; ++i )
    {
        AStackString<> string;
        string.Format( "%u", (uint32_t)( i + 1 ) );
        m_ThreadList->AddItem( string.Get() );
    }

    #if defined( __WINDOWS__ )
        // font
        m_Font = FNEW( OSFont() );
        m_Font->Init( 14, "Verdana" );
    #endif

    // Mode drop down
    m_ModeDropDown = FNEW( OSDropDown( this ) );
    m_ModeDropDown->SetFont( m_Font );
    m_ModeDropDown->Init( 100, 3, 230, 200 );
    m_ModeDropDown->AddItem( "Disabled" );
    m_ModeDropDown->AddItem( "Work For Others When Idle" );
    m_ModeDropDown->AddItem( "Work For Others Always" );
    m_ModeDropDown->AddItem( "Work For Others Proportional" );
    m_ModeDropDown->SetSelectedItem( WorkerSettings::Get().GetMode() );

    // Mode label
    m_ModeLabel = FNEW( OSLabel( this ) );
    m_ModeLabel->SetFont( m_Font );
    m_ModeLabel->Init( 5, 7, 95, 15, "Current Mode:" );

    // Resources drop down
    m_ResourcesDropDown = FNEW( OSDropDown( this ) );
    m_ResourcesDropDown->SetFont( m_Font );
    m_ResourcesDropDown->Init( 380, 3, 150, 200 );
    {
        // add items
        uint32_t numProcessors = Env::GetNumProcessors();
        AStackString<> buffer;
        for ( uint32_t i=0; i<numProcessors; ++i )
        {
            float perc = ( i == ( numProcessors - 1 ) ) ? 100.0f : ( (float)( i + 1 ) / (float)numProcessors ) * 100.0f;
            buffer.Format( "%u CPUs (%2.1f%%)", ( i + 1 ), (double)perc );
            m_ResourcesDropDown->AddItem( buffer.Get() );
        }
    }
    m_ResourcesDropDown->SetSelectedItem( WorkerSettings::Get().GetNumCPUsToUse() - 1 );

    // Resources label
    m_ResourcesLabel = FNEW( OSLabel( this ) );
    m_ResourcesLabel->SetFont( m_Font );
    m_ResourcesLabel->Init( 335, 7, 45, 15, "Using:" );

    // GracePeriod edit
    static AStackString<> gracePeriodText;
    gracePeriodText.Format( "%u", WorkerSettings::Get().GetGracePeriod() );
    m_GracePeriodEdit = FNEW( OSEdit( this ) );
    m_GracePeriodEdit->SetFont( m_Font );
    m_GracePeriodEdit->Init( 650, 3, 30, 20, gracePeriodText.Get() );

    // GracePeriod label
    m_GracePeriodLabel = FNEW( OSLabel( this ) );
    m_GracePeriodLabel->SetFont( m_Font );
    m_GracePeriodLabel->Init( 535, 7, 115, 15, "Kill After (s):" );

    // Blocking edit
    static AStackString<> blockingText;
    for (const AString& s : WorkerSettings::Get().GetBlockingProcessNames())
    {
        if ( blockingText.IsEmpty() == false )
            blockingText += ',';
        blockingText += s;
    }
    m_BlockingEdit = FNEW( OSEdit( this ) );
    m_BlockingEdit->SetFont( m_Font );
    m_BlockingEdit->Init( 100, 30, 428, 20, blockingText.Get() );

    // Blocking label
    m_BlockingLabel = FNEW( OSLabel( this ) );
    m_BlockingLabel->SetFont( m_Font );
    m_BlockingLabel->Init( 5, 32, 95, 15, "Blocking Apps:" );

    // BlockingGracePeriod edit
    static AStackString<> blockingGracePeriodText;
    blockingGracePeriodText.Format( "%u", WorkerSettings::Get().GetBlockingGracePeriod() );
    m_BlockingGracePeriodEdit = FNEW( OSEdit( this ) );
    m_BlockingGracePeriodEdit->SetFont( m_Font );
    m_BlockingGracePeriodEdit->Init( 650, 30, 30, 20, blockingGracePeriodText.Get() );

    // BlockingGracePeriod label
    m_BlockingGracePeriodLabel = FNEW( OSLabel( this ) );
    m_BlockingGracePeriodLabel->SetFont( m_Font );
    m_BlockingGracePeriodLabel->Init( 535, 32, 115, 15, "Blocking Kill After:" );

    // splitter
    m_Splitter = FNEW( OSSplitter( this ) );
    m_Splitter->Init( 0, 57, w, 2u );

    // popup menu for tray icon
    m_Menu = FNEW( OSMenu( this ) );
    m_Menu->Init();
    m_Menu->AddItem( "Exit" );
    m_TrayIcon->SetMenu( m_Menu );

    #if defined( __WINDOWS__ )
        // Display the window and minimize it if needed
        if ( WorkerSettings::Get().GetStartMinimzed() )
        {
            UpdateWindow( (HWND)GetHandle() );
            ToggleMinimized(); // minimze
        }
        else
        {
            ShowWindow( (HWND)GetHandle(), SW_SHOWNOACTIVATE );
            UpdateWindow( (HWND)GetHandle() );
            ShowWindow( (HWND)GetHandle(), SW_SHOWNOACTIVATE ); // First call can be ignored
        }
    #endif

    SetStatus( "Idle" );
}

// DESTRUCTOR
//------------------------------------------------------------------------------
WorkerWindow::~WorkerWindow()
{
    // clean up UI resources
    FDELETE( m_Splitter );
    FDELETE( m_ResourcesLabel );
    FDELETE( m_ResourcesDropDown );
    FDELETE( m_ModeLabel );
    FDELETE( m_ModeDropDown );
    FDELETE( m_ThreadList );
    FDELETE( m_Menu );
    FDELETE( m_Font );
    FDELETE( m_TrayIcon );
}

// SetStatus
//------------------------------------------------------------------------------
void WorkerWindow::SetStatus( const char * statusText )
{
    AStackString< 512 > text;
    text.Format( "FBuildWorker %s | \"%s\" | %s", FBUILD_VERSION_STRING, m_HostName.Get(), statusText );
    SetTitle( text.Get() );
}

// SetWorkerState
//------------------------------------------------------------------------------
void WorkerWindow::SetWorkerState( size_t index, const AString & hostName, const AString & status )
{
    m_ThreadList->SetItemText( (uint32_t)index, 1, hostName.Get() );
    m_ThreadList->SetItemText( (uint32_t)index, 2, status.Get() );
}

// Work
//------------------------------------------------------------------------------
void WorkerWindow::Work()
{
    #if defined( __WINDOWS__ )
        // process messages until wo need to quit
        MSG msg;
        do
        {
            // any messages pending?
            if ( PeekMessage( &msg, nullptr, 0, 0, PM_NOREMOVE ) )
            {
                // message available, process it
                VERIFY( GetMessage( &msg, NULL, 0, 0 ) != 0 );
                TranslateMessage( &msg );
                DispatchMessage( &msg );
                continue; // immediately handle any new messages
            }
            else
            {
                // no message right now - prevent CPU thrashing by having a sleep
                Sleep( 100 );
            }
        } while ( m_WantToQuit == false );
    #endif

    #if defined( __OSX__ )
        PumpMessages();
    #endif
}

// OnMinimize
//------------------------------------------------------------------------------
/*virtual*/ bool WorkerWindow::OnMinimize()
{
    #if defined( __OSX__ )
        SetMinimized( true );
    #else
        // Override minimize
        ToggleMinimized();
    #endif
    return true; // Stop window minimizing (since we already handled it)
}

// OnClose
//------------------------------------------------------------------------------
/*virtual*/ bool WorkerWindow::OnClose()
{
    // Override close to minimize
    #if defined( __OSX__ )
        SetMinimized( true );
    #else
        ToggleMinimized();
    #endif

    return true; // Stop window closeing (since we already handled it)
}

// OnQuit
//------------------------------------------------------------------------------
/*virtual*/ bool WorkerWindow::OnQuit()
{
    SetWantToQuit();
    return true; // Handled
}

// OnTrayIconLeftClick
//------------------------------------------------------------------------------
/*virtual*/ bool WorkerWindow::OnTrayIconLeftClick()
{
    ToggleMinimized();
    return true; // Handled
}

// OnTrayIconRightClick
//------------------------------------------------------------------------------
/*virtual*/ bool WorkerWindow::OnTrayIconRightClick()
{
    #if defined( __WINDOWS__ )
        uint32_t index;
        if ( m_Menu->ShowAndWaitForSelection( index ) )
        {
            OnTrayIconMenuItemSelected( index );
        }
    #endif

    return true; // Handled
}

// OnDropDownSelectionChanged
//------------------------------------------------------------------------------
/*virtual*/ void WorkerWindow::OnDropDownSelectionChanged( OSDropDown * dropDown )
{
    const size_t index = dropDown->GetSelectedItem();
    if ( dropDown == m_ModeDropDown )
    {
        WorkerSettings::Get().SetMode( (WorkerSettings::Mode)index );
    }
    else if ( dropDown == m_ResourcesDropDown )
    {
        WorkerSettings::Get().SetNumCPUsToUse( (uint32_t)index + 1 );
    }
    WorkerSettings::Get().Save();
}

// OnTrayIconMenuItemSelected
//------------------------------------------------------------------------------
/*virtual*/ void WorkerWindow::OnTrayIconMenuItemSelected( uint32_t /*index*/ )
{
    // We only have one menu item right now
    SetWantToQuit();
}

// OnEditChanged
//------------------------------------------------------------------------------
/*virtual*/ void WorkerWindow::OnEditChanged( OSEdit * edit )
{
    if ( edit == nullptr )
    {
        return; // for some reasons this gets called during init with a null pointer...
    }
    if ( edit == m_BlockingEdit )
    {
        Array<AString> blocking;
        edit->GetText().Tokenize(blocking, ',');
        WorkerSettings::Get().SetBlockingProcessNames( blocking );
    }
    else if ( edit == m_BlockingGracePeriodEdit )
    {
        uint32_t val = 0;
        PRAGMA_DISABLE_PUSH_MSVC( 4996 ) // This function or variable may be unsafe...
        sscanf( edit->GetText().Get(), "%u", &val );
        PRAGMA_DISABLE_POP_MSVC // 4996
        WorkerSettings::Get().SetBlockingGracePeriod( val );
    }
    else if ( edit == m_GracePeriodEdit )
    {
        uint32_t val = 0;
        PRAGMA_DISABLE_PUSH_MSVC( 4996 ) // This function or variable may be unsafe...
        sscanf( edit->GetText().Get(), "%u", &val );
        PRAGMA_DISABLE_POP_MSVC // 4996
        WorkerSettings::Get().SetGracePeriod( val );
    }
    WorkerSettings::Get().Save();
}

// ToggleMinimized
//------------------------------------------------------------------------------
void WorkerWindow::ToggleMinimized()
{
    static bool minimized( false );
    #if defined( __WINDOWS__ )
        if ( !minimized )
        {
            // hide the main window
            ShowWindow( (HWND)GetHandle(), SW_HIDE );
        }
        else
        {
            // show the main window
            HWND hWnd = (HWND)GetHandle();
            ShowWindow( hWnd, SW_SHOW );

            // bring to front
            SetForegroundWindow( hWnd );
            SetActiveWindow( hWnd );
        }
    #elif defined( __APPLE__ )
        SetMinimized( minimized );
    #elif defined( __LINUX__ )
        // TODO:LINUX Implement WorkerWindow::Toggle
    #else
        #error Unknown Platform
    #endif
    minimized = !minimized;

    WorkerSettings::Get().SetStartMinimized( minimized );
    WorkerSettings::Get().Save();
}

//------------------------------------------------------------------------------
