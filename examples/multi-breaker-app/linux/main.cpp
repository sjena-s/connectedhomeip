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
#include <AppMain.h>
#include <electrical-alarm-stub.h>
#include <electrical-distribution-stub.h>
#include <electrical-protection-alarm-stub.h>
#include <power-topology-stub.h>

#include <app-common/zap-generated/ids/Clusters.h>
#include <app/clusters/identify-server/IdentifyCluster.h>
#include <app/server-cluster/ServerClusterInterfaceRegistry.h>
#include <app/util/attribute-storage.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <lib/support/CodeUtils.h>
#include <platform/ConfigurationManager.h>
#include <platform/DefaultTimerDelegate.h>

#include <cstring>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

namespace {

constexpr EndpointId kEnclosureEndpointId = 1; // Electrical Distribution Enclosure (0x0517)
constexpr EndpointId kBreaker1EndpointId  = 2; // Electrical Circuit Breaker (0x0516), has OnOff
constexpr EndpointId kBreaker2EndpointId  = 3; // Electrical Circuit Breaker (0x0516), no OnOff

// The enclosure device type requires each breaker child endpoint to carry a semantic tag from the
// Common Number namespace, so the breakers are number one and number two.
constexpr uint8_t kCommonNumberNamespaceId = 0x07;
constexpr uint8_t kNumberOneTag            = 0x01;
constexpr uint8_t kNumberTwoTag            = 0x02;

const Descriptor::Structs::SemanticTagStruct::Type kBreaker1TagList[] = {
    { .namespaceID = kCommonNumberNamespaceId, .tag = kNumberOneTag },
};
const Descriptor::Structs::SemanticTagStruct::Type kBreaker2TagList[] = {
    { .namespaceID = kCommonNumberNamespaceId, .tag = kNumberTwoTag },
};

// The three Electrical Protection Alarm instances carry deliberately different feature sets. Short
// Circuit is mandatory with the cluster on every instance; the rest differ so a test gating on a
// feature finds it on some endpoints and not others.
constexpr BitMask<ElectricalProtectionAlarm::Feature>
    kEnclosureProtectionFeatures(ElectricalProtectionAlarm::Feature::kShortCircuit, ElectricalProtectionAlarm::Feature::kOverLoad,
                                 ElectricalProtectionAlarm::Feature::kSelfTest);
constexpr BitMask<ElectricalProtectionAlarm::Feature> kBreaker1ProtectionFeatures(ElectricalProtectionAlarm::Feature::kShortCircuit,
                                                                                  ElectricalProtectionAlarm::Feature::kOverVoltage);
constexpr BitMask<ElectricalProtectionAlarm::Feature>
    kBreaker2ProtectionFeatures(ElectricalProtectionAlarm::Feature::kShortCircuit,
                                ElectricalProtectionAlarm::Feature::kResidualCurrent,
                                ElectricalProtectionAlarm::Feature::kArcFault);

// Electrical Alarm features per breaker. Over Current is mandatory with the cluster on a circuit
// breaker, so both carry it; everything else is disjoint between them.
constexpr BitMask<ElectricalAlarm::Feature> kBreaker1AlarmFeatures(ElectricalAlarm::Feature::kOverCurrent,
                                                                   ElectricalAlarm::Feature::kOverVoltage,
                                                                   ElectricalAlarm::Feature::kUnderVoltage,
                                                                   ElectricalAlarm::Feature::kOverPower,
                                                                   ElectricalAlarm::Feature::kReset);
constexpr BitMask<ElectricalAlarm::Feature> kBreaker2AlarmFeatures(ElectricalAlarm::Feature::kOverCurrent,
                                                                   ElectricalAlarm::Feature::kOverFrequency,
                                                                   ElectricalAlarm::Feature::kUnderFrequency,
                                                                   ElectricalAlarm::Feature::kUnderCurrent,
                                                                   ElectricalAlarm::Feature::kAdjustableThresholds);

// Identify is on the enclosure and the first breaker only. Both share one timer delegate, and the
// default delegate callbacks are enough: nothing here has an indicator to drive.
DefaultTimerDelegate sTimerDelegate;

RegisteredServerCluster<IdentifyCluster> gIdentifyEnclosure(
    IdentifyCluster::Config(kEnclosureEndpointId, sTimerDelegate).WithIdentifyType(Identify::IdentifyTypeEnum::kVisibleIndicator));
RegisteredServerCluster<IdentifyCluster> gIdentifyBreaker1(
    IdentifyCluster::Config(kBreaker1EndpointId, sTimerDelegate).WithIdentifyType(Identify::IdentifyTypeEnum::kVisibleIndicator));

} // namespace

