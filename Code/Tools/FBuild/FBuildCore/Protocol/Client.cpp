// Client.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Client.h"

#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"
#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Graph/CompilerNode.h"
#include "Tools/FBuild/FBuildCore/Graph/FileNode.h"
#include "Tools/FBuild/FBuildCore/Graph/Node.h"
#include "Tools/FBuild/FBuildCore/Graph/ObjectNode.h"
#include <Tools/FBuild/FBuildCore/Helpers/MultiBuffer.h>
#include "Tools/FBuild/FBuildCore/WorkerPool/Job.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/JobQueue.h"
#include "Tools/FBuild/FBuildWorker/Worker/WorkerSettings.h"

#include "Core/Env/ErrorFormat.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/FileIO/MemoryStream.h"
#include "Core/Math/Random.h"
#include "Core/Math/Conversions.h"
#include "Core/Process/Atomic.h"
#include "Core/Profile/Profile.h"
#include "Core/Tracing/Tracing.h"

// Defines
//------------------------------------------------------------------------------
#define CLIENT_STATUS_UPDATE_FREQUENCY_SECONDS ( 0.1f )
#define CONNECTION_REATTEMPT_DELAY_TIME ( 10.0f )
#define SYSTEM_ERROR_ATTEMPT_COUNT ( 3 )
#define DIST_INFO( ... ) if ( m_DetailedLogging ) { FLOG_BUILD( __VA_ARGS__ ); }

// CONSTRUCTOR
//------------------------------------------------------------------------------
Client::Client( const Array< AString > & buildWorkerList,
                const Array< AString > & controlWorkerList,
                uint16_t port,
                uint32_t workerConnectionLimit,
                bool detailedLogging )
    : m_WorkerList( buildWorkerList )
    , m_ShouldExit( false )
    , m_DetailedLogging( detailedLogging )
    , m_ControlPendingSendCounter ( 0 )
    , m_ControlPendingReceiveCounter ( 0 )
    , m_ControlMessageExpectResponse ( false )
    , m_WorkerConnectionLimit( workerConnectionLimit )
    , m_Port( port )
{
    // Append control workers
    size_t firstControlOnlyWorker = m_WorkerList.GetSize();
    for ( const AString& worker : controlWorkerList )
    {
        if ( m_WorkerList.Find( worker ) == nullptr )
        {
            m_WorkerList.Append( worker );
        }
    }

    // allocate space for server states
    m_ServerList.SetSize( m_WorkerList.GetSize() );

    // set build/control flags for workers
    for ( size_t i = 0; i < m_ServerList.GetSize(); ++i )
    {
        if (i < firstControlOnlyWorker)
        {
            // this was a build worker (and could also be a control worker)
            m_ServerList[i].m_BuildJobsEnabled = true;
            m_ServerList[i].m_ControlEnabled = ( controlWorkerList.Find( m_WorkerList[i] ) != nullptr);
        }
        else
        {
            // this was a control-only worker
            m_ServerList[i].m_BuildJobsEnabled = false;
            m_ServerList[i].m_ControlEnabled = true;
        }
    }

    m_Thread = Thread::CreateThread( ThreadFuncStatic,
                                     "Client",
                                     ( 64 * KILOBYTE ),
                                     this );
    ASSERT( m_Thread );
}

// DESTRUCTOR
//------------------------------------------------------------------------------
Client::~Client()
{
    SetShuttingDown();

    AtomicStoreRelaxed( &m_ShouldExit, true );
    Thread::WaitForThread( m_Thread );

    ShutdownAllConnections();

    Thread::CloseHandle( m_Thread );
}

//------------------------------------------------------------------------------
/*virtual*/ void Client::OnDisconnected( const ConnectionInfo * connection )
{
    ASSERT( connection );
    ServerState * ss = (ServerState *)connection->GetUserData();
    ASSERT( ss );

    MutexHolder mh( ss->m_Mutex );
    DIST_INFO( "Disconnected: %s\n", ss->m_RemoteName.Get() );
    if ( ss->m_Jobs.IsEmpty() == false )
    {
        Job ** it = ss->m_Jobs.Begin();
        const Job * const * end = ss->m_Jobs.End();
        while ( it != end )
        {
            FLOG_MONITOR( "FINISH_JOB TIMEOUT %s \"%s\" \n", ss->m_RemoteName.Get(), (*it)->GetNode()->GetName().Get() );
            JobQueue::Get().ReturnUnfinishedDistributableJob( *it );
            ++it;
        }
        ss->m_Jobs.Clear();
    }

    // This is usually null here, but might need to be freed if
    // we had the connection drop between message and payload
    FREE( (void *)( ss->m_CurrentMessage ) );

    ss->m_RemoteName.Clear();
    AtomicStoreRelaxed( &ss->m_Connection, static_cast< const ConnectionInfo * >( nullptr ) );
    ss->m_CurrentMessage = nullptr;
}

// ThreadFuncStatic
//------------------------------------------------------------------------------
/*static*/ uint32_t Client::ThreadFuncStatic( void * param )
{
    PROFILE_SET_THREAD_NAME( "ClientThread" )

    Client * c = (Client *)param;
    c->ThreadFunc();
    return 0;
}

// ThreadFunc
//------------------------------------------------------------------------------
void Client::ThreadFunc()
{
    PROFILE_FUNCTION

    // ensure first status update will be sent more rapidly
    m_StatusUpdateTimer.Start();

    for ( ;; )
    {
        LookForWorkers();
        if ( AtomicLoadRelaxed( &m_ShouldExit ) )
        {
            break;
        }

        CommunicateJobAvailability();
        if ( AtomicLoadRelaxed( &m_ShouldExit ) )
        {
            break;
        }

        CommunicateCommands();
        if ( AtomicLoadRelaxed( &m_ShouldExit ) )
        {
            break;
        }

        Thread::Sleep( 1 );
        if ( AtomicLoadRelaxed( &m_ShouldExit ) )
        {
            break;
        }
    }
}

