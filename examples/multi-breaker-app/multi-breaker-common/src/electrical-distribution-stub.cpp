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

#include <electrical-distribution-stub.h>

#include <lib/support/CodeUtils.h>

#include <algorithm>
#include <array>
#include <memory>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ElectricalDistribution;
using namespace chip::app::Clusters::ElectricalDistribution::Attributes;

namespace {

// This app performs no real measurement, so publish representative fixed values.
const ElectricalDistributionCluster::StartupConfiguration kDefaultConfig = {
    .maxContinuousCurrent = DataModel::MakeNullable<int64_t>(100000), // 100 A, in mA
    .maxVoltage           = DataModel::MakeNullable<int64_t>(240000), // 240 V, in mV
    .numberOfPoles        = DataModel::MakeNullable<uint16_t>(2),
    .endOfLife            = DataModel::MakeNullable(EndOfLifeEnum::kNone),
    .serviceEntranceRated = DataModel::MakeNullable(true),
};

// One per endpoint carrying the cluster: the enclosure and the first breaker.
constexpr size_t kMaxInstances = 2;
std::array<std::unique_ptr<ElectricalDistribution::Instance>, kMaxInstances> gInstances;

} // namespace

namespace chip::app::Clusters::ElectricalDistribution {

CHIP_ERROR ElectricalDistributionInit(EndpointId endpointId)
{
    auto slot = std::find_if(gInstances.begin(), gInstances.end(), [](const auto & i) { return i == nullptr; });
    VerifyOrReturnError(slot != gInstances.end(), CHIP_ERROR_NO_MEMORY);

    *slot = std::make_unique<Instance>(endpointId, kDefaultConfig);
    VerifyOrReturnError(*slot != nullptr, CHIP_ERROR_NO_MEMORY);

    CHIP_ERROR err = (*slot)->Init();
    if (err != CHIP_NO_ERROR)
    {
        slot->reset();
    }
    return err;
}

void ElectricalDistributionShutdown()
{
    // ~Instance() unregisters.
    for (auto & instance : gInstances)
    {
        instance.reset();
    }
}

CHIP_ERROR SetEndOfLife(const Attributes::EndOfLife::TypeInfo::Type & endOfLife)
{
    // The endpoint is not known to the caller (trigger handlers have it stripped), so the value
    // applies to every instance.
    bool any = false;
    for (auto & instance : gInstances)
    {
        if (instance != nullptr)
        {
            ReturnErrorOnFailure(instance->Cluster().SetEndOfLife(endOfLife));
            any = true;
        }
    }
    return any ? CHIP_NO_ERROR : CHIP_ERROR_INCORRECT_STATE;
}

} // namespace chip::app::Clusters::ElectricalDistribution
