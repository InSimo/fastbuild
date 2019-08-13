// Protocol.h - Defines network communication protocol
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/MSVCStaticAnalysis.h"
#include "Core/Env/Types.h"

// Forward Declarations
//------------------------------------------------------------------------------
class ConnectionInfo;
class ConstMemoryStream;
class MemoryStream;
class TCPConnectionPool;

// Defines
//------------------------------------------------------------------------------
//#define PROTOCOL_DEBUG_ENABLED // uncomment this for protocol spam
#ifdef PROTOCOL_DEBUG_ENABLED
    #include "Core/Tracing/Tracing.h"
    #define PROTOCOL_DEBUG( ... ) DEBUGSPAM( __VA_ARGS__ )
#else
    #define PROTOCOL_DEBUG( ... ) (void)0
#endif

// Protocol
//------------------------------------------------------------------------------
namespace Protocol
{
    enum : uint16_t { PROTOCOL_PORT = 31264 }; // Arbitrarily chosen port
    enum { PROTOCOL_VERSION = 21 };

    enum { PROTOCOL_TEST_PORT = PROTOCOL_PORT + 1 }; // Different port for use by tests

    // Identifiers for all unique messages
    //------------------------------------------------------------------------------
    enum MessageType
    {
        MSG_CONNECTION          = 1, // Server <- Client : Initial handshake
        MSG_STATUS              = 2, // Server <- Client : Update status (work available)

        MSG_REQUEST_JOB         = 3, // Server -> Client : Ask for a job to do
        MSG_NO_JOB_AVAILABLE    = 4, // Server <- Client : Respond that no jobs are available
        MSG_JOB                 = 5, // Server <- Client : Respond with a job to do

        MSG_JOB_RESULT          = 6, // Server -> Client : Return completed job

        MSG_REQUEST_MANIFEST    = 7, // Server -> Client : Ask client for the manifest of tools required for a job
        MSG_MANIFEST            = 8, // Server <- Client : Respond with manifest details

        MSG_REQUEST_FILE        = 9, // Server -> Client : Ask client for a file
        MSG_FILE                = 10,// Server <- Client : Send a requested file

        MSG_REQUEST_SERVER_INFO = 11, // Server <- Client : Request info on current server state (mode, jobs)
        MSG_SERVER_INFO         = 12, // Server -> Client : Repond to info request
        MSG_SET_MODE            = 13, // Server <- Client : Change the current mode
        MSG_ADD_BLOCKING_PROCESS = 14, // Server <- Client : Change the current mode
        MSG_REMOVE_BLOCKING_PROCESS = 15, // Server <- Client : Change the current mode

        NUM_MESSAGES            // leave last
    };
};

#ifdef PROTOCOL_DEBUG_ENABLED
    const char * GetProtocolMessageDebugName( Protocol::MessageType msgType );
#endif

namespace Protocol
{
    // base class for all messages
    //------------------------------------------------------------------------------
    class IMessage
    {
    public:
        bool Send( const ConnectionInfo * connection ) const;
        bool Send( const ConnectionInfo * connection, const MemoryStream & payload ) const;
        bool Send( const ConnectionInfo * connection, const ConstMemoryStream & payload ) const;
        bool Broadcast( TCPConnectionPool * pool ) const;

        inline MessageType  GetType() const { return m_MsgType; }
        inline uint32_t     GetSize() const { return m_MsgSize; }
        inline bool         HasPayload() const { return m_HasPayload; }

    protected:
        IMessage( MessageType msgType, uint32_t msgSize, bool hasPayload );

        // properties common to all messages
        MessageType     m_MsgType;
        uint32_t        m_MsgSize;
        bool            m_HasPayload;
        char            m_Padding1[ 3 ];
    };
    static_assert( sizeof( IMessage ) == 9 + 3/*padding*/, "Message base class has incorrect size" );

    // MsgConnection
    //------------------------------------------------------------------------------
    class MsgConnection : public IMessage
    {
    public:
        explicit MsgConnection( uint32_t numJobsAvailable );

        inline uint32_t GetProtocolVersion() const { return m_ProtocolVersion; }
        inline uint32_t GetNumJobsAvailable() const { return m_NumJobsAvailable; }
        inline uint8_t  GetPlatform() const { return m_Platform; }
        const char * GetHostName() const { return m_HostName; }
    private:
        uint32_t        m_ProtocolVersion;
        uint32_t        m_NumJobsAvailable;
        uint8_t         m_Platform;
        uint8_t         m_Padding2[3];
        char            m_HostName[ 64 ];
    };
    static_assert( sizeof( MsgConnection ) == sizeof( IMessage ) + 76, "MsgConnection message has incorrect size" );

