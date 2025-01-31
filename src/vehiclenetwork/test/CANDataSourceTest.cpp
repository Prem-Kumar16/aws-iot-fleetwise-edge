
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "businterfaces/CANDataSource.h"
#include "WaitUntil.h"
#include <functional>
#include <gtest/gtest.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unordered_set>

using namespace Aws::IoTFleetWise::TestingSupport;
using namespace Aws::IoTFleetWise::VehicleNetwork;

static void
cleanUp( int socketFD )
{
    close( socketFD );
}

static int
setup( bool fd = false )
{
    // Setup a socket
    std::string socketCANIFName( "vcan0" );
    struct sockaddr_can interfaceAddress;
    struct ifreq interfaceRequest;

    int type = SOCK_RAW | SOCK_NONBLOCK;
    int socketFD = socket( PF_CAN, type, CAN_RAW );
    if ( socketFD < 0 )
    {
        return -1;
    }
    if ( fd )
    {
        int canfd_on = 1;
        if ( setsockopt( socketFD, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canfd_on, sizeof( canfd_on ) ) != 0 )
        {
            return -1;
        }
    }

    if ( socketCANIFName.size() >= sizeof( interfaceRequest.ifr_name ) )
    {
        cleanUp( socketFD );
        return -1;
    }
    (void)strncpy( interfaceRequest.ifr_name, socketCANIFName.c_str(), sizeof( interfaceRequest.ifr_name ) - 1U );

    if ( ioctl( socketFD, SIOCGIFINDEX, &interfaceRequest ) )
    {
        cleanUp( socketFD );
        return -1;
    }

    memset( &interfaceAddress, 0, sizeof( interfaceAddress ) );
    interfaceAddress.can_family = AF_CAN;
    interfaceAddress.can_ifindex = interfaceRequest.ifr_ifindex;

    if ( bind( socketFD, (struct sockaddr *)&interfaceAddress, sizeof( interfaceAddress ) ) < 0 )
    {
        cleanUp( socketFD );
        return -1;
    }

    return socketFD;
}

class LocalDataSourceEventListener : public VehicleDataSourceListener
{
public:
    LocalDataSourceEventListener()
        : gotConnectCallback( false )
        , gotDisConnectCallback( false )
    {
    }

    inline void
    onVehicleDataSourceConnected( const VehicleDataSourceID &Id )
    {
        static_cast<void>( Id ); // Ignore parameter
        gotConnectCallback = true;
    }

    inline void
    onVehicleDataSourceDisconnected( const VehicleDataSourceID &Id )
    {
        static_cast<void>( Id ); // Ignore parameter
        gotDisConnectCallback = true;
    }

    bool gotConnectCallback;
    bool gotDisConnectCallback;
};

static bool
sendTestMessage( int socketFD )
{
    struct can_frame frame = {};
    frame.can_id = 0x123;
    frame.can_dlc = 4;
    for ( uint8_t i = 0; i < 3; ++i )
    {
        frame.data[i] = i;
    }
    ssize_t bytesWritten = write( socketFD, &frame, sizeof( struct can_frame ) );
    EXPECT_EQ( bytesWritten, sizeof( struct can_frame ) );
    return true;
}

static bool
sendTestFDMessage( int socketFD )
{
    struct canfd_frame frame = {};
    frame.can_id = 0x123;
    frame.len = 64;
    for ( uint8_t i = 0; i < 64; ++i )
    {
        frame.data[i] = i;
    }
    ssize_t bytesWritten = write( socketFD, &frame, sizeof( struct canfd_frame ) );
    EXPECT_EQ( bytesWritten, sizeof( struct canfd_frame ) );
    return true;
}

static bool
sendTestMessageExtendedID( int socketFD )
{
    struct can_frame frame = {};
    frame.can_id = 0x123 | CAN_EFF_FLAG;

    frame.can_dlc = 4;
    for ( uint8_t i = 0; i < 3; ++i )
    {
        frame.data[i] = i;
    }
    ssize_t bytesWritten = write( socketFD, &frame, sizeof( struct can_frame ) );
    EXPECT_EQ( bytesWritten, sizeof( struct can_frame ) );
    return true;
}

class CANDataSourceTest : public ::testing::Test
{
public:
    int socketFD;

protected:
    void
    SetUp() override
    {
        socketFD = setup();
        if ( socketFD == -1 )
        {
            GTEST_SKIP() << "Skipping test fixture due to unavailability of socket";
        }
    }

