// TestCache.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildTest.h"

// FBuild
#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/Graph/ObjectNode.h"
#include "Tools/FBuild/FBuildCore/Graph/SettingsNode.h"
#include "Tools/FBuild/FBuildCore/Protocol/Server.h"

// Core
#include "Core/FileIO/FileStream.h"
#include "Core/Profile/Profile.h"
#include "Core/Strings/AStackString.h"

// TestCache
//------------------------------------------------------------------------------
class TestCache : public FBuildTest
{
private:
    DECLARE_TESTS

    void Write() const;
    void Read() const;
    void ReadWrite() const;
    void ConsistentCacheKeysWithDist() const;

    void LightCache_IncludeUsingMacro() const;
    void LightCache_CyclicInclude() const;
    void LightCache_ImportDirective() const;

    // MSVC Static Analysis tests
    const char* const mAnalyzeMSVCBFFPath = "Tools/FBuild/FBuildTest/Data/TestCache/Analyze_MSVC/fbuild.bff";
    const char* const mAnalyzeMSVCXMLFile1 = "../tmp/Test/Cache/Analyze_MSVC/Analyze+WarningsOnly/file1.nativecodeanalysis.xml";
    const char* const mAnalyzeMSVCXMLFile2 = "../tmp/Test/Cache/Analyze_MSVC/Analyze+WarningsOnly/file2.nativecodeanalysis.xml";
    void Analyze_MSVC_WarningsOnly_Write() const;
    void Analyze_MSVC_WarningsOnly_Read() const;
    void Analyze_MSVC_WarningsOnly_WriteWithDistActive() const;
    void Analyze_MSVC_WarningsOnly_ReadWithDistActive() const;

    TestCache & operator = ( TestCache & other ) = delete; // Avoid warnings about implicit deletion of operators
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestCache )
    REGISTER_TEST( Write )
    REGISTER_TEST( Read )
    REGISTER_TEST( ReadWrite )
    REGISTER_TEST( ConsistentCacheKeysWithDist )
    #if defined( __WINDOWS__ )
        REGISTER_TEST( LightCache_IncludeUsingMacro )
        REGISTER_TEST( LightCache_CyclicInclude )
        REGISTER_TEST( LightCache_ImportDirective )
        REGISTER_TEST( Analyze_MSVC_WarningsOnly_Write )
        REGISTER_TEST( Analyze_MSVC_WarningsOnly_Read )
        REGISTER_TEST( Analyze_MSVC_WarningsOnly_WriteWithDistActive )
        REGISTER_TEST( Analyze_MSVC_WarningsOnly_ReadWithDistActive )
    #endif
REGISTER_TESTS_END

// Write
//------------------------------------------------------------------------------
void TestCache::Write() const
{
    FBuildTestOptions options;
    options.m_ForceCleanBuild = true;
    options.m_UseCacheWrite = true;
    options.m_CacheVerbose = true;

    // Normal caching using compiler's preprocessor
    size_t numDepsA = 0;
    {
        PROFILE_SECTION( "Normal" )

        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestCache/cache.bff";

        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );

        TEST_ASSERT( fBuild.Build( AStackString<>( "ObjectList" ) ) );

        // Ensure cache was written to
        const FBuildStats::Stats & objStats = fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE );
        TEST_ASSERT( objStats.m_NumCacheStores == objStats.m_NumProcessed );
        TEST_ASSERT( objStats.m_NumBuilt == objStats.m_NumProcessed );

        numDepsA = fBuild.GetRecursiveDependencyCount( "ObjectList" );
        TEST_ASSERT( numDepsA > 0 );
    }

    // Light cache
    #if defined( __WINDOWS__ )
        size_t numDepsB = 0;
        {
            PROFILE_SECTION( "Light" )

            options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestCache/lightcache.bff";

            FBuildForTest fBuild( options );
            TEST_ASSERT( fBuild.Initialize() );

            TEST_ASSERT( fBuild.Build( AStackString<>( "ObjectList" ) ) );

            // Ensure cache was written to
            const FBuildStats::Stats & objStats = fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE );
            TEST_ASSERT( objStats.m_NumCacheStores == objStats.m_NumProcessed );
            TEST_ASSERT( objStats.m_NumBuilt == objStats.m_NumProcessed );

            numDepsB = fBuild.GetRecursiveDependencyCount( "ObjectList" );
            TEST_ASSERT( numDepsB > 0 );
        }

        TEST_ASSERT( numDepsB >= numDepsA );

        // Ensure LightCache did not fail
        TEST_ASSERT( GetRecordedOutput().Find( "Light cache cannot be used for" ) == nullptr );
    #endif
}