// LookForWorkers
//------------------------------------------------------------------------------
void Client::LookForWorkers()
{
    PROFILE_FUNCTION

    MutexHolder mh( m_ServerListMutex );

    const size_t numWorkers( m_ServerList.GetSize() );

    // find out how many connections we have now
    size_t numConnections = 0;
    for ( size_t i=0; i<numWorkers; i++ )
    {
        if ( AtomicLoadRelaxed( &m_ServerList[ i ].m_Connection ) )
        {
            numConnections++;
        }
    }

    // limit maximum concurrent connections
    if ( numConnections >= m_WorkerConnectionLimit )
    {
        return;
    }

    // if we're connected to every possible worker already
    if ( numConnections == numWorkers )
    {
        return;
    }

    // randomize the start index to better distribute workers when there
    // are many workers/clients - otherwise all clients will attempt to connect
    // to the same subset of workers
    static Random r;
    size_t startIndex = r.GetRandIndex( (uint32_t)numWorkers );

    // find someone to connect to
    for ( size_t j=0; j<numWorkers; j++ )
    {
        const size_t i( ( j + startIndex ) % numWorkers );

        ServerState & ss = m_ServerList[ i ];
        if ( AtomicLoadRelaxed( &ss.m_Connection ) )
        {
            continue;
        }

        // ignore blacklisted workers
        if ( !ss.m_BuildJobsEnabled && !ss.m_ControlEnabled )
        {
            continue;
        }

        // lock the server state
        MutexHolder mhSS( ss.m_Mutex );

        ASSERT( ss.m_Jobs.IsEmpty() );

        if ( ss.m_DelayTimer.GetElapsed() < CONNECTION_REATTEMPT_DELAY_TIME )
        {
            continue;
        }

        DIST_INFO( "Connecting to: %s\n", m_WorkerList[ i ].Get() );
        const ConnectionInfo * ci = Connect( m_WorkerList[ i ], m_Port, 2000, &ss ); // 2000ms connection timeout
        if ( ci == nullptr )
        {
            DIST_INFO( " - connection: %s (FAILED)\n", m_WorkerList[ i ].Get() );
            ss.m_DelayTimer.Start(); // reset connection attempt delay
        }
        else
        {
            DIST_INFO( " - connection: %s (OK)\n", m_WorkerList[ i ].Get() );
            const uint32_t numJobsAvailable = ss.m_BuildJobsEnabled ? (uint32_t)JobQueue::Get().GetNumDistributableJobsAvailable() : (uint32_t)0;

            ss.m_RemoteName = m_WorkerList[ i ];
            AtomicStoreRelaxed( &ss.m_Connection, ci ); // success!
            ss.m_NumJobsAvailable = numJobsAvailable;

            // send connection msg
            Protocol::MsgConnection msg( numJobsAvailable );
            SendMessageInternal( ci, msg );
        }

        // limit to one connection attempt per iteration
        return;
    }
}

// CommunicateJobAvailability
//------------------------------------------------------------------------------
void Client::CommunicateJobAvailability()
{
    PROFILE_FUNCTION

    // too soon since last status update?
    if ( m_StatusUpdateTimer.GetElapsed() < CLIENT_STATUS_UPDATE_FREQUENCY_SECONDS )
    {
        return;
    }

    m_StatusUpdateTimer.Start(); // reset time

    // has status changed since we last sent it?
    uint32_t numJobsAvailable = (uint32_t)JobQueue::Get().GetNumDistributableJobsAvailable();
    Protocol::MsgStatus msg( numJobsAvailable );

    MutexHolder mh( m_ServerListMutex );
    if ( m_ServerList.IsEmpty() )
    {
        return; // no servers to communicate with
    }

    // update each server to know how many jobs we have now
    ServerState * it = m_ServerList.Begin();
    const ServerState * const end = m_ServerList.End();
    while ( it != end )
    {
        if ( it->m_BuildJobsEnabled && AtomicLoadRelaxed( &it->m_Connection ) )
        {
            MutexHolder ssMH( it->m_Mutex );
            if ( const ConnectionInfo * connection = AtomicLoadRelaxed( &it->m_Connection ) )
            {
                if ( it->m_NumJobsAvailable != numJobsAvailable )
                {
                    PROFILE_SECTION( "UpdateJobAvailability" )
                    SendMessageInternal( connection, msg );
                    it->m_NumJobsAvailable = numJobsAvailable;
                }
            }
        }
        ++it;
    }
}

// CommunicateCommands
//------------------------------------------------------------------------------
void Client::CommunicateCommands()
{
    PROFILE_FUNCTION

    // no command to send
    if (AtomicLoadRelaxed( &m_ControlPendingSendCounter ) == 0)
    {
        return;
    }

    MutexHolder mh( m_ServerListMutex );
    // Find a server with a pending command
    ServerState * it = m_ServerList.Begin();
    const ServerState * const end = m_ServerList.End();
    while ( it != end )
    {
        if ( it->m_ControlPendingSend && AtomicLoadRelaxed( &it->m_Connection ) )
        {
            MutexHolder ssMH( it->m_Mutex );
            if ( const ConnectionInfo * connection = AtomicLoadRelaxed( &it->m_Connection ) )
            {
                PROFILE_SECTION( "SendCommand" )
                const Protocol::IMessage* msg = m_ControlMessage.Get();
                if (msg->HasPayload())
                {
                    SendMessageInternal( connection, *msg, *m_ControlMessagePayload.Get() );
                }
                else
                {
                    SendMessageInternal( connection, *msg );
                }
            }
            it->m_ControlPendingSend = false;
            if ( m_ControlMessageExpectResponse )
            {
                it->m_ControlPendingResponse = true;
                AtomicIncU32( &m_ControlPendingReceiveCounter );
            }
            else
            {
                it->m_ControlSuccess = true;
            }
            AtomicDecU32(&m_ControlPendingSendCounter);
        }
        ++it;
    }
}

