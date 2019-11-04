// IdleDetection
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Containers/Array.h"
#include "Core/Containers/Singleton.h"
#include "Core/Env/Types.h"
#include "Core/Strings/AStackString.h"
#include "Core/Time/Timer.h"

// Forward Declarations
//------------------------------------------------------------------------------

// WorkerWindow
//------------------------------------------------------------------------------
class IdleDetection : public Singleton< IdleDetection >
{
public:
    explicit IdleDetection();
    ~IdleDetection();

    // returns true if idle
    void Update( const Array<AString>& blockingProcessNames, const Array<uint32_t>& addedBlockingPid, const Array<uint32_t>& removedBlockingPid );

    // query status
    inline bool IsIdle() const { return m_IsIdle; }
    inline float IsIdleFloat() const { return m_IsIdleFloat; }
    inline bool IsBlocked() const { return m_IsBlocked; }
    uint32_t GetNumBlockingProcesses() const { return (uint32_t)m_BlockingProcesses.GetSize(); }

    // query status
    inline bool AddBlockingProcess(uint32_t pid);
    inline bool RemoveBlockingProcess(uint32_t pid);
    inline float GetCPUUsageFASTBuild() const { return m_CPUUsageFASTBuild; }
    inline float GetCPUUsageTotal() const { return m_CPUUsageTotal; }

    struct BlockingProcessInfo
    {
        inline bool operator == ( uint32_t pid ) const { return m_PID == pid; }
        inline bool operator < ( uint32_t pid ) const { return m_PID < pid; }
        inline bool operator < ( const BlockingProcessInfo& b ) const { return m_PID < b.m_PID; }
        uint32_t    m_PID;
        AString     m_Name;
    };

    const SortedArray< BlockingProcessInfo >& GetBlockingProcesses() const { return m_BlockingProcesses; }

private:
    // struct to track processes with
    struct ProcessInfo
    {
        inline bool operator == ( uint32_t pid ) const { return m_PID == pid; }
        inline bool operator < ( uint32_t pid ) const { return m_PID < pid; }
        inline bool operator < ( const ProcessInfo& b ) const { return m_PID < b.m_PID; }
        enum : uint16_t
        {
            FLAG_SELF             = 1 << 0,
            FLAG_IN_OUR_HIERARCHY = 1 << 1,
            FLAG_BLOCKING         = 1 << 2,
        };
        uint32_t    m_PID;
        uint16_t    m_AliveValue;
        uint16_t    m_Flags;
        #if defined( __WINDOWS__ )
            void *      m_ProcessHandle;
        #endif
        uint64_t    m_LastTime;
    };

    bool IsIdleInternal( float & idleCurrent, const Array<AString>& blockingProcessNames, const Array<uint32_t>& addedBlockingPid, const Array<uint32_t>& removedBlockingPid );

    static void GetSystemTotalCPUUsage( uint64_t & outIdleTime,
                                        uint64_t & outKernTime,
                                        uint64_t & outUserTime );
    static void GetProcessTime( const ProcessInfo & pi,
                                uint64_t & outKernTime,
                                uint64_t & outUserTime );
    void UpdateProcessList( const Array<AString>& blockingProcessNames, const Array<uint32_t>& addedBlockingPid, const Array<uint32_t>& removedBlockingPid );
    bool IsBlocking( const char* processName, const Array<AString>& blockingProcessNames );

    Timer   m_Timer;
    float   m_CPUUsageFASTBuild;
    float   m_CPUUsageTotal;
    bool    m_IsIdle;
    float   m_IsIdleFloat;
    float   m_IsIdleCurrent;
    int32_t m_IdleSmoother;
    int32_t m_IdleFloatSmoother;
    bool    m_IsBlocked;
    SortedArray< ProcessInfo > m_Processes;
    SortedArray< BlockingProcessInfo > m_BlockingProcesses;
    uint64_t m_LastTimeIdle;
    uint64_t m_LastTimeBusy;
};

//------------------------------------------------------------------------------
