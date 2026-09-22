// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "connections/implementation/mediums/bluetooth/bluetooth_classic_stub.h"

#include <memory>
#include <string>

#include "connections/implementation/bwu_handler.h"
#include "connections/implementation/endpoint_channel.h"
#include "connections/implementation/mediums/bluetooth_radio.h"
#include "internal/platform/bluetooth_adapter.h"
#include "internal/platform/cancellation_flag.h"
#include "internal/platform/expected.h"
#include "internal/platform/mac_address.h"

namespace nearby {
namespace connections {

BluetoothClassicStub::BluetoothClassicStub(BluetoothRadio& radio) {}

BluetoothClassicStub::~BluetoothClassicStub() = default;

bool BluetoothClassicStub::IsAvailable() const { return false; }

bool BluetoothClassicStub::TurnOffDiscoverability() { return false; }

bool BluetoothClassicStub::StopDiscovery(const std::string& serviceId) {
  return false;
}

void BluetoothClassicStub::StopAllDiscovery() {}

bool BluetoothClassicStub::IsAcceptingConnections(
    const std::string& service_id) {
  return false;
}

bool BluetoothClassicStub::StopAcceptingConnections(
    const std::string& service_id) {
  return false;
}

bool BluetoothClassicStub::IsMediumValid() const { return false; }

bool BluetoothClassicStub::IsAdapterValid() const { return false; }

MacAddress BluetoothClassicStub::GetAddress() const { return MacAddress(); }

BluetoothDevice BluetoothClassicStub::GetRemoteDevice(MacAddress mac_address) {
  return BluetoothDevice();
}

bool BluetoothClassicStub::IsDiscovering(const std::string& serviceId) const {
  return false;
}

std::unique_ptr<BwuHandler> BluetoothClassicStub::CreateBwuHandler(
    BwuHandler::IncomingConnectionCallback incoming_connection_callback) {
  return nullptr;
}

ErrorOr<bool> BluetoothClassicStub::TurnOnDiscoverability(
    const std::string& device_name) {
  return {Error(location::nearby::proto::connections::OperationResultCode::
                    MEDIUM_UNAVAILABLE_BLUETOOTH_NOT_AVAILABLE)};
}

ErrorOr<bool> BluetoothClassicStub::StartDiscovery(
    const std::string& serviceId, DiscoveredDeviceCallback callback) {
  return {Error(location::nearby::proto::connections::OperationResultCode::
                    MEDIUM_UNAVAILABLE_BLUETOOTH_NOT_AVAILABLE)};
}

ErrorOr<bool> BluetoothClassicStub::StartAcceptingConnections(
    const std::string& service_id, AcceptedConnectionCallback callback,
    bool for_upgrade) {
  return {Error(location::nearby::proto::connections::OperationResultCode::
                    MEDIUM_UNAVAILABLE_BLUETOOTH_NOT_AVAILABLE)};
}

ErrorOr<std::unique_ptr<EndpointChannel>> BluetoothClassicStub::Connect(
    BluetoothDevice& bluetooth_device, const std::string& service_id,
    const std::string& local_service_id, const std::string& channel_name,
    CancellationFlag* cancellation_flag) {
  return {Error(location::nearby::proto::connections::OperationResultCode::
                    MEDIUM_UNAVAILABLE_BLUETOOTH_NOT_AVAILABLE)};
}

}  // namespace connections
}  // namespace nearby