    void
    TearDown() override
    {
        cleanUp( socketFD );
    }
};

TEST_F( CANDataSourceTest, testAquireDataFromNetwork )
{
    LocalDataSourceEventListener listener;
    ASSERT_TRUE( socketFD != -1 );

    static_cast<void>( socketFD >= 0 );
    VehicleDataSourceConfig sourceConfig;
    sourceConfig.transportProperties.emplace( "interfaceName", "vcan0" );
    sourceConfig.transportProperties.emplace( "protocolName", "CAN" );
    sourceConfig.transportProperties.emplace( "threadIdleTimeMs", "100" );
    sourceConfig.maxNumberOfVehicleDataMessages = 1000;
    std::vector<VehicleDataSourceConfig> sourceConfigs = { sourceConfig };
    CANDataSource dataSource;
    ASSERT_TRUE( dataSource.init( sourceConfigs ) );
    ASSERT_TRUE( dataSource.subscribeListener( &listener ) );

    ASSERT_TRUE( dataSource.connect() );
    ASSERT_TRUE( listener.gotConnectCallback );
    ASSERT_TRUE( dataSource.isAlive() );
    // Set the Channel in an active acquire state
    dataSource.resumeDataAcquisition();
    ASSERT_EQ( dataSource.getVehicleDataSourceIfName(), "vcan0" );
    ASSERT_EQ( dataSource.getVehicleDataSourceProtocol(), VehicleDataSourceProtocol::RAW_SOCKET );
    ASSERT_EQ( dataSource.getVehicleDataSourceType(), VehicleDataSourceType::CAN_SOURCE );
    VehicleDataMessage msg;
    WAIT_ASSERT_TRUE( sendTestMessage( socketFD ) && dataSource.getBuffer()->pop( msg ) );
    ASSERT_TRUE( dataSource.disconnect() );
    ASSERT_TRUE( dataSource.unSubscribeListener( &listener ) );
    ASSERT_TRUE( listener.gotDisConnectCallback );
}

TEST_F( CANDataSourceTest, testDoNotAcquireDataFromNetwork )
{
    LocalDataSourceEventListener listener;
    int socketFD = setup();
    ASSERT_TRUE( socketFD != -1 );

    static_cast<void>( socketFD >= 0 );
    VehicleDataSourceConfig sourceConfig;
    sourceConfig.transportProperties.emplace( "interfaceName", "vcan0" );
    sourceConfig.transportProperties.emplace( "protocolName", "CAN" );
    sourceConfig.transportProperties.emplace( "threadIdleTimeMs", "100" );
    sourceConfig.maxNumberOfVehicleDataMessages = 1000;
    std::vector<VehicleDataSourceConfig> sourceConfigs = { sourceConfig };
    CANDataSource dataSource;
    ASSERT_TRUE( dataSource.init( sourceConfigs ) );
    ASSERT_TRUE( dataSource.subscribeListener( &listener ) );

    ASSERT_TRUE( dataSource.connect() );
    ASSERT_TRUE( listener.gotConnectCallback );
    ASSERT_TRUE( dataSource.isAlive() );
    // The channel is not acquiring data from the network by default
    // We should test that although data is available in the socket,
    // the channel buffer must be empty
    ASSERT_EQ( dataSource.getVehicleDataSourceIfName(), "vcan0" );
    ASSERT_EQ( dataSource.getVehicleDataSourceProtocol(), VehicleDataSourceProtocol::RAW_SOCKET );
    ASSERT_EQ( dataSource.getVehicleDataSourceType(), VehicleDataSourceType::CAN_SOURCE );
    VehicleDataMessage msg;
    // No messages should be in the buffer
    DELAY_ASSERT_FALSE( sendTestMessage( socketFD ) && dataSource.getBuffer()->pop( msg ) );
    ASSERT_TRUE( dataSource.disconnect() ); // Here the frame will be read from the socket
    ASSERT_TRUE( dataSource.unSubscribeListener( &listener ) );
    ASSERT_TRUE( listener.gotDisConnectCallback );
}