// Read
//------------------------------------------------------------------------------
void TestCache::Read() const
{
    FBuildTestOptions options;
    options.m_ForceCleanBuild = true;
    options.m_UseCacheRead = true;
    options.m_CacheVerbose = true;

    // Normal caching using compiler's preprocessor
    size_t numDepsA = 0;
    {
        PROFILE_SECTION( "Normal" )

        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestCache/cache.bff";

        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );

        TEST_ASSERT( fBuild.Build( AStackString<>( "ObjectList" ) ) );

        // Ensure cache was written to
        const FBuildStats::Stats & objStats = fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE );
        TEST_ASSERT( objStats.m_NumCacheHits == objStats.m_NumProcessed );
        TEST_ASSERT( objStats.m_NumBuilt == 0 );

        numDepsA = fBuild.GetRecursiveDependencyCount( "ObjectList" );
        TEST_ASSERT( numDepsA > 0 );
    }

    // Light cache
    #if defined( __WINDOWS__ )
        size_t numDepsB = 0;
        {
            PROFILE_SECTION( "Light" )

            options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestCache/lightcache.bff";

            FBuildForTest fBuild( options );
            TEST_ASSERT( fBuild.Initialize() );

            TEST_ASSERT( fBuild.Build( AStackString<>( "ObjectList" ) ) );

            // Ensure cache was written to
            const FBuildStats::Stats & objStats = fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE );
            TEST_ASSERT( objStats.m_NumCacheHits == objStats.m_NumProcessed );
            TEST_ASSERT( objStats.m_NumBuilt == 0 );

            numDepsB = fBuild.GetRecursiveDependencyCount( "ObjectList" );
            TEST_ASSERT( numDepsB > 0 );
        }

        TEST_ASSERT( numDepsB >= numDepsA );

        // Ensure LightCache did not fail
        TEST_ASSERT( GetRecordedOutput().Find( "Light cache cannot be used for" ) == nullptr );
    #endif
}

// ReadWrite
//------------------------------------------------------------------------------
void TestCache::ReadWrite() const
{
    FBuildTestOptions options;
    options.m_ForceCleanBuild = true;
    options.m_UseCacheRead = true;
    options.m_UseCacheWrite = true;
    options.m_CacheVerbose = true;

    // Normal caching using compiler's preprocessor
    size_t numDepsA = 0;
    {
        PROFILE_SECTION( "Normal" )
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestCache/cache.bff";

        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );

        TEST_ASSERT( fBuild.Build( AStackString<>( "ObjectList" ) ) );

        // Ensure cache was written to
        const FBuildStats::Stats & objStats = fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE );
        TEST_ASSERT( objStats.m_NumCacheHits == objStats.m_NumProcessed );
        TEST_ASSERT( objStats.m_NumBuilt == 0 );

        numDepsA = fBuild.GetRecursiveDependencyCount( "ObjectList" );
        TEST_ASSERT( numDepsA > 0 );
    }

    // Light cache
    #if defined( __WINDOWS__ )
        size_t numDepsB = 0;
        {
            PROFILE_SECTION( "Light" )

            options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestCache/lightcache.bff";

            FBuildForTest fBuild( options );
            TEST_ASSERT( fBuild.Initialize() );

            TEST_ASSERT( fBuild.Build( AStackString<>( "ObjectList" ) ) );

            // Ensure cache was written to
            const FBuildStats::Stats & objStats = fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE );
            TEST_ASSERT( objStats.m_NumCacheHits == objStats.m_NumProcessed );
            TEST_ASSERT( objStats.m_NumBuilt == 0 );

            numDepsB = fBuild.GetRecursiveDependencyCount( "ObjectList" );
            TEST_ASSERT( numDepsB > 0 );
        }

        TEST_ASSERT( numDepsB >= numDepsA );

        // Ensure LightCache did not fail
        TEST_ASSERT( GetRecordedOutput().Find( "Light cache cannot be used for" ) == nullptr );
    #endif
}

