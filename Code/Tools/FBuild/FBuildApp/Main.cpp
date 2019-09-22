// FBuild.cpp : Defines the entry point for the console application.
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Helpers/CtrlCHandler.h"

#include "Core/Process/Process.h"
#include "Core/Process/SharedMemory.h"
#include "Core/Process/SystemMutex.h"
#include "Core/Process/Thread.h"
#include "Core/Profile/Profile.h"
#include "Core/Strings/AStackString.h"
#include "Core/Tracing/Tracing.h"

#include <memory.h>
#include <stdio.h>
#if defined( __WINDOWS__ )
    #include "Core/Env/WindowsHeader.h"
#endif

// Return Codes
//------------------------------------------------------------------------------
enum ReturnCodes
{
    FBUILD_OK                               = 0,
    FBUILD_BUILD_FAILED                     = -1,
    FBUILD_ERROR_LOADING_BFF                = -2,
    FBUILD_BAD_ARGS                         = -3,
    FBUILD_ALREADY_RUNNING                  = -4,
    FBUILD_FAILED_TO_SPAWN_WRAPPER          = -5,
    FBUILD_FAILED_TO_SPAWN_WRAPPER_FINAL    = -6,
    FBUILD_WRAPPER_CRASHED                  = -7,
};

// Headers
//------------------------------------------------------------------------------
int WrapperMainProcess( const AString & args, const FBuildOptions & options, SystemMutex & finalProcess );
int WrapperIntermediateProcess( const FBuildOptions & options );
int Main(int argc, char * argv[]);

// Misc
//------------------------------------------------------------------------------
// data passed between processes in "wrapper" mode
struct SharedData
{
    bool    Started;
    int     ReturnCode;
};

// Global
//------------------------------------------------------------------------------
SharedMemory g_SharedMemory;

// main
//------------------------------------------------------------------------------
int main(int argc, char * argv[])
{
    // This wrapper is purely for profiling scope
    int result = Main( argc, argv );
    PROFILE_SYNCHRONIZE // make sure no tags are active and do one final sync
    return result;
}