TEST_F( CANDataSourceTest, testNetworkDataAquisitionStateChange )
{
    // In this test, we want to start the channel with the default settings i.e. sleep mode,
    // then activate data acquisition and check that the channel buffer effectively has a message,
    // then interrupt the consumption and make sure that the channel is in sleep mode.
    LocalDataSourceEventListener listener;
    ASSERT_TRUE( socketFD != -1 );

    static_cast<void>( socketFD >= 0 );
    VehicleDataSourceConfig sourceConfig;
    sourceConfig.transportProperties.emplace( "interfaceName", "vcan0" );
    sourceConfig.transportProperties.emplace( "protocolName", "CAN" );
    sourceConfig.transportProperties.emplace( "threadIdleTimeMs", "100" );
    sourceConfig.maxNumberOfVehicleDataMessages = 1000;
    std::vector<VehicleDataSourceConfig> sourceConfigs = { sourceConfig };
    CANDataSource dataSource;
    ASSERT_TRUE( dataSource.init( sourceConfigs ) );
    ASSERT_TRUE( dataSource.subscribeListener( &listener ) );

    ASSERT_TRUE( dataSource.connect() );
    ASSERT_TRUE( listener.gotConnectCallback );
    ASSERT_TRUE( dataSource.isAlive() );
    // The channel is not acquiring data from the network by default
    // We should test that although data is available in the socket,
    // the channel buffer must be empty
    ASSERT_EQ( dataSource.getVehicleDataSourceIfName(), "vcan0" );
    ASSERT_EQ( dataSource.getVehicleDataSourceProtocol(), VehicleDataSourceProtocol::RAW_SOCKET );
    ASSERT_EQ( dataSource.getVehicleDataSourceType(), VehicleDataSourceType::CAN_SOURCE );
    // Send a message on the bus.
    VehicleDataMessage msg;
    // No messages should be in the buffer
    DELAY_ASSERT_FALSE( sendTestMessage( socketFD ) && dataSource.getBuffer()->pop( msg ) );

    // Activate consumption on the bus and make sure the channel buffer has items.
    dataSource.resumeDataAcquisition();

    // Send a message on the bus.
    // 1 message should be in the buffer as the channel is active.
    WAIT_ASSERT_TRUE( sendTestMessage( socketFD ) && dataSource.getBuffer()->pop( msg ) );

    // Interrupt data acquisition and make sure that the channel now does not consume data
    // anymore.
    dataSource.suspendDataAcquisition();
    // Send a message on the bus.
    // No messages should be in the buffer
    DELAY_ASSERT_FALSE( sendTestMessage( socketFD ) && dataSource.getBuffer()->pop( msg ) );

    ASSERT_TRUE( dataSource.disconnect() );
    ASSERT_TRUE( dataSource.unSubscribeListener( &listener ) );
    ASSERT_TRUE( listener.gotDisConnectCallback );
}

TEST_F( CANDataSourceTest, testSourceIdsAreUnique )
{
    ASSERT_TRUE( socketFD != -1 );
    static_cast<void>( socketFD >= 0 );

    constexpr auto NUM_SOURCES = 5;
    std::unordered_set<VehicleDataSourceID> sourceIDs;
    for ( auto i = 0; i < NUM_SOURCES; ++i )
    {
        CANDataSource source;
        sourceIDs.insert( source.getVehicleDataSourceID() );
    }
    ASSERT_EQ( NUM_SOURCES, sourceIDs.size() );
}

TEST_F( CANDataSourceTest, testCanFDSocketMode )
{
    LocalDataSourceEventListener listener;
    int socketFD = setup( true );
    ASSERT_TRUE( socketFD != -1 );

    static_cast<void>( socketFD >= 0 );
    VehicleDataSourceConfig sourceConfig;
    sourceConfig.transportProperties.emplace( "interfaceName", "vcan0" );
    sourceConfig.transportProperties.emplace( "protocolName", "CAN-FD" );
    sourceConfig.transportProperties.emplace( "threadIdleTimeMs", "100" );
    sourceConfig.maxNumberOfVehicleDataMessages = 1000;
    std::vector<VehicleDataSourceConfig> sourceConfigs = { sourceConfig };
    CANDataSource dataSource;
    ASSERT_TRUE( dataSource.init( sourceConfigs ) );
    ASSERT_TRUE( dataSource.subscribeListener( &listener ) );

    ASSERT_TRUE( dataSource.connect() );
    ASSERT_TRUE( listener.gotConnectCallback );
    ASSERT_TRUE( dataSource.isAlive() );
    // Set the Channel in an active acquire state
    dataSource.resumeDataAcquisition();

    ASSERT_EQ( dataSource.getVehicleDataSourceIfName(), "vcan0" );
    ASSERT_EQ( dataSource.getVehicleDataSourceProtocol(), VehicleDataSourceProtocol::RAW_SOCKET );
    ASSERT_EQ( dataSource.getVehicleDataSourceType(), VehicleDataSourceType::CAN_SOURCE );

    // Send a CAN-FD message on the bus.
    VehicleDataMessage msg;
    WAIT_ASSERT_TRUE( sendTestFDMessage( socketFD ) && dataSource.getBuffer()->pop( msg ) );
    ASSERT_TRUE( dataSource.disconnect() );
    ASSERT_TRUE( dataSource.unSubscribeListener( &listener ) );
    ASSERT_TRUE( listener.gotDisConnectCallback );
}