// ConsistentCacheKeysWithDist
//------------------------------------------------------------------------------
void TestCache::ConsistentCacheKeysWithDist() const
{
    FBuildTestOptions options;
    options.m_CacheVerbose = true;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestCache/ConsistentCacheKeys/fbuild.bff";

    // Ensure compilation is performed "remotely"
    options.m_AllowDistributed = true;
    options.m_AllowLocalRace = false;
    options.m_NoLocalConsumptionOfRemoteJobs = true;

    // Write Only
    {
        options.m_UseCacheRead = false;
        options.m_UseCacheWrite = true;

        // Compile
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );

        Server s;
        s.Listen( Protocol::PROTOCOL_TEST_PORT );

        TEST_ASSERT( fBuild.Build( "ConsistentCacheKeys" ) );

        // Check for cache hit
        TEST_ASSERT( fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE ).m_NumCacheStores == 1 );
    }

    // Read Only
    {
        options.m_UseCacheRead = true;
        options.m_UseCacheWrite = false;

        // Compile with /analyze (warnings only)
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );

        Server s;
        s.Listen( Protocol::PROTOCOL_TEST_PORT );

        TEST_ASSERT( fBuild.Build( "ConsistentCacheKeys" ) );

        // Check for cache hit
        TEST_ASSERT( fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE ).m_NumCacheHits == 1 );
    }

    // Ensure that we used the same key for reading and writing the cache
    const AString & output = GetRecordedOutput();
    const char * store = output.Find( "Cache Store" );
    const char * hit = output.Find( "Cache Hit" );
    TEST_ASSERT( store && hit );
    const char * storeQuote1 = output.Find( '\'', store );
    const char * hitQuote1 = output.Find( '\'', hit );
    TEST_ASSERT( storeQuote1 && hitQuote1 );
    const char * storeQuote2 = output.Find( '\'', storeQuote1 + 1 );
    const char * hitQuote2 = output.Find( '\'', hitQuote1 + 1 );
    TEST_ASSERT( storeQuote2 && hitQuote2 );
    AStackString<> storeKey( storeQuote1 + 1, storeQuote2 );
    AStackString<> hitKey( hitQuote1 + 1, hitQuote2 );
    TEST_ASSERT( storeKey.IsEmpty() == false );
    TEST_ASSERT( storeKey == hitKey );
}

// LightCache_IncludeUsingMacro
//------------------------------------------------------------------------------
void TestCache::LightCache_IncludeUsingMacro() const
{
    FBuildTestOptions options;
    options.m_ForceCleanBuild = true;
    options.m_UseCacheWrite = true;
    options.m_CacheVerbose = true;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestCache/LightCache_IncludeUsingMacro/fbuild.bff";

    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    TEST_ASSERT( fBuild.Build( AStackString<>( "ObjectList" ) ) );

    // Ensure we detected that we could not use the LightCache
    TEST_ASSERT( GetRecordedOutput().Find( "Light cache cannot be used for" ) );

    // Ensure cache we fell back to normal caching
    const FBuildStats::Stats & objStats = fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE );
    TEST_ASSERT( objStats.m_NumCacheStores == 1 );
}

// LightCache_CyclicInclude
//------------------------------------------------------------------------------
void TestCache::LightCache_CyclicInclude() const
{
    FBuildTestOptions options;
    options.m_ForceCleanBuild = true;
    options.m_UseCacheWrite = true;
    options.m_CacheVerbose = true;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestCache/LightCache_CyclicInclude/fbuild.bff";

    {
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );

        TEST_ASSERT( fBuild.Build( AStackString<>( "ObjectList" ) ) );

        // Ensure cache we fell back to normal caching
        const FBuildStats::Stats & objStats = fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE );
        TEST_ASSERT( objStats.m_NumCacheStores == objStats.m_NumProcessed );
        TEST_ASSERT( objStats.m_NumBuilt == objStats.m_NumProcessed );
    }

    {
        options.m_UseCacheWrite = false;
        options.m_UseCacheRead = true;

        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );

        TEST_ASSERT( fBuild.Build( AStackString<>( "ObjectList" ) ) );

        // Ensure cache we fell back to normal caching
        const FBuildStats::Stats & objStats = fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE );
        TEST_ASSERT( objStats.m_NumCacheHits == objStats.m_NumProcessed );
        TEST_ASSERT( objStats.m_NumBuilt == 0 );
    }
}