// SendMessageInternal
//------------------------------------------------------------------------------
void Client::SendMessageInternal( const ConnectionInfo * connection, const Protocol::IMessage & msg )
{
    if ( msg.Send( connection ) )
    {
        return;
    }

    DIST_INFO( "Send Failed: %s (Type: %u, Size: %u)\n",
                ((ServerState *)connection->GetUserData())->m_RemoteName.Get(),
                (uint32_t)msg.GetType(),
                msg.GetSize() );
}

// SendMessageInternal
//------------------------------------------------------------------------------
void Client::SendMessageInternal( const ConnectionInfo * connection, const Protocol::IMessage & msg, const MemoryStream & memoryStream )
{
    if ( msg.Send( connection, memoryStream ) )
    {
        return;
    }

    DIST_INFO( "Send Failed: %s (Type: %u, Size: %u, Payload: %u)\n",
                ((ServerState *)connection->GetUserData())->m_RemoteName.Get(),
                (uint32_t)msg.GetType(),
                msg.GetSize(),
                (uint32_t)memoryStream.GetSize() );
}

// OnReceive
//------------------------------------------------------------------------------
/*virtual*/ void Client::OnReceive( const ConnectionInfo * connection, void * data, uint32_t size, bool & keepMemory )
{
    keepMemory = true; // we'll take care of freeing the memory

    MutexHolder mh( m_ServerListMutex );

    ServerState * ss = (ServerState *)connection->GetUserData();
    ASSERT( ss );

    // are we expecting a msg, or the payload for a msg?
    void * payload = nullptr;
    size_t payloadSize = 0;
    if ( ss->m_CurrentMessage == nullptr )
    {
        // message
        ss->m_CurrentMessage = static_cast< const Protocol::IMessage * >( data );
        if ( ss->m_CurrentMessage->HasPayload() )
        {
            return;
        }
    }
    else
    {
        // payload
        ASSERT( ss->m_CurrentMessage->HasPayload() );
        payload = data;
        payloadSize = size;
    }

    // determine message type
    const Protocol::IMessage * imsg = ss->m_CurrentMessage;
    Protocol::MessageType messageType = imsg->GetType();

    PROTOCOL_DEBUG( "Server -> Client : %u (%s)\n", messageType, GetProtocolMessageDebugName( messageType ) );

    switch ( messageType )
    {
        case Protocol::MSG_REQUEST_JOB:
        {
            const Protocol::MsgRequestJob * msg = static_cast< const Protocol::MsgRequestJob * >( imsg );
            Process( connection, msg );
            break;
        }
        case Protocol::MSG_JOB_RESULT:
        {
            const Protocol::MsgJobResult * msg = static_cast< const Protocol::MsgJobResult * >( imsg );
            Process( connection, msg, payload, payloadSize );
            break;
        }
        case Protocol::MSG_REQUEST_MANIFEST:
        {
            const Protocol::MsgRequestManifest * msg = static_cast< const Protocol::MsgRequestManifest * >( imsg );
            Process( connection, msg );
            break;
        }
        case Protocol::MSG_REQUEST_FILE:
        {
            const Protocol::MsgRequestFile * msg = static_cast< const Protocol::MsgRequestFile * >( imsg );
            Process( connection, msg );
            break;
        }
        case Protocol::MSG_SERVER_INFO:
        {
            const Protocol::MsgServerInfo * msg = static_cast< const Protocol::MsgServerInfo * >( imsg );
            Process( connection, msg, payload, payloadSize );
            break;
        }
        default:
        {
            // unknown message type
            ASSERT( false ); // this indicates a protocol bug
            DIST_INFO( "Protocol Error: %s\n", ss->m_RemoteName.Get() );
            Disconnect( connection );
            break;
        }
    }

    // free everything
    FREE( (void *)( ss->m_CurrentMessage ) );
    FREE( payload );
    ss->m_CurrentMessage = nullptr;
}

