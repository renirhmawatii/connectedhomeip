/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
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

#include "pw_rpc/server.h"
#include "pw_rpc_system_server/rpc_server.h"
#include "pw_rpc_system_server/socket.h"

#include <app/clusters/ecosystem-information-server/ecosystem-information-server.h>
#include <lib/core/CHIPError.h>

#include <string>
#include <thread>

#if defined(PW_RPC_FABRIC_BRIDGE_SERVICE) && PW_RPC_FABRIC_BRIDGE_SERVICE
#include "pigweed/rpc_services/FabricBridge.h"
#endif

#include "BridgedDevice.h"
#include "BridgedDeviceManager.h"

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

namespace {

#if defined(PW_RPC_FABRIC_BRIDGE_SERVICE) && PW_RPC_FABRIC_BRIDGE_SERVICE
class FabricBridge final : public chip::rpc::FabricBridge
{
public:
    pw::Status AddSynchronizedDevice(const chip_rpc_SynchronizedDevice & request, pw_protobuf_Empty & response) override;
    pw::Status RemoveSynchronizedDevice(const chip_rpc_SynchronizedDevice & request, pw_protobuf_Empty & response) override;
    pw::Status ActiveChanged(const chip_rpc_KeepActiveChanged & request, pw_protobuf_Empty & response) override;
};

pw::Status FabricBridge::AddSynchronizedDevice(const chip_rpc_SynchronizedDevice & request, pw_protobuf_Empty & response)
{
    NodeId nodeId = request.node_id;
    ChipLogProgress(NotSpecified, "Received AddSynchronizedDevice: " ChipLogFormatX64, ChipLogValueX64(nodeId));

    auto device = std::make_unique<BridgedDevice>(nodeId);
    device->SetReachable(true);

    BridgedDevice::BridgedAttributes attributes;

    if (request.has_unique_id)
    {
        attributes.uniqueId = request.unique_id;
    }

    if (request.has_vendor_name)
    {
        attributes.vendorName = request.vendor_name;
    }

    if (request.has_vendor_id)
    {
        attributes.vendorId = request.vendor_id;
    }

    if (request.has_product_name)
    {
        attributes.productName = request.product_name;
    }

    if (request.has_product_id)
    {
        attributes.productId = request.product_id;
    }

    if (request.has_node_label)
    {
        attributes.nodeLabel = request.node_label;
    }

    if (request.has_hardware_version)
    {
        attributes.hardwareVersion = request.hardware_version;
    }

    if (request.has_hardware_version_string)
    {
        attributes.hardwareVersionString = request.hardware_version_string;
    }

    if (request.has_software_version)
    {
        attributes.softwareVersion = request.software_version;
    }

    if (request.has_software_version_string)
    {
        attributes.softwareVersionString = request.software_version_string;
    }

    device->SetBridgedAttributes(attributes);
    device->SetIcd(request.has_is_icd && request.is_icd);

    auto result = BridgeDeviceMgr().AddDeviceEndpoint(std::move(device), 1 /* parentEndpointId */);
    if (!result.has_value())
    {
        ChipLogError(NotSpecified, "Failed to add device with nodeId=0x" ChipLogFormatX64, ChipLogValueX64(nodeId));
        return pw::Status::Unknown();
    }

    BridgedDevice * addedDevice = BridgeDeviceMgr().GetDeviceByNodeId(nodeId);
    VerifyOrDie(addedDevice);

    CHIP_ERROR err = EcosystemInformation::EcosystemInformationServer::Instance().AddEcosystemInformationClusterToEndpoint(
        addedDevice->GetEndpointId());
    VerifyOrDie(err == CHIP_NO_ERROR);

    return pw::OkStatus();
}

pw::Status FabricBridge::RemoveSynchronizedDevice(const chip_rpc_SynchronizedDevice & request, pw_protobuf_Empty & response)
{
    NodeId nodeId = request.node_id;
    ChipLogProgress(NotSpecified, "Received RemoveSynchronizedDevice: " ChipLogFormatX64, ChipLogValueX64(nodeId));

    auto removed_idx = BridgeDeviceMgr().RemoveDeviceByNodeId(nodeId);
    if (!removed_idx.has_value())
    {
        ChipLogError(NotSpecified, "Failed to remove device with nodeId=0x" ChipLogFormatX64, ChipLogValueX64(nodeId));
        return pw::Status::NotFound();
    }

    return pw::OkStatus();
}

pw::Status FabricBridge::ActiveChanged(const chip_rpc_KeepActiveChanged & request, pw_protobuf_Empty & response)
{
    NodeId nodeId = request.node_id;
    ChipLogProgress(NotSpecified, "Received ActiveChanged: " ChipLogFormatX64, ChipLogValueX64(nodeId));

    auto * device = BridgeDeviceMgr().GetDeviceByNodeId(nodeId);
    if (device == nullptr)
    {
        ChipLogError(NotSpecified, "Could not find bridged device associated with nodeId=0x" ChipLogFormatX64,
                     ChipLogValueX64(nodeId));
        return pw::Status::NotFound();
    }

    device->LogActiveChangeEvent(request.promised_active_duration_ms);
    return pw::OkStatus();
}

FabricBridge fabric_bridge_service;
#endif // defined(PW_RPC_FABRIC_BRIDGE_SERVICE) && PW_RPC_FABRIC_BRIDGE_SERVICE

void RegisterServices(pw::rpc::Server & server)
{
#if defined(PW_RPC_FABRIC_BRIDGE_SERVICE) && PW_RPC_FABRIC_BRIDGE_SERVICE
    server.RegisterService(fabric_bridge_service);
#endif
}

} // namespace

void RunRpcService()
{
    pw::rpc::system_server::Init();
    RegisterServices(pw::rpc::system_server::Server());
    pw::rpc::system_server::Start();
}

void InitRpcServer(uint16_t rpcServerPort)
{
    pw::rpc::system_server::set_socket_port(rpcServerPort);
    std::thread rpc_service(RunRpcService);
    rpc_service.detach();
}