// LightCache_ImportDirective
//------------------------------------------------------------------------------
void TestCache::LightCache_ImportDirective() const
{
    FBuildTestOptions options;
    options.m_ForceCleanBuild = true;
    options.m_UseCacheWrite = true;
    options.m_CacheVerbose = true;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestCache/LightCache_ImportDirective/fbuild.bff";

    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    TEST_ASSERT( fBuild.Build( AStackString<>( "ObjectList" ) ) );

    // Ensure we detected that we could not use the LightCache
    TEST_ASSERT( GetRecordedOutput().Find( "Light cache cannot be used for" ) );

    // Ensure cache we fell back to normal caching
    const FBuildStats::Stats & objStats = fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE );
    TEST_ASSERT( objStats.m_NumCacheStores == 1 );
}

// Analyze_MSVC_WarningsOnly_Write
//------------------------------------------------------------------------------
void TestCache::Analyze_MSVC_WarningsOnly_Write() const
{
    FBuildTestOptions options;
    options.m_ForceCleanBuild = true;
    options.m_UseCacheWrite = true;
    options.m_CacheVerbose = true;
    options.m_ConfigFile = mAnalyzeMSVCBFFPath;

    EnsureFileDoesNotExist( mAnalyzeMSVCXMLFile1 );
    EnsureFileDoesNotExist( mAnalyzeMSVCXMLFile2 );

    // Compile with /analyze (warnings only) (cache write)
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );
    TEST_ASSERT( fBuild.Build( "Analyze+WarningsOnly" ) );

    // Check for cache store
    TEST_ASSERT( fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE ).m_NumCacheStores == 2 );

    // Check for expected warnings
    const AString& output = GetRecordedOutput();
    // file1.cpp
    TEST_ASSERT( output.Find( "warning C6201" ) && output.Find( "Index '32' is out of valid index range" ) );
    TEST_ASSERT( output.Find( "warning C6386" ) && output.Find( "Buffer overrun while writing to 'buffer'" ) );
    // file2.cpp
    #if _MSC_VER >= 1910 // From VS2017 or later 
        TEST_ASSERT( output.Find( "warning C6387" ) && output.Find( "could be '0':  this does not adhere to the specification for the function" ) );
    #endif

    // Check analysis file is present with expected errors
    AString xml;
    LoadFileContentsAsString( mAnalyzeMSVCXMLFile1, xml );
    TEST_ASSERT( xml.Find( "<DEFECTCODE>6201</DEFECTCODE>" ) );
    TEST_ASSERT( xml.Find( "<DEFECTCODE>6386</DEFECTCODE>" ) );
    LoadFileContentsAsString( mAnalyzeMSVCXMLFile2, xml );
    #if _MSC_VER >= 1910 // From VS2017 or later 
        TEST_ASSERT( xml.Find( "<DEFECTCODE>6387</DEFECTCODE>" ) );
    #endif
}

// Analyze_MSVC_WarningsOnly_Read
//------------------------------------------------------------------------------
void TestCache::Analyze_MSVC_WarningsOnly_Read() const
{
    FBuildTestOptions options;
    options.m_ForceCleanBuild = true;
    options.m_UseCacheRead = true;
    options.m_CacheVerbose = true;
    options.m_ConfigFile = mAnalyzeMSVCBFFPath;

    EnsureFileDoesNotExist( mAnalyzeMSVCXMLFile1 );
    EnsureFileDoesNotExist( mAnalyzeMSVCXMLFile2 );

    // Compile with /analyze (warnings only) (cache read)
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );
    TEST_ASSERT( fBuild.Build( "Analyze+WarningsOnly" ) );

    // Check for cache hit
    TEST_ASSERT( fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE ).m_NumCacheHits == 2 );

    // NOTE: Process output will not contain warnings (as compilation was skipped)

    // Check analysis file is present with expected errors
    AString xml;
    LoadFileContentsAsString( mAnalyzeMSVCXMLFile1, xml );
    TEST_ASSERT( xml.Find( "<DEFECTCODE>6201</DEFECTCODE>" ) );
    TEST_ASSERT( xml.Find( "<DEFECTCODE>6386</DEFECTCODE>" ) );
    LoadFileContentsAsString( mAnalyzeMSVCXMLFile2, xml );
    #if _MSC_VER >= 1910 // From VS2017 or later 
        TEST_ASSERT( xml.Find( "<DEFECTCODE>6387</DEFECTCODE>" ) );
    #endif
}