// Process( MsgRequestJob )
//------------------------------------------------------------------------------
void Client::Process( const ConnectionInfo * connection, const Protocol::MsgRequestJob * )
{
    PROFILE_SECTION( "MsgRequestJob" )

    ServerState * ss = (ServerState *)connection->GetUserData();
    ASSERT( ss );

    // no jobs for blacklisted or control-only workers
    if ( !ss->m_BuildJobsEnabled )
    {
        MutexHolder mh( ss->m_Mutex );
        Protocol::MsgNoJobAvailable msg;
        SendMessageInternal( connection, msg );
        return;
    }

    Job * job = JobQueue::Get().GetDistributableJobToProcess( true );
    if ( job == nullptr )
    {
        PROFILE_SECTION( "NoJob" )
        // tell the client we don't have anything right now
        // (we completed or gave away the job already)
        MutexHolder mh( ss->m_Mutex );
        Protocol::MsgNoJobAvailable msg;
        SendMessageInternal( connection, msg );
        return;
    }

    // send the job to the client
    MemoryStream stream;
    job->Serialize( stream );

    MutexHolder mh( ss->m_Mutex );

    ss->m_Jobs.Append( job ); // Track in-flight job

    // if tool is explicity specified, get the id of the tool manifest
    Node * n = job->GetNode()->CastTo< ObjectNode >()->GetCompiler();
    const ToolManifest & manifest = n->CastTo< CompilerNode >()->GetManifest();
    uint64_t toolId = manifest.GetToolId();
    ASSERT( toolId );

    // output to signify remote start
    FLOG_BUILD( "-> Obj: %s <REMOTE: %s>\n", job->GetNode()->GetName().Get(), ss->m_RemoteName.Get() );
    FLOG_MONITOR( "START_JOB %s \"%s\" \n", ss->m_RemoteName.Get(), job->GetNode()->GetName().Get() );

    {
        PROFILE_SECTION( "SendJob" )
        Protocol::MsgJob msg( toolId );
        SendMessageInternal( connection, msg, stream );
    }
}

// Process( MsgJobResult )
//------------------------------------------------------------------------------
void Client::Process( const ConnectionInfo * connection, const Protocol::MsgJobResult *, const void * payload, size_t payloadSize )
{
    PROFILE_SECTION( "MsgJobResult" )

    // find server
    ServerState * ss = (ServerState *)connection->GetUserData();
    ASSERT( ss );

    ConstMemoryStream ms( payload, payloadSize );

    uint32_t jobId = 0;
    ms.Read( jobId );

    AStackString<> name;
    ms.Read( name );

    bool result = false;
    ms.Read( result );

    bool systemError = false;
    ms.Read( systemError );

    Array< AString > messages;
    ms.Read( messages );

    uint32_t buildTime;
    ms.Read( buildTime );

    // get result data (built data or errors if failed)
    uint32_t size = 0;
    ms.Read( size );
    const void * data = (const char *)ms.GetData() + ms.Tell();

    {
        MutexHolder mh( ss->m_Mutex );
        VERIFY( ss->m_Jobs.FindDerefAndErase( jobId ) );
    }

    // Has the job been cancelled in the interim?
    // (Due to a Race by the main thread for example)
    Job * job = JobQueue::Get().OnReturnRemoteJob( jobId );
    if ( job == nullptr )
    {
        // don't save result as we were cancelled
        return;
    }

    DIST_INFO( "Got Result: %s - %s%s\n", ss->m_RemoteName.Get(),
                                          job->GetNode()->GetName().Get(),
                                          job->GetDistributionState() == Job::DIST_RACE_WON_REMOTELY ? " (Won Race)" : "" );

    job->SetMessages( messages );

    if ( result == true )
    {
        // built ok - serialize to disc
        MultiBuffer mb( data, ms.GetSize() - ms.Tell() );

        ObjectNode * objectNode = job->GetNode()->CastTo< ObjectNode >();
        const AString & nodeName = objectNode->GetName();
        if ( Node::EnsurePathExistsForFile( nodeName ) == false )
        {
            FLOG_ERROR( "Failed to create path for '%s'", nodeName.Get() );
            result = false;
        }
        else
        {
            size_t fileIndex = 0;

            const ObjectNode * on = job->GetNode()->CastTo< ObjectNode >();

            // 1. Object file
            result = WriteFileToDisk( nodeName, mb, fileIndex++ );

            // 2. PDB file (optional)
            if ( result && on->IsUsingPDB() )
            {
                AStackString<> pdbName;
                on->GetPDBName( pdbName );
                result = WriteFileToDisk( pdbName, mb, fileIndex++ );
            }

            // 3. .nativecodeanalysis.xml (optional)
            if ( result && on->IsUsingStaticAnalysisMSVC() )
            {
                AStackString<> xmlFileName;
                on->GetNativeAnalysisXMLPath( xmlFileName );
                result = WriteFileToDisk( xmlFileName, mb, fileIndex++ );
            }

            if ( result )
            {
                // record new file time
                objectNode->RecordStampFromBuiltFile();

                // record time taken to build
                objectNode->SetLastBuildTime( buildTime );
                objectNode->SetStatFlag(Node::STATS_BUILT);
                objectNode->SetStatFlag(Node::STATS_BUILT_REMOTE);

                // commit to cache?
                if ( FBuild::Get().GetOptions().m_UseCacheWrite &&
                        objectNode->ShouldUseCache() )
                {
                    objectNode->WriteToCache( job );
                }
            }
            else
            {
                objectNode->GetStatFlag( Node::STATS_FAILED );
            }
        }

        // get list of messages during remote work
        AStackString<> msgBuffer;
        job->GetMessagesForLog( msgBuffer );

        if ( objectNode->IsMSVC())
        {
            if ( objectNode->GetFlag( ObjectNode::FLAG_WARNINGS_AS_ERRORS_MSVC ) == false )
            {
                FileNode::HandleWarningsMSVC( job, objectNode->GetName(), msgBuffer.Get(), msgBuffer.GetLength() );
            }
        }
        else if ( objectNode->IsClang() || objectNode->IsGCC() )
        {
            if ( !objectNode->GetFlag( ObjectNode::FLAG_WARNINGS_AS_ERRORS_CLANGGCC ) )
            {
                FileNode::HandleWarningsClangGCC( job, objectNode->GetName(), msgBuffer.Get(), msgBuffer.GetLength() );
            }
        }
    }
    else
    {
        ((FileNode *)job->GetNode())->GetStatFlag( Node::STATS_FAILED );

        // failed - build list of errors
        const AString & nodeName = job->GetNode()->GetName();
        AStackString< 8192 > failureOutput;
        failureOutput.Format( "PROBLEM: %s\n", nodeName.Get() );
        for ( const AString * it = messages.Begin(); it != messages.End(); ++it )
        {
            failureOutput += *it;
        }

        // was it a system error?
        if ( systemError )
        {
            // blacklist misbehaving worker
            ss->m_BuildJobsEnabled = false;

            // take note of failure of job
            job->OnSystemError();

            // debugging message
            const size_t workerIndex = (size_t)( ss - m_ServerList.Begin() );
            const AString & workerName = m_WorkerList[ workerIndex ];
            DIST_INFO( "Remote System Failure!\n"
                       " - Blacklisted Worker: %s\n"
                       " - Node              : %s\n"
                       " - Job Error Count   : %u / %u\n"
                       " - Details           :\n"
                       "%s",
                       workerName.Get(),
                       job->GetNode()->GetName().Get(),
                       job->GetSystemErrorCount(), SYSTEM_ERROR_ATTEMPT_COUNT,
                       failureOutput.Get()
                      );

            // should we retry on another worker?
            if ( job->GetSystemErrorCount() < SYSTEM_ERROR_ATTEMPT_COUNT )
            {
                // re-queue job which will be re-attempted on another worker
                JobQueue::Get().ReturnUnfinishedDistributableJob( job );
                return;
            }

            // failed too many times on different workers, add info about this to
            // error output
            AStackString<> tmp;
            tmp.Format( "FBuild: Error: Task failed on %u different workers\n", (uint32_t)SYSTEM_ERROR_ATTEMPT_COUNT );
            if ( failureOutput.EndsWith( '\n' ) == false )
            {
                failureOutput += '\n';
            }
            failureOutput += tmp;
        }

        Node::DumpOutput( nullptr, failureOutput.Get(), failureOutput.GetLength(), nullptr );
    }

    if ( FLog::IsMonitorEnabled() )
    {
        AStackString<> msgBuffer;
        job->GetMessagesForMonitorLog( msgBuffer );

        FLOG_MONITOR( "FINISH_JOB %s %s \"%s\" \"%s\"\n",
                      result ? "SUCCESS" : "ERROR",
                      ss->m_RemoteName.Get(),
                      job->GetNode()->GetName().Get(),
                      msgBuffer.Get() );
    }

    JobQueue::Get().FinishedProcessingJob( job, result, true ); // remote job
}

