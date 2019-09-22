// Client.h - Handles Client Side connections
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Containers/Array.h"
#include "Core/Containers/AutoPtr.h"
#include "Core/Network/TCPConnectionPool.h"
#include "Core/Process/Thread.h"
#include "Core/Strings/AString.h"
#include "Core/Time/Timer.h"

// Forward Declarations
//------------------------------------------------------------------------------
class Job;
class MemoryStream;
class MultiBuffer;
namespace Protocol
{
    class IMessage;
    class MsgJobResult;
    class MsgRequestJob;
    class MsgRequestManifest;
    class MsgRequestFile;
    class MsgServerInfo;
}
class ToolManifest;

// Client
//------------------------------------------------------------------------------
class Client : public TCPConnectionPool
{
public:
    Client( const Array< AString > & buildWorkerList,
            const Array< AString > & controlWorkerList,
            uint16_t port,
            uint32_t workerConnectionLimit,
            bool detailedLogging );
    ~Client();

    // Worker Control Commands
    // non-blocking (for a single command, firing a second one will wait for the first to complete)
    void WorkersSetMode( const Array< AString > & workers, int32_t mode, int gracePeriod = 0 );
    void WorkersAddBlocking( const Array< AString > & workers, uint32_t pid, int gracePeriod = 0 );
    void WorkersRemoveBlocking( const Array< AString > & workers, uint32_t pid );
    // blocking
    bool WorkersGetLastCommandResult( uint32_t timeoutMS = 30000);
    bool WorkersDisplayInfo( const Array< AString > & workers, int32_t infoLevel );
    bool WorkersWaitIdle( const Array< AString > & workers, int32_t timeout, int infoLevel = 0 );

private:
    virtual void OnDisconnected( const ConnectionInfo * connection );
    virtual void OnReceive( const ConnectionInfo * connection, void * data, uint32_t size, bool & keepMemory );

    void Process( const ConnectionInfo * connection, const Protocol::MsgRequestJob * msg );
    void Process( const ConnectionInfo * connection, const Protocol::MsgJobResult *, const void * payload, size_t payloadSize );
    void Process( const ConnectionInfo * connection, const Protocol::MsgRequestManifest * msg );
    void Process( const ConnectionInfo * connection, const Protocol::MsgRequestFile * msg );
    void Process( const ConnectionInfo * connection, const Protocol::MsgServerInfo * msg, const void * payload, size_t payloadSize );

    const ToolManifest * FindManifest( const ConnectionInfo * connection, uint64_t toolId ) const;
    bool WriteFileToDisk( const AString& fileName, const MultiBuffer & multiBuffer, size_t index ) const;

    static uint32_t ThreadFuncStatic( void * param );
    void            ThreadFunc();

    void            LookForWorkers();
    void            CommunicateJobAvailability();
    void            CommunicateCommands();

    // More verbose name to avoid conflict with windows.h SendMessage
    void            SendMessageInternal( const ConnectionInfo * connection, const Protocol::IMessage & msg );
    void            SendMessageInternal( const ConnectionInfo * connection, const Protocol::IMessage & msg, const MemoryStream & memoryStream );

    void WorkersSetCommandPending( const Array< AString > & workers);
    void WorkersGatherInfo( int displayInfoLevel, Array< int >* numWorkerPerMode = nullptr, int* numCPUTotal = nullptr, int* numCPUIdle = nullptr, int* numCPUBusy = nullptr);

    Array< AString >    m_WorkerList;   // workers to connect to
    volatile bool       m_ShouldExit;   // signal from main thread
    bool                m_DetailedLogging;
    Thread::ThreadHandle m_Thread;      // the thread to find and manage workers

    // state
    Timer               m_StatusUpdateTimer;

    // control command message
    volatile uint32_t m_ControlPendingSendCounter;
    volatile uint32_t m_ControlPendingReceiveCounter;
    AutoPtr< Protocol::IMessage, DeleteDeletor > m_ControlMessage;
    AutoPtr< MemoryStream, DeleteDeletor > m_ControlMessagePayload;
    bool m_ControlMessageExpectResponse;

    struct ServerState
    {
        explicit ServerState();

        const ConnectionInfo *  m_Connection;
        AString                 m_RemoteName;

        Mutex                   m_Mutex;
        const Protocol::IMessage * m_CurrentMessage;
        Timer                   m_DelayTimer;
        uint32_t                m_NumJobsAvailable;     // num jobs we've told this server we have available
        Array< Job * >          m_Jobs;                 // jobs we've sent to this server

        bool                    m_BuildJobsEnabled;
        bool                    m_ControlEnabled;

        bool  m_ControlPendingSend; // true if a control command should be sent to this worker
        bool  m_ControlPendingResponse; // true if a control command has been sent but we are waiting for the response
        bool  m_ControlSuccess; // true if the last control command was sent successfully
        bool  m_ControlFailure; // true if the last control command failed to be sent
        // server info received from last MsgServerInfo message
        uint64_t m_InfoTimeStamp;
        uint8_t  m_InfoMode;
        uint16_t m_InfoNumClients;
        uint16_t m_InfoNumCPUTotal;
        uint16_t m_InfoNumCPUIdle;
        uint16_t m_InfoNumCPUBusy;
        uint16_t m_InfoNumBlockingProcesses;
        float    m_InfoCPUUsageFASTBuild;
        float    m_InfoCPUUsageTotal;
        Array<bool> m_InfoWorkerIdle;
        Array<bool> m_InfoWorkerBusy;
        Array<AString> m_InfoHostNames;
        Array<AString> m_InfoJobStatus;
    };
    Mutex                   m_ServerListMutex;
    Array< ServerState >    m_ServerList;
    uint32_t                m_WorkerConnectionLimit;
    uint16_t                m_Port;
};

//------------------------------------------------------------------------------
