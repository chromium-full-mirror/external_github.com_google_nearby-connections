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

#include "connections/implementation/mediums/wifi_direct_stub.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "connections/implementation/bwu_handler.h"
#include "connections/implementation/endpoint_channel.h"
#include "internal/platform/cancellation_flag.h"
#include "internal/platform/expected.h"
#include "internal/platform/wifi_credential.h"

namespace nearby {
namespace connections {

WifiDirectStub::WifiDirectStub() = default;
WifiDirectStub::~WifiDirectStub() = default;

bool WifiDirectStub::IsGOAvailable() const { return false; }
bool WifiDirectStub::IsGCAvailable() const { return false; }
bool WifiDirectStub::IsGOStarted() { return false; }
bool WifiDirectStub::StartWifiDirect() { return false; }
bool WifiDirectStub::StopWifiDirect() { return false; }
bool WifiDirectStub::IsConnectedToGO() { return false; }
bool WifiDirectStub::ConnectWifiDirect(
    const WifiDirectCredentials& wifi_direct_credentials) {
  return false;
}
bool WifiDirectStub::DisconnectWifiDirect() { return false; }
bool WifiDirectStub::StartAcceptingConnections(
    const std::string& service_id, AcceptedConnectionCallback callback) {
  return false;
}
bool WifiDirectStub::StopAcceptingConnections(const std::string& service_id) {
  return false;
}
bool WifiDirectStub::IsAcceptingConnections(const std::string& service_id) {
  return false;
}

WifiDirectCredentials* WifiDirectStub::GetCredentials(
    absl::string_view service_id) {
  return nullptr;
}

std::vector<WifiDirectStub::WifiDirectAuthType>
WifiDirectStub::GetSupportedWifiDirectAuthTypes() const {
  return {};
}

WifiDirectStub::WifiDirectAuthType
WifiDirectStub::GetPreferredWifiDirectAuthType() const {
  return WifiDirectAuthType::WIFI_DIRECT_TYPE_UNKNOWN;
}

bool WifiDirectStub::SetPreferredWifiDirectAuthType(
    WifiDirectAuthType auth_type) {
  return false;
}

std::unique_ptr<BwuHandler> WifiDirectStub::CreateBwuHandler(
    BwuHandler::IncomingConnectionCallback incoming_connection_callback) {
  return nullptr;
}

ErrorOr<std::unique_ptr<EndpointChannel>> WifiDirectStub::Connect(
    const std::string& service_id, const std::string& ip_address, int port,
    CancellationFlag* cancellation_flag) {
  return {Error(location::nearby::proto::connections::OperationResultCode::
                    MEDIUM_UNAVAILABLE_WIFI_DIRECT_NOT_AVAILABLE)};
}

}  // namespace connections
}  // namespace nearby