    // MsgStatus
    //------------------------------------------------------------------------------
    class MsgStatus : public IMessage
    {
    public:
        explicit MsgStatus( uint32_t numJobsAvailable );

        inline uint32_t GetNumJobsAvailable() const { return m_NumJobsAvailable; }
    private:
        uint32_t        m_NumJobsAvailable;
    };
    static_assert( sizeof( MsgStatus ) == sizeof( IMessage ) + 4, "MsgStatus message has incorrect size" );

    // MsgRequestJob
    //------------------------------------------------------------------------------
    class MsgRequestJob : public IMessage
    {
    public:
        MsgRequestJob();
    };
    static_assert( sizeof( MsgRequestJob ) == sizeof( IMessage ), "MsgRequestJob message has incorrect size" );

    // MsgNoJobAvailable
    //------------------------------------------------------------------------------
    class MsgNoJobAvailable : public IMessage
    {
    public:
        MsgNoJobAvailable();
    };
    static_assert( sizeof( MsgNoJobAvailable ) == sizeof( IMessage ), "MsgNoJobAvailable message has incorrect size" );

    // MsgJob
    //------------------------------------------------------------------------------
    class MsgJob : public IMessage
    {
    public:
        explicit MsgJob( uint64_t toolId );

        inline uint64_t GetToolId() const { return m_ToolId; }
    private:
        char     m_Padding2[ 4 ];
        uint64_t m_ToolId;
    };
    static_assert( sizeof( MsgJob ) == sizeof( IMessage ) + 4/*alignment*/ + 8, "MsgJob message has incorrect size" );

    // MsgJobResult
    //------------------------------------------------------------------------------
    class MsgJobResult : public IMessage
    {
    public:
        MsgJobResult();
    };
    static_assert( sizeof( MsgJobResult ) == sizeof( IMessage ), "MsgJobResult message has incorrect size" );

    // MsgRequestManifest
    //------------------------------------------------------------------------------
    class MsgRequestManifest : public IMessage
    {
    public:
        explicit MsgRequestManifest( uint64_t toolId );

        inline uint64_t GetToolId() const { return m_ToolId; }
    private:
        char     m_Padding2[ 4 ];
        uint64_t m_ToolId;
    };
    static_assert( sizeof( MsgRequestManifest ) == sizeof( IMessage ) + 4/*alignment*/ + 8, "MsgRequestManifest message has incorrect size" );

    // MsgManifest
    //------------------------------------------------------------------------------
    class MsgManifest : public IMessage
    {
    public:
        explicit MsgManifest( uint64_t toolId );

        inline uint64_t GetToolId() const { return m_ToolId; }
    private:
        char     m_Padding2[ 4 ];
        uint64_t m_ToolId;
    };
    static_assert( sizeof( MsgManifest ) == sizeof( IMessage ) + 4/*alignment*/ + 8, "MsgManifest message has incorrect size" );

    // MsgRequestFile
    //------------------------------------------------------------------------------
    class MsgRequestFile : public IMessage
    {
    public:
        MsgRequestFile( uint64_t toolId, uint32_t fileId );

        inline uint64_t GetToolId() const { return m_ToolId; }
        inline uint32_t GetFileId() const { return m_FileId; }
    private:
        uint32_t m_FileId;
        uint64_t m_ToolId;
    };
    static_assert( sizeof( MsgRequestFile ) == sizeof( IMessage ) + 12, "MsgRequestFile message has incorrect size" );

    // MsgFile
    //------------------------------------------------------------------------------
    class MsgFile : public IMessage
    {
    public:
        MsgFile( uint64_t toolId, uint32_t fileId );

        inline uint64_t GetToolId() const { return m_ToolId; }
        inline uint32_t GetFileId() const { return m_FileId; }
    private:
        uint32_t m_FileId;
        uint64_t m_ToolId;
    };
    static_assert( sizeof( MsgFile ) == sizeof( IMessage ) + 12, "MsgFile message has incorrect size" );