// Main
//------------------------------------------------------------------------------
int Main(int argc, char * argv[])
{
    PROFILE_FUNCTION

    Timer t;

    // Register Ctrl-C Handler
    CtrlCHandler ctrlCHandler;

    // handle cmd line args
    FBuildOptions options;
    options.m_SaveDBOnCompletion = true; // Override default
    options.m_ShowProgress = true; // Override default
    switch ( options.ProcessCommandLine( argc, argv ) )
    {
        case FBuildOptions::OPTIONS_OK:             break;
        case FBuildOptions::OPTIONS_OK_AND_QUIT:    return FBUILD_OK;
        case FBuildOptions::OPTIONS_ERROR:          return FBUILD_BAD_ARGS;
    }

    const FBuildOptions::WrapperMode wrapperMode = options.m_WrapperMode;
    if ( wrapperMode == FBuildOptions::WRAPPER_MODE_INTERMEDIATE_PROCESS )
    {
        return WrapperIntermediateProcess( options );
    }

    #if defined( __WINDOWS__ )
        // TODO:MAC Implement SetPriorityClass
        // TODO:LINUX Implement SetPriorityClass
        VERIFY( SetPriorityClass( GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS ) );
    #endif

    // don't buffer output
    VERIFY( setvbuf(stdout, nullptr, _IONBF, 0) == 0 );
    VERIFY( setvbuf(stderr, nullptr, _IONBF, 0) == 0 );

    // ensure only one FASTBuild instance is running at a time
    SystemMutex mainProcess( options.GetMainProcessMutexName().Get() );

    // in "wrapper" mode, Main process monitors life of final process using this
    // (when main process can acquire, final process has terminated)
    SystemMutex finalProcess( options.GetFinalProcessMutexName().Get() );

    // only 1 instance running at a time (except if no build is requested)
    if ( ( wrapperMode == FBuildOptions::WRAPPER_MODE_MAIN_PROCESS ) ||
         ( wrapperMode == FBuildOptions::WRAPPER_MODE_NONE && options.m_PerformBuild ) )
    {
        if ( mainProcess.TryLock() == false )
        {
            if ( options.m_WaitMode == false )
            {
                OUTPUT( "FBuild: Error: Another instance of FASTBuild is already running in '%s'.\n", options.GetWorkingDir().Get() );
                return FBUILD_ALREADY_RUNNING;
            }

            OUTPUT( "FBuild: Waiting for another FASTBuild to terminate due to -wait option.\n" );
            while( mainProcess.TryLock() == false )
            {
                Thread::Sleep( 1000 );
                if ( FBuild::GetStopBuild() )
                {
                    return FBUILD_BUILD_FAILED;
                }
            }
        }
    }

    if ( wrapperMode == FBuildOptions::WRAPPER_MODE_MAIN_PROCESS )
    {
        return WrapperMainProcess( options.m_Args, options, finalProcess );
    }

    ASSERT( ( wrapperMode == FBuildOptions::WRAPPER_MODE_NONE ) ||
            ( wrapperMode == FBuildOptions::WRAPPER_MODE_FINAL_PROCESS ) );

    SharedData * sharedData = nullptr;
    if ( wrapperMode == FBuildOptions::WRAPPER_MODE_FINAL_PROCESS )
    {
        while ( !finalProcess.TryLock() )
        {
            OUTPUT( "FBuild: Waiting for another FASTBuild to terminate...\n" );
            if ( mainProcess.TryLock() )
            {
                // main process has aborted, terminate
                return FBUILD_FAILED_TO_SPAWN_WRAPPER_FINAL;
            }
            Thread::Sleep( 1000 );
        }

        g_SharedMemory.Open( options.GetSharedMemoryName().Get(), sizeof( SharedData ) );

        // signal to "main" process that we have started
        sharedData = (SharedData *)g_SharedMemory.GetPtr();
        if ( sharedData == nullptr )
        {
            // main process was killed while we were waiting
            return FBUILD_FAILED_TO_SPAWN_WRAPPER_FINAL;
        }
        sharedData->Started = true;
    }

    FBuild fBuild( options );

    // load the dependency graph if available
    if ( !fBuild.Initialize() )
    {
        if ( sharedData )
        {
            sharedData->ReturnCode = FBUILD_ERROR_LOADING_BFF;
        }
        ctrlCHandler.DeregisterHandler(); // Ensure this happens before FBuild is destroyed
        return FBUILD_ERROR_LOADING_BFF;
    }

    // initialize worker clients
    Array< AString > buildWorkers; // list of workers for build
    Array< AString > controlWorkers; // list of workers to send commands to
    if ( ( options.m_PerformBuild && options.m_AllowDistributed ) || options.m_WorkerCommands.IsEmpty() == false )
    {
        // List the remote workers to be able to send commands and build if needed
        if ( options.m_PerformBuild && options.m_AllowDistributed )
        {
            buildWorkers = options.m_Workers;
        }
        if ( options.m_WorkerCommands.IsEmpty() == false )
        {
            for (const FBuildOptions::WorkerCommandOptions& cmd : options.m_WorkerCommands)
            {
                if ( !controlWorkers.Find( cmd.m_Worker ) )
                {
                    controlWorkers.Append( cmd.m_Worker );
                }
            }
        }
        fBuild.InitializeWorkers( options.m_PerformBuild, buildWorkers, controlWorkers );
    }

    bool result = false;
    // this can be done in addition to other actions (i.e. before the actual build)
    if ( options.m_WorkerCommands.IsEmpty() == false )
    {
        int32_t infoLevel = 0; // store the last info level, used to refresh status while waiting
        Array< AString > singleWorker(1);
        for (const FBuildOptions::WorkerCommandOptions& cmd : options.m_WorkerCommands)
        {
            result = false; // reset error status
            int32_t waitTimeout = 0;
            const Array< AString > * cmdWorkers;
            if ( cmd.m_Worker == "*" )
            {
                cmdWorkers = &controlWorkers;
            }
            else
            {
                singleWorker.SetSize(1);
                singleWorker[0] = cmd.m_Worker;
                cmdWorkers = &singleWorker;
            }

            switch ( cmd.m_Command )
            {
                case FBuildOptions::WORKER_COMMAND_INFO:
                    infoLevel = cmd.m_Value;
                    result = fBuild.WorkersDisplayInfo( *cmdWorkers, cmd.m_Value );
                    break;
                case FBuildOptions::WORKER_COMMAND_SETMODE:
                    result = true; // non-blocking
                    fBuild.WorkersSetMode( *cmdWorkers, cmd.m_Value, options.m_WorkerCommandGracePeriod );
                    waitTimeout = options.m_WorkerCommandWaitTimeout;
                    break;
                case FBuildOptions::WORKER_COMMAND_ADDBLOCKING:
                    result = true; // non-blocking
                    fBuild.WorkersAddBlocking( *cmdWorkers, (uint32_t)cmd.m_Value, options.m_WorkerCommandGracePeriod );
                    waitTimeout = options.m_WorkerCommandWaitTimeout;
                    break;
                case FBuildOptions::WORKER_COMMAND_REMOVEBLOCKING:
                    result = true; // non-blocking
                    fBuild.WorkersRemoveBlocking( *cmdWorkers, (uint32_t)cmd.m_Value );
                    break;
            }
            if (waitTimeout != 0 && result)
            {
                result = fBuild.WorkersWaitIdle( *cmdWorkers, waitTimeout, infoLevel );
            }
            if (!options.m_WorkerCommandIgnoreFailures && result)
            {
                // check success of non-blocking commands
                result = fBuild.WorkersGetLastCommandResult();
            }
            if (!result && !options.m_WorkerCommandIgnoreFailures)
            {
                break; // stop trying to send commands
            }
        }
        if (!result && !options.m_WorkerCommandIgnoreFailures)
        {
            if ( sharedData )
            {
                sharedData->ReturnCode = FBUILD_BUILD_FAILED;
            }
            ctrlCHandler.DeregisterHandler(); // Ensure this happens before FBuild is destroyed
            return FBUILD_BUILD_FAILED;
        }
        // else continue the build
        result = false; // reset error status
    }

    // these actions are exclusive (i.e. only one is executed)
    if ( options.m_DisplayTargetList )
    {
        fBuild.DisplayTargetList( options.m_ShowHiddenTargets );
        result = true;
    }
    else if ( options.m_DisplayDependencyDB )
    {
        result = fBuild.DisplayDependencyDB( options.m_Targets );
    }
    else if ( options.m_GenerateCompilationDatabase )
    {
        result = fBuild.GenerateCompilationDatabase( options.m_Targets );
    }
    else if ( options.m_CacheInfo )
    {
        result = fBuild.CacheOutputInfo();
    }
    else if ( options.m_CacheTrim )
    {
        result = fBuild.CacheTrim();
    }
    else if ( options.m_PerformBuild )
    {
        result = fBuild.Build( options.m_Targets );
    }

    if ( sharedData )
    {
        sharedData->ReturnCode = ( result == true ) ? FBUILD_OK : FBUILD_BUILD_FAILED;
    }

    // final line of output - status of build
    float totalBuildTime = t.GetElapsed();
    uint32_t minutes = uint32_t( totalBuildTime / 60.0f );
    totalBuildTime -= ( minutes * 60.0f );
    const float seconds = totalBuildTime;
    if ( minutes > 0 )
    {
        FLOG_BUILD( "Time: %um %05.3fs\n", minutes, (double)seconds );
    }
    else
    {
        FLOG_BUILD( "Time: %05.3fs\n", (double)seconds );
    }

    ctrlCHandler.DeregisterHandler(); // Ensure this happens before FBuild is destroyed
    return ( result == true ) ? FBUILD_OK : FBUILD_BUILD_FAILED;
}

