// FBuild - the main application
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
// FBuildCore
#include "FBuildOptions.h"
#include "Tools/FBuild/FBuildCore/FBuildVersion.h"
#include "Tools/FBuild/FBuildCore/FLog.h"

// Core
#include "Core/Env/Env.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/PathUtils.h"
#include "Core/Process/Process.h"
#include "Core/Math/xxHash.h"
#include "Core/Tracing/Tracing.h"

// FBuildWorker
#include "Tools/FBuild/FBuildWorker/Worker/WorkerSettings.h"
// system
#include <stdio.h> // for sscanf
#if defined( __WINDOWS__ )
    #include "Core/Env/WindowsHeader.h" // for QueryDosDeviceA
#endif

// CONSTRUCTOR - FBuildOptions
//------------------------------------------------------------------------------
FBuildOptions::FBuildOptions()
{
#ifdef DEBUG
    //m_ShowInfo = true; // uncomment this to enable spam when debugging
#endif

    // Default to NUMBER_OF_PROCESSORS
    m_NumWorkerThreads = Env::GetNumProcessors();

    // Default working dir is the system working dir
    AStackString<> workingDir;
    VERIFY( FileIO::GetCurrentDir( workingDir ) );
    SetWorkingDir( workingDir );
}

// ProcessCommandLine
//------------------------------------------------------------------------------
FBuildOptions::OptionsResult FBuildOptions::ProcessCommandLine( int argc, char * argv[] )
{
    // Store executable name
    AStackString<> programName( "FBuild.exe" );
    if ( argc > 0 )
    {
        AStackString<> programPath( argv[0] );
        if ( !programPath.IsEmpty() )
        {
            const char* slash = programPath.FindLast( NATIVE_SLASH );
            programName = ( slash ? slash + 1 : programPath.Get() );
        }
    }

    bool progressOptionSpecified = false;

    bool buildNeeded = false;
    bool buildNotNeeded = false;
    // Parse options
    for ( int32_t i=1; i<argc; ++i ) // start from 1 to skip exe name
    {
        AStackString<> thisArg( argv[ i ] );

        // Store into full arg string
        if ( m_Args.IsEmpty() == false )
        {
            m_Args += ' ';
        }
        m_Args += thisArg;

        // options start with a '-'
        if ( thisArg.BeginsWith( '-' ) )
        {
            if ( thisArg == "-cache" )
            {
                m_UseCacheRead = true;
                m_UseCacheWrite = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-cacheread" )
            {
                m_UseCacheRead = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-cachewrite" )
            {
                m_UseCacheWrite = true;
                continue;
            }
            else if ( thisArg == "-cacheinfo" )
            {
                m_CacheInfo = true;
                buildNotNeeded = true; // this is an action outside of a normal build
                continue;
            }
            else if ( thisArg == "-cachetrim" )
            {
                buildNotNeeded = true; // this is an action outside of a normal build
                const int sizeIndex = ( i + 1 );
                PRAGMA_DISABLE_PUSH_MSVC( 4996 ) // This function or variable may be unsafe...
                if ( ( sizeIndex >= argc ) ||
                     ( sscanf( argv[ sizeIndex ], "%u", &m_CacheTrim ) ) != 1 ) // TODO:C Consider using sscanf_s
                PRAGMA_DISABLE_POP_MSVC // 4996
                {
                    OUTPUT( "FBuild: Error: Missing or bad <sizeMiB> for '-cachetrim' argument\n" );
                    OUTPUT( "Try \"%s -help\"\n", programName.Get() );
                    return OPTIONS_ERROR;
                }
                i++; // skip extra arg we've consumed

                // add to args we might pass to subprocess
                m_Args += ' ';
                m_Args += argv[ sizeIndex ];
                continue;
            }
            else if ( thisArg == "-cacheverbose" )
            {
                m_CacheVerbose = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-clean" )
            {
                m_ForceCleanBuild = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-compdb" )
            {
                m_GenerateCompilationDatabase = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-config" )
            {
                int pathIndex = ( i + 1 );
                if ( pathIndex >= argc )
                {
                    OUTPUT( "FBuild: Error: Missing <path> for '-config' argument\n" );
                    OUTPUT( "Try \"%s -help\"\n", programName.Get() );
                    return OPTIONS_ERROR;
                }
                m_ConfigFile = argv[ pathIndex ];
                i++; // skip extra arg we've consumed

                // add to args we might pass to subprocess
                m_Args += ' ';
                m_Args += '"'; // surround config file with quotes to avoid problems with spaces in the path
                m_Args += m_ConfigFile;
                m_Args += '"';
                continue;
            }
            #ifdef DEBUG
                else if ( thisArg == "-debug" )
                {
                    ASSERT( false && "Break due to '-debug' argument - attach debugger!" );
                    continue;
                }
            #endif
            else if ( thisArg == "-dist" )
            {
                m_AllowDistributed = true;
                continue;
            }
            else if ( thisArg == "-distverbose" )
            {
                m_AllowDistributed = true;
                m_DistVerbose = true;
                continue;
            }
            else if ( thisArg == "-worker" )
            {
                int workerIndex = ( i + 1 );
                if ( workerIndex >= argc )
                {
                    OUTPUT( "FBuild: Error: Missing <worker> for '-worker' argument\n" );
                    OUTPUT( "Try \"%s -help\"\n", programName.Get() );
                    return OPTIONS_ERROR;
                }
                AStackString<> workerStr ( argv[ workerIndex ] );
                m_Workers.Append( workerStr );
                i++; // skip extra arg we've consumed
                m_AllowDistributed = true;
                continue;
            }
            else if ( thisArg == "-workers" )
            {
                int workerIndex = ( i + 1 );
                if ( workerIndex >= argc )
                {
                    OUTPUT( "FBuild: Error: Missing <workers> for '-workers' argument\n" );
                    OUTPUT( "Try \"%s -help\"\n", programName.Get() );
                    return OPTIONS_ERROR;
                }
                AStackString<> workerStr ( argv[ workerIndex ] );
                Array<AString> workers;
                workerStr.Tokenize( workers, ',' );
                m_Workers.Append( workers );
                i++; // skip extra arg we've consumed
                m_AllowDistributed = true;
                continue;
            }
            else if ( thisArg == "-workercmd" || thisArg == "-myworkercmd"  || thisArg == "-allworkerscmd" )
            {
                buildNotNeeded = true; // this is an action outside of a normal build
                WorkerCommandOptions cmd;
                if ( thisArg == "-myworkercmd" )
                {
                    cmd.m_Worker = "127.0.0.1";
                }
                else if ( thisArg == "-allworkerscmd" )
                {
                    cmd.m_Worker = "*";
                }
                else
                {
                    int workerIndex = ( i + 1 );
                    if ( workerIndex >= argc )
                    {
                        OUTPUT( "FBuild: Error: Missing <worker> for '%s' argument\n", thisArg.Get() );
                        OUTPUT( "Try \"%s -help\"\n", programName.Get() );
                        return OPTIONS_ERROR;
                    }
                    cmd.m_Worker = argv[ workerIndex ];
                    i += 1; // skip extra arg we've consumed
                }
                int cmdIndex = ( i + 1 );
                if ( cmdIndex >= argc )
                {
                    OUTPUT( "FBuild: Error: Missing <cmd> for '%s' argument\n", thisArg.Get() );
                    OUTPUT( "Try \"%s -help\"\n", programName.Get() );
                    return OPTIONS_ERROR;
                }
                AStackString<> cmdStr ( argv[ cmdIndex ] );
                int valIndex = ( i + 2 );
                if ( valIndex >= argc )
                {
                    OUTPUT( "FBuild: Error: Missing <value> for '%s' argument\n", thisArg.Get() );
                    OUTPUT( "Try \"%s -help\"\n", programName.Get() );
                    return OPTIONS_ERROR;
                }
                AStackString<> valStr ( argv[ valIndex ] );
                i+=2; // skip extra arg we've consumed
                if ( cmdStr == "info" || cmdStr == "json")
                {
                    cmd.m_Command = FBuildOptions::WORKER_COMMAND_INFO;
                    PRAGMA_DISABLE_PUSH_MSVC( 4996 ) // This function or variable may be unsafe...
                    sscanf( valStr.Get(), "%i", &cmd.m_Value );
                    PRAGMA_DISABLE_POP_MSVC // 4996
                    if (cmdStr == "json")
                    {
                        cmd.m_Value = -cmd.m_Value; // we use negative info level internally for json requests
                    }
                }
                else if ( cmdStr == "setmode" )
                {
                    cmd.m_Command = FBuildOptions::WORKER_COMMAND_SETMODE;
                    if ( valStr.EqualsI(                        "disabled" ) )
                        cmd.m_Value = (int32_t) WorkerSettings::Mode::DISABLED;
                    else if ( valStr.EqualsI(                   "idle" ) )
                        cmd.m_Value = (int32_t) WorkerSettings::Mode::WHEN_IDLE;
                    else if ( valStr.EqualsI(                   "dedicated" ) )
                        cmd.m_Value = (int32_t) WorkerSettings::Mode::DEDICATED;
                    else if ( valStr.EqualsI(                   "proportional" ) )
                        cmd.m_Value = (int32_t) WorkerSettings::Mode::PROPORTIONAL;
                    else
                    {
                        OUTPUT( "FBuild: Error: Unrecognized <mode> for '%s' argument\n", thisArg.Get() );
                        OUTPUT( "Try \"%s -help\"\n", programName.Get() );
                        return OPTIONS_ERROR;
                    }
                }
                else if ( cmdStr == "addblocking" || cmdStr == "removeblocking" )
                {
                    int32_t pid = 0;
                    PRAGMA_DISABLE_PUSH_MSVC( 4996 ) // This function or variable may be unsafe...
                    sscanf( valStr.Get(), "%i", &pid );
                    PRAGMA_DISABLE_POP_MSVC // 4996
                    if ( pid <= 0 )
                    { // negative or 0 values means this process or its nth parent
                        pid = Process::GetParentId( Process::GetCurrentId(), -pid );
                    }
                    cmd.m_Value = pid;
                    if ( cmdStr == "addblocking" )
                    {
                        cmd.m_Command = FBuildOptions::WORKER_COMMAND_ADDBLOCKING;
                    }
                    else if ( cmdStr == "removeblocking" )
                    {
                        cmd.m_Command = FBuildOptions::WORKER_COMMAND_REMOVEBLOCKING;
                    }
                }
                else
                {
                    OUTPUT( "FBuild: Error: Unrecognized <cmd> for '%s' argument\n", thisArg.Get() );
                    OUTPUT( "Try \"%s -help\"\n", programName.Get() );
                    return OPTIONS_ERROR;
                }
                m_WorkerCommands.Append(cmd);
                continue;
            }
            else if ( thisArg == "-workercmdflag" )
            {
                int flagIndex = ( i + 1 );
                if ( flagIndex >= argc )
                {
                    OUTPUT( "FBuild: Error: Missing <cmd> for '%s' argument\n", thisArg.Get() );
                    OUTPUT( "Try \"%s -help\"\n", programName.Get() );
                    return OPTIONS_ERROR;
                }
                AStackString<> flagStr ( argv[ flagIndex ] );
                i += 1; // skip extra arg we've consumed
                if (flagStr == "nofailure" )
                {
                    m_WorkerCommandIgnoreFailures = true;
                    continue;
                }
                else if ( flagStr == "grace" || flagStr == "wait" )
                {
                    int valIndex = ( i + 2 );
                    if ( valIndex >= argc )
                    {
                        OUTPUT( "FBuild: Error: Missing <value> for '%s' argument\n", thisArg.Get() );
                        OUTPUT( "Try \"%s -help\"\n", programName.Get() );
                        return OPTIONS_ERROR;
                    }
                    AStackString<> valStr ( argv[ valIndex ] );
                    i += 1; // skip extra arg we've consumed
                    if ( flagStr == "grace")
                    {
                        PRAGMA_DISABLE_PUSH_MSVC( 4996 ) // This function or variable may be unsafe...
                        sscanf( valStr.Get(), "%i", &m_WorkerCommandGracePeriod );
                        PRAGMA_DISABLE_POP_MSVC // 4996
                        continue;
                    }
                    else if ( flagStr == "wait")
                    {
                        PRAGMA_DISABLE_PUSH_MSVC( 4996 ) // This function or variable may be unsafe...
                        sscanf( valStr.Get(), "%u", &m_WorkerCommandWaitTimeout );
                        PRAGMA_DISABLE_POP_MSVC // 4996
                        continue;
                    }
                }
                else
                {
                    OUTPUT( "FBuild: Error: Unrecognized <flag> for '%s' argument\n", thisArg.Get() );
                    OUTPUT( "Try \"%s -help\"\n", programName.Get() );
                    return OPTIONS_ERROR;
                }
            }
            else if ( thisArg == "-fastcancel" )
            {
                m_FastCancel = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-fixuperrorpaths" )
            {
                m_FixupErrorPaths = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-forceremote" )
            {
                m_AllowDistributed = true;
                m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
                m_AllowLocalRace = false;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-help" )
            {
                DisplayHelp( programName );
                return OPTIONS_OK_AND_QUIT; // exit app
            }
            else if ( ( thisArg == "-ide" ) || ( thisArg == "-vs" ) )
            {
                m_ShowProgress = false;
                progressOptionSpecified = true;
                #if defined( __WINDOWS__ )
                    m_FixupErrorPaths = true;
                    m_WrapperMode = WRAPPER_MODE_MAIN_PROCESS;
                #endif
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            PRAGMA_DISABLE_PUSH_MSVC( 4996 ) // This function or variable may be unsafe...
            else if ( thisArg.BeginsWith( "-j" ) &&
                      sscanf( thisArg.Get(), "-j%u", &m_NumWorkerThreads ) == 1 ) // TODO:C Consider using sscanf_s
            PRAGMA_DISABLE_POP_MSVC // 4996
            {
                // only accept within sensible range
                if ( m_NumWorkerThreads <= 256 )
                {
                    continue; // 'numWorkers' will contain value now
                }
            }
            else if ( thisArg == "-monitor" )
            {
                m_EnableMonitor = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-nooutputbuffering" )
            {
                // this doesn't do anything any more
                OUTPUT( "FBuild: Warning: -nooutputbuffering is deprecated.\n" );
                continue;
            }
            else if ( thisArg == "-noprogress" )
            {
                m_ShowProgress = false;
                progressOptionSpecified = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-nostoponerror")
            {
                m_StopOnFirstError = false;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-nosummaryonerror" )
            {
                m_ShowSummary = true;
                m_NoSummaryOnError = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-nounity" )
            {
                m_NoUnity = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-progress" )
            {
                m_ShowProgress = true;
                progressOptionSpecified = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-quiet" )
            {
                m_ShowBuildCommands = false;
                m_ShowInfo = false;
                continue;
            }
            else if ( thisArg == "-report" )
            {
                m_GenerateReport = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-showcmds" )
            {
                m_ShowCommandLines = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-showdeps" )
            {
                m_DisplayDependencyDB = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-showtargets" )
            {
                m_DisplayTargetList = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-showalltargets" )
            {
                m_DisplayTargetList = true;
                m_ShowHiddenTargets = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-summary" )
            {
                m_ShowSummary = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-verbose" )
            {
                m_ShowInfo = true;
                m_CacheVerbose = true;
                continue;
            }
            else if ( thisArg == "-version" )
            {
                DisplayVersion();
                return OPTIONS_OK_AND_QUIT; // exit app
            }
            // -vs : see -ide
            else if ( thisArg == "-wait" )
            {
                m_WaitMode = true;
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-wrapper")
            {
                #if defined( __WINDOWS__ )
                    m_WrapperMode = WRAPPER_MODE_MAIN_PROCESS;
                #endif
                buildNeeded = true; // this option implies performing a build
                continue;
            }
            else if ( thisArg == "-wrapperintermediate") // Internal use only
            {
                #if defined( __WINDOWS__ )
                    m_WrapperMode = WRAPPER_MODE_INTERMEDIATE_PROCESS;
                #endif
                continue;
            }
            else if ( thisArg == "-wrapperfinal") // Internal use only
            {
                #if defined( __WINDOWS__ )
                    m_WrapperMode = WRAPPER_MODE_FINAL_PROCESS;
                #endif
                continue;
            }

            // can't use FLOG_ERROR as FLog is not initialized
            OUTPUT( "FBuild: Error: Unknown argument '%s'\n", thisArg.Get() );
            OUTPUT( "Try \"%s -help\"\n", programName.Get() );
            return OPTIONS_ERROR;
        }
        else
        {
            // assume target
            m_Targets.Append( thisArg );
            buildNeeded = true; // this option implies performing a build
        }
    }
    // We always perform a build, except if no targets or options implying a build is given
    // AND an action not linked to a build is requested (cache trim, workers control, ...)
    m_PerformBuild = buildNeeded || !buildNotNeeded;

    if ( progressOptionSpecified == false )
    {
        // By default show progress bar only if stdout goes to the terminal
        m_ShowProgress = ( Env::IsStdOutRedirected() == false );
    }

    // Default to build "all"
    if ( m_Targets.IsEmpty() && m_PerformBuild )
    {
        FLOG_INFO( "No target specified, defaulting to target 'all'" );
        m_Targets.Append( AStackString<>( "all" ) );
    }

    // When building multiple targets, try to build as much as possible
    if ( m_Targets.GetSize() > 1 )
    {
        m_StopOnFirstError = false;
    }

    // cache mode environment variable (if not supplied on cmd line)
    if ( ( m_UseCacheRead == false ) && ( m_UseCacheWrite == false ) )
    {
        AStackString<> cacheMode;
        if ( Env::GetEnvVariable( "FASTBUILD_CACHE_MODE", cacheMode ) )
        {
            if ( cacheMode == "r" )
            {
                m_UseCacheRead = true;
            }
            else if ( cacheMode == "w" )
            {
                m_UseCacheWrite = true;
            }
            else if ( cacheMode == "rw" )
            {
                m_UseCacheRead = true;
                m_UseCacheWrite = true;
            }
            else
            {
                OUTPUT( "FASTBUILD_CACHE_MODE is invalid (%s)\n", cacheMode.Get() );
                return OPTIONS_ERROR;
            }
        }
    }

    // Global mutex names depend on workingDir which is managed by FBuildOptions
    m_ProgramName = programName;

    return OPTIONS_OK;
}

// FBuildOptions::SetWorkingDir
//------------------------------------------------------------------------------
void FBuildOptions::SetWorkingDir( const AString & path )
{
    ASSERT( !path.IsEmpty() );
    m_WorkingDir = path;

    // clean path
    PathUtils::FixupFolderPath( m_WorkingDir );

    // no trailing slash
    if ( m_WorkingDir.EndsWith( NATIVE_SLASH ) )
    {
        m_WorkingDir.SetLength( m_WorkingDir.GetLength() - 1 );
    }

    #if defined( __WINDOWS__ )
        // so C:\ and c:\ are treated the same on Windows, for better cache hits
        // make the drive letter always uppercase
        if ( ( m_WorkingDir.GetLength() >= 2 ) &&
             ( m_WorkingDir[ 1 ] == ':' ) &&
             ( m_WorkingDir[ 0 ] >= 'a' ) &&
             ( m_WorkingDir[ 0 ] <= 'z' ) )
        {
            m_WorkingDir[ 0 ] = ( 'A' + ( m_WorkingDir[ 0 ] - 'a' ) );
        }
    #endif

    // Generate Mutex/SharedMemory names
    #if defined( __WINDOWS__ ) || defined( __OSX__ )
        #if defined( __WINDOWS__ )
            // convert subst drive mappings to the read path
            // (so you can't compile from the real path and the subst path at the
            // same time which would cause problems)
            AStackString<> canonicalPath;
            if ( ( m_WorkingDir.GetLength() >= 2 ) &&
                 ( m_WorkingDir[ 1 ] == ':' ) &&
                 ( m_WorkingDir[ 0 ] >= 'A' ) &&
                 ( m_WorkingDir[ 0 ] <= 'Z' ) )
            {
                // get drive letter without slash
                AStackString<> driveLetter( m_WorkingDir.Get(), m_WorkingDir.Get() + 2 );

                // get real path for drive letter (could be the same, or a subst'd path)
                char actualPath[ MAX_PATH ];
                actualPath[ 0 ] = '\000';
                VERIFY( QueryDosDeviceA( driveLetter.Get(), actualPath, MAX_PATH ) );

                // if returned path is of format "\??\C:\Folder"...
                if ( AString::StrNCmp( actualPath, "\\??\\", 4 ) == 0 )
                {
                    // .. then it means the working dir is a subst folder
                    // trim the "\\??\\" and use the real path as a base
                    canonicalPath = &actualPath[ 4 ];
                    canonicalPath += ( m_WorkingDir.Get() + 2 ); // add everything after the subst drive letter
                }
                else
                {
                    // The path was already a real path (QueryDosDevice returns the volume only)
                    canonicalPath = m_WorkingDir;
                }
            }
            else
            {
                // a UNC or other funky path - just leave it as is
                canonicalPath = m_WorkingDir;
            }
        #elif defined( __OSX__ )
            AStackString<> canonicalPath( m_WorkingDir );
        #endif

        // case insensitive
        canonicalPath.ToLower();
    #elif defined( __LINUX__ )
        // case sensitive
        AStackString<> canonicalPath( m_WorkingDir );
    #endif

    m_WorkingDirHash = xxHash::Calc32( canonicalPath );
    m_ProcessMutexName.Format( "Global\\FASTBuild-0x%08x", m_WorkingDirHash );
    m_FinalProcessMutexName.Format( "Global\\FASTBuild_Final-0x%08x", m_WorkingDirHash );
    m_SharedMemoryName.Format( "FASTBuildSharedMemory_%08x", m_WorkingDirHash );
}

// DisplayHelp
//------------------------------------------------------------------------------
void FBuildOptions::DisplayHelp( const AString & programName ) const
{
    DisplayVersion();
    OUTPUT( "----------------------------------------------------------------------\n"
            "Usage: %s [options] [target1]..[targetn]\n", programName.Get() );
    OUTPUT( "The default action is to perform a build, which is done if a target is\n"
            "specified, or any Build Options is used, or no other actions are\n"
            "requested.\n" );
    OUTPUT( "----------------------------------------------------------------------\n"
            "Generic Options:\n"
            " -verbose       Show detailed diagnostic information. This will slow\n"
            "                down building.\n"
            " -config [path] Explicitly specify the config file to use.\n" );
#ifdef DEBUG
    OUTPUT( " -debug         Break at startup, to attach debugger.\n" );
#endif
    OUTPUT( " -dist          Allow distributed compilation.\n"
            " -distverbose   Print detailed info for distributed compilation.\n"
            " -workers [names] Use these specific workers. Multiple names can be\n"
            "                set by using this option multiple times, or using ','.\n"
            "                This option overrides the list of workers from the BFF\n"
            "                file or brokerage.\n" );
    OUTPUT( "----------------------------------------------------------------------\n"
            "Build Options:\n"
            " -cache[read|write] Control use of the build cache.\n"
            " -cacheverbose  Emit details about cache interactions.\n"
            " -clean         Force a clean build.\n"
            " -compdb        Generate JSON compilation database for specified targets.\n"
            " -fastcancel    [Experimental] Fast cancellation behavior on build failure.\n"
            " -fixuperrorpaths Reformat error paths to be Visual Studio friendly.\n"
            " -forceremote   Force distributable jobs to only be built remotely.\n"
            " -ide           Enable multiple options when building from an IDE.\n"
            "                Enables: -noprogress, -fixuperrorpaths &\n"
            "                -wrapper (Windows)\n"
            " -j[x]          Explicitly set LOCAL worker thread count X, instead of\n"
            "                default of hardware thread count.\n"
            " -monitor       Emit a machine-readable file while building.\n"
            " -noprogress    Don't show the progress bar while building.\n"
            " -nounity       [Experimental] Build files individually instead of in Unity.\n"
            " -nostoponerror Don't stop building on first error. Try to build as much\n"
            "                as possible.\n"
            " -nosummaryonerror Hide the summary if the build fails. Implies -summary.\n"
            " -progress      Show the progress bar while building, even if stdout is redirected.\n"
            " -quiet         Don't show build output.\n"
            " -report        Ouput a detailed report.html at the end of the build.\n"
            "                This will lengthen the total build time.\n"
            " -showcmds      Show command lines used to launch external processes.\n"
            " -showdeps      Show known dependency tree for specified targets.\n"
            " -showtargets   Display list of primary targets, excluding those marked \"Hidden\".\n"
            " -showalltargets Display list of primary targets, including those marked \"Hidden\".\n"
            " -summary       Show a summary at the end of the build.\n"
            " -vs            VisualStudio mode. Same as -ide.\n"
            " -wait          Wait for a previous build to complete before starting.\n"
            "                (Slower than building both targets in one invocation).\n"
            " -wrapper       (Windows only) Spawn a sub-process to gracefully handle\n"
            "                termination from Visual Studio.\n" );
    OUTPUT( "----------------------------------------------------------------------\n"
            "Other Actions Options:\n"
            " -cacheinfo     Output cache statistics.\n"
            " -cachetrim [size] Trim the cache to the given size in MiB.\n"
            " -help          Show this help.\n"
            " -version       Print version and exit. No other work will be\n"
            "                performed.\n"
            " -workercmd [worker] [cmd] [value] Send a command to a specific worker.\n"
            "                Note: most commands are meant for the localhost worker.\n"
            " -myworkercmd [cmd] [value] Alias for -workercmd 127.0.0.1 cmd value.\n"
            " -allworkerscmd [cmd] [value] Send a command to all workers, as set by\n"
            "                the -workers option, or the BFF file, or brokerage.\n"
            "                Note: controlling multiple workers may be rdangerousisky.\n"
            "                This is mainly meant for the info command.\n"
            "   Commands:\n"
            "    info [level] Request each worker to show their status\n"
            "                level = 1 for oneline summary, 2 for per-CPU details)\n"
            "    json [level] Same as info, but outputing the result in json format\n"
            "    setmode [mode] Set the worker mode\n"
            "                mode = disabled | idle | dedicated | proportional\n"
            "    [add|remove]blocking [pid] Add/Remove a process Id that blocks the\n"
            "                execution of jobs until it terminates.\n"
            "                pid > 0: a specific process (local to the worker).\n"
            "                pid = 0: the fbuild process (useful to quickly free-up\n"
            "                         the computer for a local build).\n"
            "                pid < 0: the nth parent of the fbuild process (can be\n"
            "                         called from an app/script requiring exclusive\n"
            "                         use of the computer until it finishes).\n"
            " -workercmdflag [flag] [value] Set a flag value for commands to workers.\n"
            "   Flags:\n"
            "    grace [seconds] Timeout until extra remaining jobs are killed\n"
            "                when using setmode or addblocking.\n"
            "    wait [seconds] Wait up to the given timeout for jobs to terminate\n"
            "                when using setmode or addblocking. The process will\n"
            "                return an error if jobs are still running.\n"
            "    nofailure   Ignore connections failures. Useful when the worker(s)\n"
            "                may not be running, in which case it is not necessary to\n"
            "                ask/wait for it to be blocked / disabled.\n"
            "----------------------------------------------------------------------\n" );
}

// DisplayVersion
//------------------------------------------------------------------------------
void FBuildOptions::DisplayVersion() const
{
    #ifdef DEBUG
        #define VERSION_CONFIG "(DEBUG) "
    #else
        #define VERSION_CONFIG ""
    #endif
    OUTPUT( "FASTBuild - " FBUILD_VERSION_STRING " " VERSION_CONFIG "- "
            "Copyright 2012-2019 Franta Fulin - http://www.fastbuild.org\n" );
    #undef VERSION_CONFIG
}

//------------------------------------------------------------------------------
