/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <electrical-protection-alarm-stub.h>

#include <app/clusters/electrical-protection-alarm-server/ElectricalProtectionAlarmTestEventTriggerHandler.h>
#include <clusters/ElectricalProtectionAlarm/Structs.h>
#include <lib/support/CodeUtils.h>

#include <algorithm>
#include <array>
#include <memory>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ElectricalProtectionAlarm;

namespace {

// Each alarm bit belongs to exactly one feature.
bool FeaturesSupportAlarm(BitMask<Feature> features, AlarmBitmap alarm)
{
    switch (alarm)
    {
    case AlarmBitmap::kShortCircuitFault:
        return features.Has(Feature::kShortCircuit);
    case AlarmBitmap::kOverLoadFault:
        return features.Has(Feature::kOverLoad);
    case AlarmBitmap::kOverVoltageFault:
        return features.Has(Feature::kOverVoltage);
    case AlarmBitmap::kVoltageSurgeFault:
        return features.Has(Feature::kSurgeProtection);
    case AlarmBitmap::kResidualCurrentFault:
        return features.Has(Feature::kResidualCurrent);
    case AlarmBitmap::kArcFault:
        return features.Has(Feature::kArcFault);
    case AlarmBitmap::kSelfTest:
        return features.Has(Feature::kSelfTest);
    default:
        return false;
    }
}

constexpr AlarmBitmap kAlarmList[] = {
    AlarmBitmap::kShortCircuitFault, AlarmBitmap::kOverLoadFault,        AlarmBitmap::kOverVoltageFault,
    AlarmBitmap::kVoltageSurgeFault, AlarmBitmap::kResidualCurrentFault, AlarmBitmap::kArcFault,
    AlarmBitmap::kSelfTest,
};

BitMask<AlarmBitmap> SupportedAlarmsFor(BitMask<Feature> features)
{
    BitMask<AlarmBitmap> bits;
    for (AlarmBitmap alarm : kAlarmList)
    {
        if (FeaturesSupportAlarm(features, alarm))
        {
            bits.Set(alarm);
        }
    }
    return bits;
}

// Representative ratings for a 100 A / 240 V residential panel breaker. Currents are amperage-mA
// and voltages are voltage-mV per the cluster definition; ResponseTime is in nanoseconds. These are
// illustrative fixed values, not measurements: the point is that every rating attribute encodes a
// populated struct so the attribute tests exercise their structure validation instead of taking the
// null path.
constexpr int64_t kRatedCurrentMa        = 100000;   // 100 A
constexpr int64_t kUltimateMaxCurrentMa  = 10000000; // 10 kA interrupting capacity
constexpr int64_t kServiceMaxCurrentMa   = 6000000;  // 6 kA service short-circuit rating
constexpr int64_t kMaxOperatingVoltageMv = 253000;   // 240 V +5%
constexpr int64_t kOverVoltageTripMv     = 275000;
constexpr uint64_t kResponseTimeNs       = 25;   // typical MOV clamping response
constexpr uint64_t kEnergyAbsorptionJ    = 1200; // surge energy absorption capability

Structs::ArcFaultRatingsStruct::Type MakeArcFaultRating()
{
    Structs::ArcFaultRatingsStruct::Type r;
    r.seriesArcCurrentSensitivity   = MakeOptional<int64_t>(5000);  // 5 A
    r.parallelArcCurrentSensitivity = MakeOptional<int64_t>(30000); // 30 A
    BitMask<ArcCauseBitmap> causes;
    causes.Set(ArcCauseBitmap::kSeries).Set(ArcCauseBitmap::kParallelToNeutral).Set(ArcCauseBitmap::kParallelToGround);
    r.supportedArcCauses = MakeOptional(causes);
    return r;
}

Structs::OverLoadRatingsStruct::Type MakeOverLoadRating()
{
    Structs::OverLoadRatingsStruct::Type r;
    BitMask<CurrentTripMechanismBitmap> mech;
    mech.Set(CurrentTripMechanismBitmap::kThermal).Set(CurrentTripMechanismBitmap::kMagnetic);
    r.tripCurrent        = MakeOptional(kRatedCurrentMa);
    r.tripCurve          = MakeOptional(CurrentTripCurveEnum::kTypeC);
    r.tripMechanism      = MakeOptional(mech);
    r.ultimateMaxCurrent = MakeOptional(kUltimateMaxCurrentMa);
    r.serviceMaxCurrent  = MakeOptional(kServiceMaxCurrentMa);
    return r;
}

Structs::OverVoltageRatingsStruct::Type MakeOverVoltageRating()
{
    Structs::OverVoltageRatingsStruct::Type r;
    BitMask<VoltageTripMechanismBitmap> mech;
    mech.Set(VoltageTripMechanismBitmap::kMov);
    r.tripMechanism                 = MakeOptional(mech);
    r.tripVoltage                   = MakeOptional(kOverVoltageTripMv);
    r.maxContinuousOperatingVoltage = MakeOptional(kMaxOperatingVoltageMv);
    r.responseTime                  = MakeOptional(kResponseTimeNs);
    return r;
}

Structs::SurgeProtectionRatingsStruct::Type MakeSurgeProtectionRating()
{
    Structs::SurgeProtectionRatingsStruct::Type r;
    BitMask<VoltageTripMechanismBitmap> mech;
    mech.Set(VoltageTripMechanismBitmap::kMov);
    BitMask<SurgeProtectionClassBitmap> cls;
    cls.Set(SurgeProtectionClassBitmap::kClassII);
    BitMask<SurgeProtectionTypeBitmap> type;
    type.Set(SurgeProtectionTypeBitmap::kType2);
    r.tripMechanism                  = MakeOptional(mech);
    r.protectionClass                = MakeOptional(cls);
    r.protectionType                 = MakeOptional(type);
    r.maxContinuousOperatingVoltage  = MakeOptional(kOverVoltageTripMv);
    r.maxVoltageProtection           = MakeOptional<int64_t>(1200000); // 1.2 kV let-through
    r.maxTemporaryVoltage            = MakeOptional<int64_t>(335000);
    r.nominalDischargeCurrent        = MakeOptional<int64_t>(20000000); // 20 kA
    r.maximumDischargeCurrent        = MakeOptional<int64_t>(40000000); // 40 kA
    r.ratedShortCircuitCurrent       = MakeOptional(kUltimateMaxCurrentMa);
    r.ratedShortTimeWithstandCurrent = MakeOptional(kServiceMaxCurrentMa);
    r.energyAbsorptionCapability     = MakeOptional(kEnergyAbsorptionJ);
    r.responseTime                   = MakeOptional(kResponseTimeNs);
    return r;
}

Structs::ShortCircuitRatingsStruct::Type MakeShortCircuitRating()
{
    Structs::ShortCircuitRatingsStruct::Type r;
    BitMask<CurrentTripMechanismBitmap> mech;
    mech.Set(CurrentTripMechanismBitmap::kMagnetic);
    r.tripCurrent        = MakeOptional<int64_t>(1000000); // 1 kA magnetic trip
    r.tripMechanism      = MakeOptional(mech);
    r.tripCurve          = MakeOptional(CurrentTripCurveEnum::kTypeC);
    r.ultimateMaxCurrent = MakeOptional(kUltimateMaxCurrentMa);
    r.serviceMaxCurrent  = MakeOptional(kServiceMaxCurrentMa);
    r.maxCurrent         = MakeOptional(kUltimateMaxCurrentMa);
    return r;
}

Structs::ResidualCurrentFaultRatingsStruct::Type MakeResidualCurrentRating()
{
    Structs::ResidualCurrentFaultRatingsStruct::Type r;
    BitMask<CurrentTripMechanismBitmap> mech;
    mech.Set(CurrentTripMechanismBitmap::kElectronic);
    BitMask<TrippingCharacteristicsBitmap> characteristic;
    characteristic.Set(TrippingCharacteristicsBitmap::kSelective);
    r.currentSensitivity     = MakeOptional<int64_t>(30); // 30 mA
    r.tripMechanism          = MakeOptional(mech);
    r.voltageDependent       = MakeOptional(false);
    r.groundFaultClass       = MakeOptional(GroundFaultClassEnum::kClassA);
    r.waveform               = MakeOptional(CurrentWaveformEnum::kAc);
    r.trippingCharacteristic = MakeOptional(characteristic);
    r.ultimateMaxCurrent     = MakeOptional(kUltimateMaxCurrentMa);
    r.serviceMaxCurrent      = MakeOptional(kServiceMaxCurrentMa);
    return r;
}

// Only the ratings and attributes whose feature is on are populated, so each instance publishes
// exactly what its feature map promises. Alarm State starts clear and is driven by the
// test-event-trigger below.
ElectricalProtectionAlarmCluster::StartupConfiguration MakeConfig(BitMask<Feature> features)
{
    ElectricalProtectionAlarmCluster::StartupConfiguration config;
    config.featureMap = features;
    config.supported  = SupportedAlarmsFor(features);
    config.mask       = SupportedAlarmsFor(features);

    if (features.Has(Feature::kArcFault))
    {
        BitMask<ArcCauseBitmap> arcCause;
        arcCause.Set(ArcCauseBitmap::kSeries).Set(ArcCauseBitmap::kParallelToNeutral);
        config.arcCause       = DataModel::MakeNullable(arcCause);
        config.arcFaultRating = DataModel::MakeNullable(MakeArcFaultRating());
    }
    if (features.Has(Feature::kOverLoad))
    {
        config.overLoadRating = DataModel::MakeNullable(MakeOverLoadRating());
    }
    if (features.Has(Feature::kOverVoltage))
    {
        config.overVoltageRating = DataModel::MakeNullable(MakeOverVoltageRating());
    }
    if (features.Has(Feature::kSurgeProtection))
    {
        config.surgeProtectionRating = DataModel::MakeNullable(MakeSurgeProtectionRating());
    }
    if (features.Has(Feature::kShortCircuit))
    {
        config.shortCircuitRating = DataModel::MakeNullable(MakeShortCircuitRating());
    }
    if (features.Has(Feature::kResidualCurrent))
    {
        config.residualCurrentRating = DataModel::MakeNullable(MakeResidualCurrentRating());
    }
    return config;
}

// One slot per endpoint carrying the cluster. The feature map is kept beside the instance because
// the test event triggers arrive with the endpoint already stripped.
struct Slot
{
    std::unique_ptr<ElectricalProtectionAlarm::Instance> instance;
    BitMask<Feature> features;
};

constexpr size_t kMaxInstances = 3;
std::array<Slot, kMaxInstances> gSlots;

// Apply `change` to every instance whose features support `alarm`. True only if at least one
// instance applied it and none failed.
template <typename Change>
bool ForEachSupporting(AlarmBitmap alarm, Change change)
{
    bool applied = false;
    for (auto & slot : gSlots)
    {
        if (slot.instance == nullptr || !FeaturesSupportAlarm(slot.features, alarm))
        {
            continue;
        }
        VerifyOrReturnValue(change(slot.instance->Cluster()), false);
        applied = true;
    }
    return applied;
}

} // namespace

