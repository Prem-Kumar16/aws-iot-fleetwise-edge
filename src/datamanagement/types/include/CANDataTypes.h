// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "SignalTypes.h"
#include "TimeTypes.h"
#include "datatypes/VehicleDataSourceTypes.h"
#include <memory>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::VehicleNetwork;
using namespace Aws::IoTFleetWise::Platform::Linux;

union CANPhysicalValue {
    double doubleVal;
    uint64_t uint64Val;
    int64_t int64Val;
};

struct CANPhysicalValueType
{
    CANPhysicalValue signalValue;
    SignalType signalType;

    template <typename T>
    CANPhysicalValueType( T val, SignalType type )
        : signalType( type )
    {
        switch ( signalType )
        {
        case SignalType::UINT64:
            signalValue.uint64Val = static_cast<uint64_t>( val );
            break;
        case SignalType::INT64:
            signalValue.int64Val = static_cast<int64_t>( val );
            break;
        default:
            signalValue.doubleVal = static_cast<double>( val );
        }
    }

    SignalType
    getType() const
    {
        return signalType;
    }
};

struct CANDecodedSignal
{

    CANDecodedSignal( uint32_t signalID, int64_t rawValue, CANPhysicalValueType physicalValue, SignalType signalTypeIn )
        : mSignalID( signalID )
        , mRawValue( rawValue )
        , mPhysicalValue( physicalValue )
        , mSignalType( signalTypeIn )
    {
    }

    uint32_t mSignalID;
    int64_t mRawValue;
    CANPhysicalValueType mPhysicalValue;
    SignalType mSignalType{ SignalType::DOUBLE };
};

struct CANFrameInfo
{
    uint32_t mFrameID{ 0 };
    std::string mFrameRawData;
    std::vector<CANDecodedSignal> mSignals;
};
/**
 * @brief Cloud does not send information about each CAN message, so we set every CAN message size to the maximum.
 */
static constexpr uint8_t MAX_CAN_FRAME_BYTE_SIZE = 64;

struct CANDecodedMessage
{
    CANFrameInfo mFrameInfo;
    Timestamp mReceptionTime{ 0 };
    Timestamp mDecodingTime{ 0 };
    VehicleDataSourceIfName mChannelIfName;
    VehicleDataSourceType mChannelType;
    VehicleDataSourceProtocol mChannelProtocol;
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