// WrapperMainProcess
//------------------------------------------------------------------------------
int WrapperMainProcess( const AString & args, const FBuildOptions & options, SystemMutex & finalProcess )
{
    // Create SharedMemory to communicate between Main and Final process
    SharedMemory sm;
    g_SharedMemory.Create( options.GetSharedMemoryName().Get(), sizeof( SharedData ) );
    SharedData * sd = (SharedData *)g_SharedMemory.GetPtr();
    memset( sd, 0, sizeof( SharedData ) );
    sd->ReturnCode = FBUILD_WRAPPER_CRASHED;

    // launch intermediate process
    AStackString<> argsCopy( args );
    argsCopy += " -wrapperintermediate";

    Process p;
    if ( !p.Spawn( options.m_ProgramName.Get(), argsCopy.Get(), options.GetWorkingDir().Get(), nullptr, true ) ) // true = forward output to our tty
    {
        return FBUILD_FAILED_TO_SPAWN_WRAPPER;
    }

    // the intermediate process will exit immediately after launching the final
    // process
    const int32_t result = p.WaitForExit();
    if ( result == FBUILD_FAILED_TO_SPAWN_WRAPPER_FINAL )
    {
        OUTPUT( "FBuild: Error: Intermediate process failed to spawn the final process.\n" );
        return result;
    }
    else if ( result != FBUILD_OK )
    {
        OUTPUT( "FBuild: Error: Intermediate process failed (%i).\n", result );
        return result;
    }

    // wait for final process to signal as started
    while ( sd->Started == false )
    {
        Thread::Sleep( 1 );
    }

    // wait for final process to exit
    for ( ;; )
    {
        if ( finalProcess.TryLock() == true )
        {
            break; // final process has released the mutex
        }
        Thread::Sleep( 1 );
    }

    return sd->ReturnCode;
}

// WrapperIntermediateProcess
//------------------------------------------------------------------------------
int WrapperIntermediateProcess( const FBuildOptions & options )
{
    // launch final process
    AStackString<> argsCopy( options.m_Args );
    argsCopy += " -wrapperfinal";

    Process p;
    if ( !p.Spawn( options.m_ProgramName.Get(), argsCopy.Get(), options.GetWorkingDir().Get(), nullptr, true ) ) // true = forward output to our tty
    {
        return FBUILD_FAILED_TO_SPAWN_WRAPPER_FINAL;
    }

    // don't wait for the final process (the main process will do that)
    p.Detach();
    return FBUILD_OK;
}

//------------------------------------------------------------------------------