namespace chip::app::Clusters::ElectricalProtectionAlarm {

CHIP_ERROR ElectricalProtectionAlarmInit(EndpointId endpointId, BitMask<Feature> features)
{
    auto slot = std::find_if(gSlots.begin(), gSlots.end(), [](const Slot & s) { return s.instance == nullptr; });
    VerifyOrReturnError(slot != gSlots.end(), CHIP_ERROR_NO_MEMORY);

    slot->instance = std::make_unique<Instance>(endpointId, MakeConfig(features));
    VerifyOrReturnError(slot->instance != nullptr, CHIP_ERROR_NO_MEMORY);
    slot->features = features;

    CHIP_ERROR err = slot->instance->Init();
    if (err != CHIP_NO_ERROR)
    {
        slot->instance.reset();
    }
    return err;
}

void ElectricalProtectionAlarmShutdown()
{
    // ~Instance() unregisters.
    for (auto & slot : gSlots)
    {
        slot.instance.reset();
        slot.features = BitMask<Feature>();
    }
}

} // namespace chip::app::Clusters::ElectricalProtectionAlarm

// The SDK strips the endpoint from the trigger before calling here, so a trigger reaches every
// instance whose features support the alarm. Subscriptions are endpoint-scoped, so a test watching
// one endpoint only sees its own instance change.
bool HandleElectricalProtectionAlarmTestEventTrigger(uint64_t eventTrigger)
{
    auto activate = [](AlarmBitmap alarm) {
        return ForEachSupporting(alarm, [alarm](ElectricalProtectionAlarmCluster & cluster) {
            return cluster.ActivateAlarms(BitMask<AlarmBitmap>(alarm)) == CHIP_NO_ERROR;
        });
    };
    auto deactivate = [](AlarmBitmap alarm) {
        return ForEachSupporting(alarm, [alarm](ElectricalProtectionAlarmCluster & cluster) {
            return cluster.DeactivateAlarms(BitMask<AlarmBitmap>(alarm)) == CHIP_NO_ERROR;
        });
    };

    switch (static_cast<ElectricalProtectionAlarmTrigger>(eventTrigger))
    {
    case ElectricalProtectionAlarmTrigger::kClearAll: {
        bool applied = false;
        for (auto & slot : gSlots)
        {
            if (slot.instance == nullptr)
            {
                continue;
            }
            VerifyOrReturnValue(slot.instance->Cluster().ClearAllAlarms() == CHIP_NO_ERROR, false);
            applied = true;
        }
        return applied;
    }
    case ElectricalProtectionAlarmTrigger::kSetShortCircuitFault:
        return activate(AlarmBitmap::kShortCircuitFault);
    case ElectricalProtectionAlarmTrigger::kSetOverLoadFault:
        return activate(AlarmBitmap::kOverLoadFault);
    case ElectricalProtectionAlarmTrigger::kSetOverVoltageFault:
        return activate(AlarmBitmap::kOverVoltageFault);
    case ElectricalProtectionAlarmTrigger::kSetVoltageSurgeFault:
        return activate(AlarmBitmap::kVoltageSurgeFault);
    case ElectricalProtectionAlarmTrigger::kSetResidualCurrentFault:
        return activate(AlarmBitmap::kResidualCurrentFault);
    case ElectricalProtectionAlarmTrigger::kSetArcFault:
        return activate(AlarmBitmap::kArcFault);
    case ElectricalProtectionAlarmTrigger::kSetSelfTest:
        return activate(AlarmBitmap::kSelfTest);
    case ElectricalProtectionAlarmTrigger::kClearShortCircuitFault:
        return deactivate(AlarmBitmap::kShortCircuitFault);
    case ElectricalProtectionAlarmTrigger::kClearOverLoadFault:
        return deactivate(AlarmBitmap::kOverLoadFault);
    case ElectricalProtectionAlarmTrigger::kClearOverVoltageFault:
        return deactivate(AlarmBitmap::kOverVoltageFault);
    case ElectricalProtectionAlarmTrigger::kClearVoltageSurgeFault:
        return deactivate(AlarmBitmap::kVoltageSurgeFault);
    case ElectricalProtectionAlarmTrigger::kClearResidualCurrentFault:
        return deactivate(AlarmBitmap::kResidualCurrentFault);
    case ElectricalProtectionAlarmTrigger::kClearArcFault:
        return deactivate(AlarmBitmap::kArcFault);
    case ElectricalProtectionAlarmTrigger::kClearSelfTest:
        return deactivate(AlarmBitmap::kSelfTest);
    default:
        return false;
    }
}