// Process( MsgRequestManifest )
//------------------------------------------------------------------------------
void Client::Process( const ConnectionInfo * connection, const Protocol::MsgRequestManifest * msg )
{
    PROFILE_SECTION( "MsgRequestManifest" )

    // find a job associated with this client with this toolId
    const uint64_t toolId = msg->GetToolId();
    ASSERT( toolId );
    const ToolManifest * manifest = FindManifest( connection, toolId );

    if ( manifest == nullptr )
    {
        // client asked for a manifest that is not valid
        ASSERT( false ); // this indicates a logic bug
        Disconnect( connection );
        return;
    }

    MemoryStream ms;
    manifest->SerializeForRemote( ms );

    // Send manifest to worker
    Protocol::MsgManifest resultMsg( toolId );
    resultMsg.Send( connection, ms );
}

// Process ( MsgRequestFile )
//------------------------------------------------------------------------------
void Client::Process( const ConnectionInfo * connection, const Protocol::MsgRequestFile * msg )
{
    PROFILE_SECTION( "MsgRequestFile" )

    // find a job associated with this client with this toolId
    const uint64_t toolId = msg->GetToolId();
    ASSERT( toolId != 0 ); // server should not request 'no sync' tool id
    const ToolManifest * manifest = FindManifest( connection, toolId );

    if ( manifest == nullptr )
    {
        // client asked for a manifest that is not valid
        ASSERT( false ); // this indicates a logic bug
        Disconnect( connection );
        return;
    }

    const uint32_t fileId = msg->GetFileId();
    size_t dataSize( 0 );
    const void * data = manifest->GetFileData( fileId, dataSize );
    if ( !data )
    {
        ASSERT( false ); // something is terribly wrong
        Disconnect( connection );
        return;
    }

    ConstMemoryStream ms( data, dataSize );

    // Send file to worker
    Protocol::MsgFile resultMsg( toolId, fileId );
    resultMsg.Send( connection, ms );
}

