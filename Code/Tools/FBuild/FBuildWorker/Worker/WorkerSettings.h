// WorkerSettings
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Containers/Singleton.h"
#include "Core/Containers/Array.h"
#include "Core/Strings/AString.h"

// Forward Declarations
//------------------------------------------------------------------------------

// WorkerSettings
//------------------------------------------------------------------------------
class WorkerSettings : public Singleton< WorkerSettings >
{
public:
    explicit WorkerSettings();
    ~WorkerSettings();

    // Worker Mode
    enum Mode
    {
        DISABLED        = 0, // Don't work for anyone
        WHEN_IDLE       = 1, // Work for others when idle
        DEDICATED       = 2, // Work for others always
        PROPORTIONAL    = 3  // Work for others proportional to free CPU
    };
    inline Mode GetMode() const { return m_Mode; }
    void SetMode( Mode m );

    // CPU Usage limits
    inline uint32_t GetNumCPUsToUse() const { return m_NumCPUsToUse; }
    void SetNumCPUsToUse( uint32_t c );

    // Start minimzed
    void SetStartMinimized( bool startMinimized );
    inline bool GetStartMinimzed() const { return m_StartMinimized; }

    // Period in seconds to wait for running jobs to finish before killing them
    // (when going to Disabled mode or no longer Idle)
    // 0 means never kill running jobs
    void SetGracePeriod( uint32_t gracePeriod );
    inline uint32_t GetGracePeriod() const { return m_GracePeriod; }

    // Names of local processes that will stop jobs from being executed
    void SetBlockingProcessNames( const Array<AString>& blockingProcessNames );
    inline const Array<AString>& GetBlockingProcessNames() const { return m_BlockingProcessNames; }

    // Period in seconds to wait for running jobs to finish before killing them
    // (when a blocking process appears)
    // 0 means never kill running jobs
    void SetBlockingGracePeriod( uint32_t blockingGracePeriod );
    inline uint32_t GetBlockingGracePeriod() const { return m_BlockingGracePeriod; }

    void Load();
    void Save();
private:
    Mode        m_Mode;
    uint32_t    m_NumCPUsToUse;
    bool        m_StartMinimized;
    uint32_t    m_GracePeriod;
    Array<AString> m_BlockingProcessNames;
    uint32_t    m_BlockingGracePeriod;
};

//------------------------------------------------------------------------------
