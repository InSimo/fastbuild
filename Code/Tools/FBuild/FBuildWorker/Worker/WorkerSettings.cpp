// WorkerSettings
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "WorkerSettings.h"

// Core
#include "Core/Env/Env.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Strings/AStackString.h"

// system
#if defined( __WINDOWS__ )
    #include "Core/Env/WindowsHeader.h"
#endif

// Other
//------------------------------------------------------------------------------
#define FBUILDWORKER_SETTINGS_MIN_VERSION ( 1 )     // Oldest compatible version
#define FBUILDWORKER_SETTINGS_CURRENT_VERSION ( 4 ) // Current version

// CONSTRUCTOR
//------------------------------------------------------------------------------
WorkerSettings::WorkerSettings()
    : m_Mode( WHEN_IDLE )
    , m_NumCPUsToUse( 1 )
    , m_StartMinimized( false )
    , m_GracePeriod( 0 )
    , m_BlockingGracePeriod( 30 )
{
    // half CPUs available to use by default
    uint32_t numCPUs = Env::GetNumProcessors();
    m_NumCPUsToUse = Math::Max< uint32_t >( 1, numCPUs / 2 );

    Load();

    // handle CPU downgrade
    m_NumCPUsToUse = Math::Min( Env::GetNumProcessors(), m_NumCPUsToUse );
}

// DESTRUCTOR
//------------------------------------------------------------------------------
WorkerSettings::~WorkerSettings() = default;

// SetMode
//------------------------------------------------------------------------------
void WorkerSettings::SetMode( Mode m )
{
    m_Mode = m;
}

// SetNumCPUsToUse
//------------------------------------------------------------------------------
void WorkerSettings::SetNumCPUsToUse( uint32_t c )
{
    m_NumCPUsToUse = c;
}

// SetStartMinimized
//------------------------------------------------------------------------------
void WorkerSettings::SetStartMinimized( bool startMinimized )
{
    m_StartMinimized = startMinimized;
}

// SetGracePeriod
//------------------------------------------------------------------------------
void WorkerSettings::SetGracePeriod( uint32_t gracePeriod )
{
    m_GracePeriod = gracePeriod;
}

// SetBlockingProcessNames
//------------------------------------------------------------------------------
void WorkerSettings::SetBlockingProcessNames( const Array<AString>& blockingProcessNames )
{
    m_BlockingProcessNames = blockingProcessNames;
}

// SetBlockingGracePeriod
//------------------------------------------------------------------------------
void WorkerSettings::SetBlockingGracePeriod( uint32_t blockingGracePeriod )
{
    m_BlockingGracePeriod = blockingGracePeriod;
}

// Load
//------------------------------------------------------------------------------
void WorkerSettings::Load()
{
    AStackString<> settingsPath;
    Env::GetExePath( settingsPath );
    settingsPath += ".settings";

    FileStream f;
    if ( f.Open( settingsPath.Get(), FileStream::READ_ONLY ) )
    {
        char header[ 4 ] = { 0 };
        f.Read( &header, 4 );
        if ( ( header[ 3 ] < FBUILDWORKER_SETTINGS_MIN_VERSION ) ||
             ( header[ 3 ] > FBUILDWORKER_SETTINGS_CURRENT_VERSION ) )
        {
            return; // version is too old, or newer, and cannot be read
        }

        // settings
        uint32_t mode;
        f.Read( mode );
        m_Mode = (Mode)mode;
        f.Read( m_NumCPUsToUse );
        f.Read( m_StartMinimized );
        if (header [ 3 ] >= 4)
        {
            f.Read( m_GracePeriod );
            f.Read( m_BlockingProcessNames );
            f.Read( m_BlockingGracePeriod );
        }
    }
}

// Save
//------------------------------------------------------------------------------
void WorkerSettings::Save()
{
    AStackString<> settingsPath;
    Env::GetExePath( settingsPath );
    settingsPath += ".settings";

    FileStream f;
    if ( f.Open( settingsPath.Get(), FileStream::WRITE_ONLY ) )
    {
        bool ok = true;

        // header
        ok &= ( f.Write( "FWS", 3 ) == 3 );
        ok &= ( f.Write( uint8_t( FBUILDWORKER_SETTINGS_CURRENT_VERSION ) ) );

        // settings
        ok &= f.Write( (uint32_t)m_Mode );
        ok &= f.Write( m_NumCPUsToUse );
        ok &= f.Write( m_StartMinimized );
        ok &= f.Write( m_GracePeriod );
        ok &= f.Write( m_BlockingProcessNames );
        ok &= f.Write( m_BlockingGracePeriod );

        if ( ok )
        {
            return;
        }
    }

    #if defined( __WINDOWS__ )
        MessageBox( nullptr, "Failed to save settings.", "FBuildWorker", MB_OK );
    #elif defined( __APPLE__ )
        // TODO:MAC Implement ShowMessageBox
    #elif defined( __LINUX__ )
        // TODO:LINUX Implement ShowMessageBox
    #else
        #error Unknown Platform
    #endif
}

//------------------------------------------------------------------------------