// Process( MsgServerInfo )
//------------------------------------------------------------------------------
void Client::Process( const ConnectionInfo * connection, const Protocol::MsgServerInfo * msg, const void * payload, size_t payloadSize )
{
    PROFILE_SECTION( "MsgServerInfo" )

    // find server
    ServerState * ss = (ServerState *)connection->GetUserData();
    ASSERT( ss );

    // lock the server state
    MutexHolder mhSS( ss->m_Mutex );

    ss->m_InfoTimeStamp = Timer::GetNow();
    ss->m_InfoMode = msg->GetMode();
    ss->m_InfoNumClients = msg->GetNumClients();
    ss->m_InfoNumCPUTotal = msg->GetNumCPUTotal();
    ss->m_InfoNumCPUIdle = msg->GetNumCPUIdle();
    ss->m_InfoNumCPUBusy = msg->GetNumCPUBusy();
    ss->m_InfoNumBlockingProcesses = msg->GetNumBlockingProcesses();
    ss->m_InfoCPUUsageFASTBuild = msg->GetCPUUsageFASTBuild();
    ss->m_InfoCPUUsageTotal = msg->GetCPUUsageTotal();

    if (payloadSize > 0)
    {
        ConstMemoryStream ms( payload, payloadSize );
        size_t numCPUs = ss->m_InfoNumCPUTotal;
        ss->m_InfoWorkerIdle.SetSize( numCPUs );
        ss->m_InfoWorkerBusy.SetSize( numCPUs );
        ss->m_InfoHostNames.SetSize( numCPUs );
        ss->m_InfoJobStatus.SetSize( numCPUs );
        for ( size_t i=0; i<numCPUs; ++i )
        {
            ms.Read( ss->m_InfoWorkerIdle[i] );
            ms.Read( ss->m_InfoWorkerBusy[i] );
            ms.Read( ss->m_InfoHostNames[i] );
            ms.Read( ss->m_InfoJobStatus[i] );
        }
    }
    else
    {
        ss->m_InfoWorkerIdle.Clear();
        ss->m_InfoWorkerBusy.Clear();
        ss->m_InfoHostNames.Clear();
        ss->m_InfoJobStatus.Clear();
    }

    if (ss->m_ControlPendingResponse)
    {
        ss->m_ControlPendingResponse = false;
        ss->m_ControlSuccess = true;
        AtomicDecU32( &m_ControlPendingReceiveCounter );
    }
}

// FindManifest
//------------------------------------------------------------------------------
const ToolManifest * Client::FindManifest( const ConnectionInfo * connection, uint64_t toolId ) const
{
    ServerState * ss = (ServerState *)connection->GetUserData();
    ASSERT( ss );

    MutexHolder mh( ss->m_Mutex );

    for ( Job ** it = ss->m_Jobs.Begin();
          it != ss->m_Jobs.End();
          ++it )
    {
        Node * n = ( *it )->GetNode()->CastTo< ObjectNode >()->GetCompiler();
        const ToolManifest & m = n->CastTo< CompilerNode >()->GetManifest();
        if ( m.GetToolId() == toolId )
        {
            // found a job with the same toolid
            return &m;
        }
    }

    return nullptr;
}

// WriteFileToDisk
//------------------------------------------------------------------------------
bool Client::WriteFileToDisk( const AString & fileName, const MultiBuffer & multiBuffer, size_t index ) const
{
    if ( multiBuffer.ExtractFile( index, fileName ) == false )
    {
        FLOG_ERROR( "Failed to create file. Error: %s File: '%s'", LAST_ERROR_STR, fileName.Get() );
        return false;
    }
    return true;
}

// CONSTRUCTOR( ServerState )
//------------------------------------------------------------------------------
Client::ServerState::ServerState()
    : m_Connection( nullptr )
    , m_CurrentMessage( nullptr )
    , m_NumJobsAvailable( 0 )
    , m_Jobs( 16, true )
    , m_BuildJobsEnabled( false )
    , m_ControlEnabled( false )
    , m_ControlPendingSend( false )
    , m_ControlPendingResponse( false )
    , m_ControlSuccess( false )
    , m_ControlFailure( false )
    , m_InfoTimeStamp( 0 )
{
    m_DelayTimer.Start( 999.0f );
}

// WorkersSetMode
//------------------------------------------------------------------------------
void Client::WorkersSetCommandPending( const Array< AString > & workers)
{
    MutexHolder mh( m_ServerListMutex );
    // reset all success / failure flags
    for (ServerState * it = m_ServerList.Begin(), *end = m_ServerList.End(); it != end; ++it)
    {
        if (it->m_ControlEnabled)
        {
            MutexHolder ssMH( it->m_Mutex );
            it->m_ControlFailure = false;
            it->m_ControlSuccess = false;
            if (it->m_ControlPendingSend)
            {
                FLOG_ERROR( "Worker %s is still processing the previous command.", it->m_RemoteName.Get() );
                it->m_ControlPendingSend = false;
            }
            if (it->m_ControlPendingResponse)
            {
                FLOG_ERROR( "Worker %s is still waiting for the previous command response.", it->m_RemoteName.Get() );
                it->m_ControlPendingResponse = false;
            }
        }
    }
    // set the pending flags
    uint32_t count = 0;
    for ( const AString& worker : workers )
    {
        AString* workerListIt = m_WorkerList.Find( worker );
        if (workerListIt == nullptr)
        {
            FLOG_ERROR( "Worker %s is not in initial workers list.", worker.Get() );
            continue;
        }
        ServerState* ss = &(m_ServerList[workerListIt - m_WorkerList.Begin()]);
        MutexHolder mhss( ss->m_Mutex );
        if (!ss->m_ControlEnabled)
        {
            FLOG_ERROR( "Worker %s is not in initial control workers list.", worker.Get() );
            continue;
        }
        ss->m_ControlPendingSend = true;
        ++count;
    }
    // set the pending counter (starting the process on the main client thread)
    AtomicStoreRelease(&m_ControlPendingSendCounter, count);
}

// WorkersSetMode
//------------------------------------------------------------------------------
void Client::WorkersSetMode( const Array< AString > & workers, int32_t mode, int gracePeriod )
{
    WorkersGetLastCommandResult(); // wait for previous command to finish
    // we can safely change the message and flags, as no other thread are looking at them now
    m_ControlMessage = FNEW( Protocol::MsgSetMode( (uint8_t)mode, (uint16_t)gracePeriod ) );
    m_ControlMessageExpectResponse = false;
    // now ask selected workers to send the command
    WorkersSetCommandPending( workers );
}