// Analyze_MSVC_WarningsOnly_WriteWithDistActive
//------------------------------------------------------------------------------
void TestCache::Analyze_MSVC_WarningsOnly_WriteWithDistActive() const
{
    FBuildTestOptions options;
    options.m_ForceCleanBuild = true;
    options.m_UseCacheWrite = true;
    options.m_CacheVerbose = true;
    options.m_ConfigFile = mAnalyzeMSVCBFFPath;

    // Ensure compilation is performed "remotely"
    options.m_AllowDistributed = true;
    options.m_AllowLocalRace = false;
    options.m_NoLocalConsumptionOfRemoteJobs = true;

    EnsureFileDoesNotExist( mAnalyzeMSVCXMLFile1 );
    EnsureFileDoesNotExist( mAnalyzeMSVCXMLFile2 );

    // Compile with /analyze (warnings only)
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    Server s;
    s.Listen( Protocol::PROTOCOL_TEST_PORT );

    TEST_ASSERT( fBuild.Build( "Analyze+WarningsOnly" ) );

    // Check for cache hit
    TEST_ASSERT( fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE ).m_NumCacheStores == 2 );

    // Check for expected warnings
    const AString& output = GetRecordedOutput();
    // file1.cpp
    TEST_ASSERT( output.Find( "warning C6201" ) && output.Find( "Index '32' is out of valid index range" ) );
    TEST_ASSERT( output.Find( "warning C6386" ) && output.Find( "Buffer overrun while writing to 'buffer'" ) );
    // file2.cpp
    #if _MSC_VER >= 1910 // From VS2017 or later 
        TEST_ASSERT( output.Find( "warning C6387" ) && output.Find( "could be '0':  this does not adhere to the specification for the function" ) );
    #endif

    // Check analysis file is present with expected errors
    AString xml;
    LoadFileContentsAsString( mAnalyzeMSVCXMLFile1, xml );
    TEST_ASSERT( xml.Find( "<DEFECTCODE>6201</DEFECTCODE>" ) );
    TEST_ASSERT( xml.Find( "<DEFECTCODE>6386</DEFECTCODE>" ) );
    LoadFileContentsAsString( mAnalyzeMSVCXMLFile2, xml );
    #if _MSC_VER >= 1910 // From VS2017 or later 
        TEST_ASSERT( xml.Find( "<DEFECTCODE>6387</DEFECTCODE>" ) );
    #endif
}

// Analyze_MSVC_WarningsOnly_ReadWithDistActive
//------------------------------------------------------------------------------
void TestCache::Analyze_MSVC_WarningsOnly_ReadWithDistActive() const
{
    FBuildTestOptions options;
    options.m_ForceCleanBuild = true;
    options.m_UseCacheRead = true;
    options.m_CacheVerbose = true;
    options.m_ConfigFile = mAnalyzeMSVCBFFPath;

    // Ensure compilation is performed "remotely"
    options.m_AllowDistributed = true;
    options.m_AllowLocalRace = false;
    options.m_NoLocalConsumptionOfRemoteJobs = true;

    EnsureFileDoesNotExist( mAnalyzeMSVCXMLFile1 );
    EnsureFileDoesNotExist( mAnalyzeMSVCXMLFile2 );

    // Compile with /analyze (warnings only)
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    Server s;
    s.Listen( Protocol::PROTOCOL_TEST_PORT );

    TEST_ASSERT( fBuild.Build( "Analyze+WarningsOnly" ) );

    // Check for cache hit
    TEST_ASSERT( fBuild.GetStats().GetStatsFor( Node::OBJECT_NODE ).m_NumCacheHits == 2 );

    // NOTE: Process output will not contain warnings (as compilation was skipped)

    // Check analysis file is present with expected errors
    AString xml;
    LoadFileContentsAsString( mAnalyzeMSVCXMLFile1, xml );
    TEST_ASSERT( xml.Find( "<DEFECTCODE>6201</DEFECTCODE>" ) );
    TEST_ASSERT( xml.Find( "<DEFECTCODE>6386</DEFECTCODE>" ) );
    LoadFileContentsAsString( mAnalyzeMSVCXMLFile2, xml );
    #if _MSC_VER >= 1910 // From VS2017 or later 
        TEST_ASSERT( xml.Find( "<DEFECTCODE>6387</DEFECTCODE>" ) );
    #endif
}

//------------------------------------------------------------------------------