void ApplicationInit()
{
    // TC-BINFO-2.1 and TC-IDM-13.1 reject the SDK's default ManufacturingDate.
    static constexpr char kManufacturingDate[] = "2026-01-01";
    VerifyOrDie(DeviceLayer::ConfigurationMgr().StoreManufacturingDate(kManufacturingDate, strlen(kManufacturingDate)) ==
                CHIP_NO_ERROR);

    // Identify is code-driven in this SDK: the cluster instances above only exist to be registered.
    VerifyOrDie(CodegenDataModelProvider::Instance().Registry().Register(gIdentifyEnclosure.Registration()) == CHIP_NO_ERROR);
    VerifyOrDie(CodegenDataModelProvider::Instance().Registry().Register(gIdentifyBreaker1.Registration()) == CHIP_NO_ERROR);

    // Electrical Distribution describes physical characteristics. The enclosure and the first
    // breaker carry it; the second breaker does not. Its generated Init callback is a no-op, so
    // the app owns each instance and registers it imperatively.
    VerifyOrDie(ElectricalDistribution::ElectricalDistributionInit(kEnclosureEndpointId) == CHIP_NO_ERROR);
    VerifyOrDie(ElectricalDistribution::ElectricalDistributionInit(kBreaker1EndpointId) == CHIP_NO_ERROR);

    // Electrical Protection Alarm reports safety faults. Same imperative registration, with a
    // different feature set on each endpoint.
    VerifyOrDie(ElectricalProtectionAlarm::ElectricalProtectionAlarmInit(kEnclosureEndpointId, kEnclosureProtectionFeatures) ==
                CHIP_NO_ERROR);
    VerifyOrDie(ElectricalProtectionAlarm::ElectricalProtectionAlarmInit(kBreaker1EndpointId, kBreaker1ProtectionFeatures) ==
                CHIP_NO_ERROR);
    VerifyOrDie(ElectricalProtectionAlarm::ElectricalProtectionAlarmInit(kBreaker2EndpointId, kBreaker2ProtectionFeatures) ==
                CHIP_NO_ERROR);

    // Power Topology is mandatory on both device types, with different features. The enclosure
    // powers its breakers as a NODE topology. Each breaker is TREE and additionally carries
    // ElectricalCircuit, which brings the ElectricalCircuitNodes attribute.
    VerifyOrDie(PowerTopology::PowerTopologyInit(
                    kEnclosureEndpointId, BitMask<PowerTopology::Feature>(PowerTopology::Feature::kNodeTopology)) == CHIP_NO_ERROR);
    constexpr BitMask<PowerTopology::Feature> kBreakerTopologyFeatures(PowerTopology::Feature::kTreeTopology,
                                                                       PowerTopology::Feature::kElectricalCircuit);
    VerifyOrDie(PowerTopology::PowerTopologyInit(kBreaker1EndpointId, kBreakerTopologyFeatures) == CHIP_NO_ERROR);
    VerifyOrDie(PowerTopology::PowerTopologyInit(kBreaker2EndpointId, kBreakerTopologyFeatures) == CHIP_NO_ERROR);

    VerifyOrDie(SetTagList(kBreaker1EndpointId, Span<const Descriptor::Structs::SemanticTagStruct::Type>(kBreaker1TagList)) ==
                CHIP_NO_ERROR);
    VerifyOrDie(SetTagList(kBreaker2EndpointId, Span<const Descriptor::Structs::SemanticTagStruct::Type>(kBreaker2TagList)) ==
                CHIP_NO_ERROR);

    // Electrical Alarm reports measurement-threshold alarms on the breakers. Over Current is
    // mandatory; the other features are split between the two breakers.
    VerifyOrDie(ElectricalAlarm::ElectricalAlarmInit(kBreaker1EndpointId, kBreaker1AlarmFeatures) == CHIP_NO_ERROR);
    VerifyOrDie(ElectricalAlarm::ElectricalAlarmInit(kBreaker2EndpointId, kBreaker2AlarmFeatures) == CHIP_NO_ERROR);
}

void ApplicationShutdown()
{
    ElectricalAlarm::ElectricalAlarmShutdown();
    ElectricalProtectionAlarm::ElectricalProtectionAlarmShutdown();
    PowerTopology::PowerTopologyShutdown();
    ElectricalDistribution::ElectricalDistributionShutdown();
    LogErrorOnFailure(CodegenDataModelProvider::Instance().Registry().Unregister(&gIdentifyBreaker1.Cluster()));
    LogErrorOnFailure(CodegenDataModelProvider::Instance().Registry().Unregister(&gIdentifyEnclosure.Cluster()));
}

int main(int argc, char * argv[])
{
    if (ChipLinuxAppInit(argc, argv) != 0)
    {
        return -1;
    }

    ChipLinuxAppMainLoop();
    return 0;
}