// WorkersAddBlocking
//------------------------------------------------------------------------------
void Client::WorkersAddBlocking( const Array< AString > & workers, uint32_t pid, int gracePeriod )
{
    WorkersGetLastCommandResult();
    m_ControlMessage = FNEW( Protocol::MsgAddBlockingProcess( pid, (uint16_t)gracePeriod ) );
    m_ControlMessageExpectResponse = false;
    WorkersSetCommandPending( workers );
}

// WorkersRemoveBlocking
//------------------------------------------------------------------------------
void Client::WorkersRemoveBlocking( const Array< AString > & workers, uint32_t pid )
{
    WorkersGetLastCommandResult();
    m_ControlMessage = FNEW( Protocol::MsgRemoveBlockingProcess( pid ) );
    m_ControlMessageExpectResponse = false;
    WorkersSetCommandPending(workers);
}

// WorkersGetLastCommandResult
//------------------------------------------------------------------------------
bool Client::WorkersGetLastCommandResult( uint32_t timeoutMS )
{
    PROFILE_SECTION( "WorkersGetLastCommandResult" )

    uint32_t totalMs = 0;
    uint32_t waitMs = 1;
    while ((AtomicLoadRelaxed( &m_ControlPendingSendCounter ) || AtomicLoadRelaxed( &m_ControlPendingReceiveCounter )) && totalMs < timeoutMS)
    {
        Thread::Sleep( waitMs );
        totalMs += waitMs;
        waitMs = Math::Min( 100u, (waitMs*12+9)/10 ); // increase wait by 20% up to 100ms
    }

    int countTimeout = 0;
    int countSuccess = 0;
    int countFailures = 0;
    {
        MutexHolder mh( m_ServerListMutex );
        for (ServerState * it = m_ServerList.Begin(), *end = m_ServerList.End(); it != end; ++it)
        {
            if (it->m_ControlEnabled)
            {
                MutexHolder ssMH( it->m_Mutex );
                if (it->m_ControlPendingSend) // timeout while sending
                {
                    ++countTimeout;
                    it->m_ControlPendingSend = false;
                    it->m_ControlFailure = true;
                    AtomicDecU32( &m_ControlPendingSendCounter );
                }
                else if (it->m_ControlPendingResponse) // timeout while waiting for response
                {
                    ++countTimeout;
                    it->m_ControlPendingResponse = false;
                    it->m_ControlFailure = true;
                    AtomicDecU32( &m_ControlPendingReceiveCounter );
                }
                else if (it->m_ControlFailure)
                {
                    ++countFailures;
                }
                else if (it->m_ControlSuccess)
                {
                    ++countSuccess;
                }
            }
        }
    }
    DIST_INFO( "WorkersGetLastCommandResult: %d Success, %d Failures, %d timeouts\n", countSuccess, countFailures, countTimeout );

    return countFailures == 0 && countTimeout == 0;
}

