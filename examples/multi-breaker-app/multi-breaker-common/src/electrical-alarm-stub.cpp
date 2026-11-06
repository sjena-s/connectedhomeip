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

#include <electrical-alarm-stub.h>

#include <app/clusters/electrical-alarm-server/ElectricalAlarmTestEventTriggerHandler.h>
#include <lib/support/CodeUtils.h>

#include <algorithm>
#include <array>
#include <memory>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ElectricalAlarm;

namespace {

// The app performs no real measurement, so every request is approved and nothing is written to
// hardware. SetElectricalAlarmThresholdsCallback is left to the base implementation, which also
// approves.
class BreakerAlarmDelegate : public ElectricalAlarm::Delegate
{
public:
    bool ModifyEnabledAlarmsCallback(const BitMask<AlarmBitmap> mask) override { return true; }
    bool ResetAlarmsCallback(const BitMask<AlarmBitmap> alarms) override { return true; }
};

BreakerAlarmDelegate gDelegate;

// One slot per endpoint carrying the cluster. The feature map is kept beside the instance because
// the test event triggers arrive with the endpoint already stripped, so the app decides which
// instances a trigger applies to from what each one offers.
struct Slot
{
    std::unique_ptr<ElectricalAlarm::Instance> instance;
    BitMask<Feature> features;
};

constexpr size_t kMaxInstances = 2;
std::array<Slot, kMaxInstances> gSlots;

constexpr AlarmBitmap kAlarmList[] = {
    AlarmBitmap::kOverVoltage,      AlarmBitmap::kUnderVoltage,   AlarmBitmap::kOverFrequency,   AlarmBitmap::kUnderFrequency,
    AlarmBitmap::kOverPower,        AlarmBitmap::kUnderPower,     AlarmBitmap::kOverCurrent,     AlarmBitmap::kUnderCurrent,
    AlarmBitmap::kFrequencyQuality, AlarmBitmap::kVoltageQuality, AlarmBitmap::kSwappedPolarity, AlarmBitmap::kLossOfMeasurement,
    AlarmBitmap::kLossOfGridPower,  AlarmBitmap::kPowerImported,  AlarmBitmap::kPowerExported,
};

// The ten alarm classes are each gated on a feature. The other five alarms (quality, polarity,
// measurement and grid-power) have no feature, so every instance can carry them.
bool FeaturesSupportAlarm(BitMask<Feature> features, AlarmBitmap alarm)
{
    switch (alarm)
    {
    case AlarmBitmap::kOverVoltage:
        return features.Has(Feature::kOverVoltage);
    case AlarmBitmap::kUnderVoltage:
        return features.Has(Feature::kUnderVoltage);
    case AlarmBitmap::kOverFrequency:
        return features.Has(Feature::kOverFrequency);
    case AlarmBitmap::kUnderFrequency:
        return features.Has(Feature::kUnderFrequency);
    case AlarmBitmap::kOverPower:
        return features.Has(Feature::kOverPower);
    case AlarmBitmap::kUnderPower:
        return features.Has(Feature::kUnderPower);
    case AlarmBitmap::kOverCurrent:
        return features.Has(Feature::kOverCurrent);
    case AlarmBitmap::kUnderCurrent:
        return features.Has(Feature::kUnderCurrent);
    case AlarmBitmap::kPowerImported:
        return features.Has(Feature::kPowerImport);
    case AlarmBitmap::kPowerExported:
        return features.Has(Feature::kPowerExport);
    default:
        return true;
    }
}

// Supported and Mask are narrowed to what the instance's features can actually raise, so a test
// that walks the Supported bits only fires triggers this endpoint will honour.
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

namespace chip::app::Clusters::ElectricalAlarm {

CHIP_ERROR ElectricalAlarmInit(EndpointId endpointId, BitMask<Feature> features)
{
    auto slot = std::find_if(gSlots.begin(), gSlots.end(), [](const Slot & s) { return s.instance == nullptr; });
    VerifyOrReturnError(slot != gSlots.end(), CHIP_ERROR_NO_MEMORY);

    slot->instance = std::make_unique<Instance>(endpointId, &gDelegate, features);
    VerifyOrReturnError(slot->instance != nullptr, CHIP_ERROR_NO_MEMORY);
    slot->features = features;

    auto fail = [slot](CHIP_ERROR err) {
        slot->instance.reset();
        return err;
    };

    CHIP_ERROR err = slot->instance->Init();
    VerifyOrReturnError(err == CHIP_NO_ERROR, fail(err));

    // Supported defaults to empty. Declare every alarm this endpoint's features can raise, so
    // Mask and State have the widest valid set to narrow against.
    const BitMask<AlarmBitmap> supported = SupportedAlarmsFor(features);
    auto & cluster                       = slot->instance->Cluster();
    VerifyOrReturnError(cluster.SetSupportedValue(supported) == Protocols::InteractionModel::Status::Success,
                        fail(CHIP_ERROR_INTERNAL));

    // Mask defaults to empty, which suppresses every alarm and makes State undrivable. Enable them
    // all so a test can activate any alarm it asks for.
    VerifyOrReturnError(cluster.SetMaskValue(supported) == Protocols::InteractionModel::Status::Success, fail(CHIP_ERROR_INTERNAL));

    // Latch every supported alarm when RST is offered, so an activated alarm stays active until
    // Reset clears it. Without this the Reset command has nothing to act on.
    if (features.Has(Feature::kReset))
    {
        VerifyOrReturnError(cluster.SetLatchValue(supported) == Protocols::InteractionModel::Status::Success,
                            fail(CHIP_ERROR_INTERNAL));
    }
    return CHIP_NO_ERROR;
}

void ElectricalAlarmShutdown()
{
    // ~Instance() unregisters.
    for (auto & slot : gSlots)
    {
        slot.instance.reset();
        slot.features = BitMask<Feature>();
    }
}

} // namespace chip::app::Clusters::ElectricalAlarm

// The SDK strips the endpoint from the trigger before calling here, so a trigger reaches every
// breaker whose features support the alarm. Subscriptions are endpoint-scoped, so a test watching
// one endpoint only sees its own instance change.
bool HandleElectricalAlarmTestEventTrigger(uint64_t eventTrigger)
{
    // Setting is additive: OR the bit into the current State, per the test plan note that simulate
    // triggers do not disturb other active alarms.
    auto activate = [](AlarmBitmap alarm) {
        return ForEachSupporting(alarm, [alarm](ElectricalAlarmCluster & cluster) {
            BitMask<AlarmBitmap> next = cluster.GetState();
            next.Set(alarm);
            return cluster.SetStateValue(next) == Protocols::InteractionModel::Status::Success;
        });
    };

    // Clearing one alarm models its measured condition going away. SetStateValue preserves bits
    // that are latched and active, so a latched alarm survives this and clears only via Reset.
    auto deactivate = [](AlarmBitmap alarm) {
        return ForEachSupporting(alarm, [alarm](ElectricalAlarmCluster & cluster) {
            BitMask<AlarmBitmap> next = cluster.GetState();
            next.Clear(alarm);
            return cluster.SetStateValue(next) == Protocols::InteractionModel::Status::Success;
        });
    };

    switch (static_cast<ElectricalAlarmTrigger>(eventTrigger))
    {
    // Test cleanup: drop every alarm including latched ones, so State returns to zero.
    case ElectricalAlarmTrigger::kClearAll: {
        bool applied = false;
        for (auto & slot : gSlots)
        {
            if (slot.instance == nullptr)
            {
                continue;
            }
            VerifyOrReturnValue(slot.instance->Cluster().SetStateValue(BitMask<AlarmBitmap>(), /* ignoreLatchState = */ true) ==
                                    Protocols::InteractionModel::Status::Success,
                                false);
            applied = true;
        }
        return applied;
    }
    case ElectricalAlarmTrigger::kSetOverVoltage:
        return activate(AlarmBitmap::kOverVoltage);
    case ElectricalAlarmTrigger::kClearOverVoltage:
        return deactivate(AlarmBitmap::kOverVoltage);
    case ElectricalAlarmTrigger::kSetUnderVoltage:
        return activate(AlarmBitmap::kUnderVoltage);
    case ElectricalAlarmTrigger::kClearUnderVoltage:
        return deactivate(AlarmBitmap::kUnderVoltage);
    case ElectricalAlarmTrigger::kSetOverFrequency:
        return activate(AlarmBitmap::kOverFrequency);
    case ElectricalAlarmTrigger::kClearOverFrequency:
        return deactivate(AlarmBitmap::kOverFrequency);
    case ElectricalAlarmTrigger::kSetUnderFrequency:
        return activate(AlarmBitmap::kUnderFrequency);
    case ElectricalAlarmTrigger::kClearUnderFrequency:
        return deactivate(AlarmBitmap::kUnderFrequency);
    case ElectricalAlarmTrigger::kSetOverPower:
        return activate(AlarmBitmap::kOverPower);
    case ElectricalAlarmTrigger::kClearOverPower:
        return deactivate(AlarmBitmap::kOverPower);
    case ElectricalAlarmTrigger::kSetUnderPower:
        return activate(AlarmBitmap::kUnderPower);
    case ElectricalAlarmTrigger::kClearUnderPower:
        return deactivate(AlarmBitmap::kUnderPower);
    case ElectricalAlarmTrigger::kSetOverCurrent:
        return activate(AlarmBitmap::kOverCurrent);
    case ElectricalAlarmTrigger::kClearOverCurrent:
        return deactivate(AlarmBitmap::kOverCurrent);
    case ElectricalAlarmTrigger::kSetUnderCurrent:
        return activate(AlarmBitmap::kUnderCurrent);
    case ElectricalAlarmTrigger::kClearUnderCurrent:
        return deactivate(AlarmBitmap::kUnderCurrent);
    case ElectricalAlarmTrigger::kSetPowerImport:
        return activate(AlarmBitmap::kPowerImported);
    case ElectricalAlarmTrigger::kClearPowerImport:
        return deactivate(AlarmBitmap::kPowerImported);
    case ElectricalAlarmTrigger::kSetPowerExport:
        return activate(AlarmBitmap::kPowerExported);
    case ElectricalAlarmTrigger::kClearPowerExport:
        return deactivate(AlarmBitmap::kPowerExported);
    default:
        return false;
    }
}