    // MsgServerStatus
    //------------------------------------------------------------------------------
    class MsgServerStatus : public IMessage
    {
    public:
        MsgServerStatus();
    };
    static_assert( sizeof( MsgServerStatus ) == sizeof( IMessage ), "MsgServerStatus message has incorrect size" );

    // MsgRequestServerInfo
    //------------------------------------------------------------------------------
    class MsgRequestServerInfo : public IMessage
    {
    public:
        MsgRequestServerInfo( uint8_t detailsLevel );

        inline uint8_t GetDetailsLevel() const { return m_DetailsLevel; }
    private:
        uint8_t m_DetailsLevel;
        char    m_Padding2[ 3 ];
    };
    static_assert( sizeof( MsgRequestServerInfo ) == sizeof( IMessage ) + 1 + 3, "MsgRequestServerInfo message has incorrect size" );

    // MsgServerInfo
    //------------------------------------------------------------------------------
    class MsgServerInfo : public IMessage
    {
    public:
        MsgServerInfo( uint8_t mode, uint16_t numClients, uint16_t numCPUTotal,
                       uint16_t numCPUAvailable, uint16_t numCPUBusy, uint16_t numBlockingProcesses,
                       float cpuUsageFASTBuild, float cpuUsageTotal );

        inline uint8_t  GetMode() const { return m_Mode; }
        inline uint16_t GetNumCPUTotal() const { return m_NumCPUTotal; }
        inline uint16_t GetNumCPUAvailable() const { return m_NumCPUAvailable; }
        inline uint16_t GetNumCPUBusy() const { return m_NumCPUBusy; }
        inline uint16_t GetNumClients() const { return m_NumClients; }
        inline uint16_t GetNumBlockingProcesses() const { return m_NumBlockingProcesses; }
        inline float    GetCPUUsageFASTBuild() const { return m_CPUUsageFASTBuild; }
        inline float    GetCPUUsageTotal() const { return m_CPUUsageTotal; }
    private:
        uint8_t  m_Mode;
        char     m_Padding2[ 1 ];
        uint16_t m_NumClients;
        uint16_t m_NumCPUTotal;
        uint16_t m_NumCPUAvailable;
        uint16_t m_NumCPUBusy;
        uint16_t m_NumBlockingProcesses;
        float    m_CPUUsageFASTBuild;
        float    m_CPUUsageTotal;
    };
    static_assert( sizeof( MsgServerInfo ) == sizeof( IMessage ) + 1 + 1 + 5*2 + 2*4, "MsgServerInfo message has incorrect size" );

    // MsgSetMode
    //------------------------------------------------------------------------------
    class MsgSetMode : public IMessage
    {
    public:
        MsgSetMode( uint8_t mode, uint16_t gracePeriod );

        inline uint8_t  GetMode() const { return m_Mode; }
        inline uint16_t GetGracePeriod() const { return m_GracePeriod; }
    private:
        uint8_t  m_Mode;
        char     m_Padding2[ 1 ];
        uint16_t m_GracePeriod;
    };
    static_assert( sizeof( MsgSetMode ) == sizeof( IMessage ) + 1 + 1 + 2, "MsgSetMode message has incorrect size" );

    // MsgAddBlockingProcess
    //------------------------------------------------------------------------------
    class MsgAddBlockingProcess : public IMessage
    {
    public:
        MsgAddBlockingProcess( uint32_t pid, uint16_t gracePeriod );

        inline uint32_t GetPid() const { return m_Pid; }
        inline uint16_t GetGracePeriod() const { return m_GracePeriod; }
    private:
        uint32_t m_Pid;
        uint16_t m_GracePeriod;
        uint8_t  m_Padding2[2];
    };
    static_assert( sizeof( MsgAddBlockingProcess ) == sizeof( IMessage ) + 4 + 2 + 2, "MsgAddBlockingProcess message has incorrect size" );

    // MsgRemoveBlockingProcess
    //------------------------------------------------------------------------------
    class MsgRemoveBlockingProcess : public IMessage
    {
    public:
        MsgRemoveBlockingProcess( uint32_t pid );

        inline uint32_t GetPid() const { return m_Pid; }
    private:
        uint32_t m_Pid;
    };
    static_assert( sizeof( MsgRemoveBlockingProcess ) == sizeof( IMessage ) + 4, "MsgRemoveBlockingProcess message has incorrect size" );
};

//------------------------------------------------------------------------------