// WorkersGatherInfo
//------------------------------------------------------------------------------
void Client::WorkersGatherInfo( int displayInfoLevel, Array< int >* numWorkerPerMode, int* numCPUTotal, int* numCPUIdle, int* numCPUBusy)
{
    PROFILE_SECTION( "WorkersGatherInfo" )
    MutexHolder mh( m_ServerListMutex );
    if (displayInfoLevel >= 1)
    {
        OUTPUT(     "|============|============|================================|====================|\n" );
        OUTPUT(     "|Worker      |Mode        |Threads +Busy -Idle *Disabled   |%% CPU +Worker *Local|\n" );
        if (displayInfoLevel >= 2)
            OUTPUT( "|         CPU|Client      |Status                                               |\n" );
        OUTPUT(     "|============|============|================================|====================|\n" );
    }
    else if (displayInfoLevel <= -1)
    { // json
        OUTPUT(     "[\n" );
    }
    int count = 0;
    for (ServerState * it = m_ServerList.Begin(), *end = m_ServerList.End(); it != end; ++it)
    {
        if (it->m_ControlEnabled)
        {
            MutexHolder ssMH( it->m_Mutex );
            if (it->m_ControlSuccess)
            {
                if (numWorkerPerMode)
                {
                    if (it->m_InfoMode >= numWorkerPerMode->GetSize()) numWorkerPerMode->SetSize(it->m_InfoMode+1);
                    (*numWorkerPerMode)[it->m_InfoMode] += 1;
                }
                if (numCPUTotal) *numCPUTotal += it->m_InfoNumCPUTotal;
                if (numCPUIdle) *numCPUIdle += it->m_InfoNumCPUIdle;
                if (numCPUBusy) *numCPUBusy += it->m_InfoNumCPUBusy;
                if (displayInfoLevel != 0)
                {
                    const char* modeStr;
                    switch ((WorkerSettings::Mode)it->m_InfoMode)
                    {
                        case WorkerSettings::DISABLED: modeStr = "disabled"; break;
                        case WorkerSettings::WHEN_IDLE: modeStr = "idle"; break;
                        case WorkerSettings::DEDICATED: modeStr = "dedicated"; break;
                        case WorkerSettings::PROPORTIONAL: modeStr = "proportional"; break;
                        default: modeStr = "unknown"; break;
                    }
                    if (displayInfoLevel >= 1)
                    {
                        AStackString<32> threadsStr;
                        AStackString<20> percentsStr;
                        int displayThreads = Math::Min(32,(int)it->m_InfoNumCPUTotal);
                        threadsStr.SetLength(displayThreads);
                        if (displayInfoLevel >= 2 && it->m_InfoWorkerBusy.GetSize() >= displayThreads)
                        { // we have detailed per-thread info
                            for (int i = 0; i < displayThreads; ++i)
                            {
                                threadsStr[i] = it->m_InfoWorkerBusy[i] ? '+' :
                                                it->m_InfoWorkerIdle[i] ? '-' : '*';
                            }
                        }
                        else
                        { // use the counts
                            for (int i = 0; i < displayThreads; ++i)
                            {
                                threadsStr[i] = (i < it->m_InfoNumCPUBusy) ? '+' :
                                                (i < it->m_InfoNumCPUBusy + it->m_InfoNumCPUIdle) ? '-' : '*';
                            }
                        }
                        const int displayPercents = 20;
                        percentsStr.SetLength(displayPercents);
                        for (int i = 0; i < displayPercents; ++i)
                        {
                            float percentsVal = (i+0.5f) * 100.0f / displayPercents;
                            percentsStr[i] = (percentsVal < it->m_InfoCPUUsageFASTBuild) ? '+' :
                                             (percentsVal < 100.0-(it->m_InfoCPUUsageTotal-it->m_InfoCPUUsageFASTBuild)) ? '-' : '*';
                        }
                        if (displayInfoLevel >= 2 && count > 0)
                            OUTPUT( "|------------|------------|--------------------------------|--------------------|\n" );
                        OUTPUT(     "|%-12.12s|%-12.12s|%-32.32s|%-20.20s|\n", it->m_RemoteName.Get(), modeStr, threadsStr.Get(), percentsStr.Get() );
                        if (displayInfoLevel >= 2 && it->m_InfoJobStatus.GetSize() > 0)
                        { // we have detailed per-thread info
                            for (int i = 0; i < it->m_InfoJobStatus.GetSize(); ++i)
                            {
                                OUTPUT(     "|         %3.3d|%-12.12s|%-53.53s|\n", i, it->m_InfoHostNames[i].Get(), it->m_InfoJobStatus[i].Get() );
                            }
                        }
                    }
                    else if (displayInfoLevel <= -1)
                    {
                        AStackString<> hostStr;
                        hostStr = it->m_RemoteName;
                        hostStr.Replace("\\", "\\\\");
                        hostStr.Replace("\"", "\\\"");
                        if (count > 0) OUTPUT( ",\n" );
                        OUTPUT( "  { \"worker\":\"%s\", \"mode\":\"%s\"", hostStr.Get(), modeStr);
                        OUTPUT( ", \"cpu_total\":%d, \"cpu_busy\":%d, \"cpu_idle\":%d", it->m_InfoNumCPUTotal, it->m_InfoNumCPUBusy, it->m_InfoNumCPUIdle);
                        OUTPUT( ", \"cpu_usage_total\":%f, \"cpu_usage_fastbuild\":%f", it->m_InfoCPUUsageTotal, it->m_InfoCPUUsageFASTBuild);
                        if (displayInfoLevel <= -2)
                        { // we have detailed per-thread info
                            OUTPUT( ",\n    \"jobs\":[" );
                            for (int i = 0; i < it->m_InfoJobStatus.GetSize(); ++i)
                            {
                                hostStr = it->m_InfoHostNames[i];
                                hostStr.Replace("\\", "\\\\");
                                hostStr.Replace("\"", "\\\"");
                                AStackString<> statusStr;
                                statusStr = it->m_InfoJobStatus[i].Get();
                                statusStr.Replace("\\", "\\\\");
                                statusStr.Replace("\"", "\\\"");
                                if (i > 0) OUTPUT(",\n            ");
                                OUTPUT( "{\"client\":\"%s\", \"status\":\"%s\"}", hostStr.Get(), statusStr.Get() );
                            }
                            OUTPUT( "]" );
                        }
                        OUTPUT( "}\n" );
                    }
                }
                ++count;
            }
        }
    }
    if (displayInfoLevel >= 1)
    {
        OUTPUT(     "|============|============|================================|====================|\n" );
    }
}

// WorkersDisplayInfo
//------------------------------------------------------------------------------
bool Client::WorkersDisplayInfo( const Array< AString > & workers, int32_t infoLevel )
{
    bool res = WorkersGetLastCommandResult();
    m_ControlMessage = FNEW( Protocol::MsgRequestServerInfo( (uint8_t)Math::Abs( infoLevel ) ) );
    m_ControlMessageExpectResponse = true;
    WorkersSetCommandPending( workers );
    res = WorkersGetLastCommandResult();
    WorkersGatherInfo( infoLevel );
    return res;
}

// WorkersWaitIdle
//------------------------------------------------------------------------------
bool Client::WorkersWaitIdle( const Array< AString > & workers, int32_t timeout, int infoLevel )
{
    Timer timeoutTimer;
    bool res = WorkersGetLastCommandResult( timeout == 0 ? 30000 : Math::Min( 30000, timeout*1000 ) );
    m_ControlMessage = FNEW( Protocol::MsgRequestServerInfo( (uint8_t)infoLevel ) );
    m_ControlMessageExpectResponse = true;
    Array< int > numWorkerPerMode;
    int numCPUTotal = 0;
    int numCPUIdle = 0;
    int numCPUBusy = 1;
    while (res && numCPUBusy > 0 && (timeout == 0 || timeoutTimer.GetElapsed() < timeout))
    {
        WorkersSetCommandPending( workers );
        res = WorkersGetLastCommandResult( timeout == 0 ? 30000 : Math::Clamp( timeout*1000 - (int)timeoutTimer.GetElapsedMS(), 0, 30000 ) );
        WorkersGatherInfo( infoLevel, &numWorkerPerMode, &numCPUTotal, &numCPUIdle, &numCPUBusy );
    }
    // TODO: wait for idle
    (void)timeout;
    return res;
}

//------------------------------------------------------------------------------