TEST_F( CANDataSourceTest, testSendRegularID )
{
    LocalDataSourceEventListener listener;
    ASSERT_TRUE( socketFD != -1 );

    static_cast<void>( socketFD >= 0 );
    VehicleDataSourceConfig sourceConfig;
    sourceConfig.transportProperties.emplace( "interfaceName", "vcan0" );
    sourceConfig.transportProperties.emplace( "protocolName", "CAN" );
    sourceConfig.transportProperties.emplace( "threadIdleTimeMs", "100" );
    sourceConfig.maxNumberOfVehicleDataMessages = 1000;
    std::vector<VehicleDataSourceConfig> sourceConfigs = { sourceConfig };
    CANDataSource dataSource;
    ASSERT_TRUE( dataSource.init( sourceConfigs ) );
    ASSERT_TRUE( dataSource.subscribeListener( &listener ) );

    ASSERT_TRUE( dataSource.connect() );
    ASSERT_TRUE( listener.gotConnectCallback );
    ASSERT_TRUE( dataSource.isAlive() );
    // Set the Channel in an active acquire state
    dataSource.resumeDataAcquisition();
    ASSERT_EQ( dataSource.getVehicleDataSourceIfName(), "vcan0" );
    ASSERT_EQ( dataSource.getVehicleDataSourceProtocol(), VehicleDataSourceProtocol::RAW_SOCKET );
    ASSERT_EQ( dataSource.getVehicleDataSourceType(), VehicleDataSourceType::CAN_SOURCE );
    VehicleDataMessage msg;
    WAIT_ASSERT_TRUE( sendTestMessage( socketFD ) && dataSource.getBuffer()->pop( msg ) );
    ASSERT_EQ( msg.getMessageID(), 0x123 );
    ASSERT_TRUE( dataSource.disconnect() );
    ASSERT_TRUE( dataSource.unSubscribeListener( &listener ) );
    ASSERT_TRUE( listener.gotDisConnectCallback );
}

TEST_F( CANDataSourceTest, testExtractExtendedID )
{
    LocalDataSourceEventListener listener;
    ASSERT_TRUE( socketFD != -1 );

    static_cast<void>( socketFD >= 0 );
    VehicleDataSourceConfig sourceConfig;
    sourceConfig.transportProperties.emplace( "interfaceName", "vcan0" );
    sourceConfig.transportProperties.emplace( "protocolName", "CAN" );
    sourceConfig.transportProperties.emplace( "threadIdleTimeMs", "100" );
    sourceConfig.maxNumberOfVehicleDataMessages = 1000;
    std::vector<VehicleDataSourceConfig> sourceConfigs = { sourceConfig };
    CANDataSource dataSource;
    ASSERT_TRUE( dataSource.init( sourceConfigs ) );
    ASSERT_TRUE( dataSource.subscribeListener( &listener ) );

    ASSERT_TRUE( dataSource.connect() );
    ASSERT_TRUE( listener.gotConnectCallback );
    ASSERT_TRUE( dataSource.isAlive() );
    // Set the Channel in an active acquire state
    dataSource.resumeDataAcquisition();
    ASSERT_EQ( dataSource.getVehicleDataSourceIfName(), "vcan0" );
    ASSERT_EQ( dataSource.getVehicleDataSourceProtocol(), VehicleDataSourceProtocol::RAW_SOCKET );
    ASSERT_EQ( dataSource.getVehicleDataSourceType(), VehicleDataSourceType::CAN_SOURCE );
    VehicleDataMessage msg;
    WAIT_ASSERT_TRUE( sendTestMessageExtendedID( socketFD ) && dataSource.getBuffer()->pop( msg ) );
    ASSERT_EQ( msg.getMessageID(), 0x80000123 );
    ASSERT_TRUE( dataSource.disconnect() );
    ASSERT_TRUE( dataSource.unSubscribeListener( &listener ) );
    ASSERT_TRUE( listener.gotDisConnectCallback );
}
