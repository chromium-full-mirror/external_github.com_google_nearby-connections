
// Copyright 2020 Google LLC
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

#include "connections/implementation/mediums/wifi_hotspot.h"

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "absl/cleanup/cleanup.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "connections/implementation/endpoint_channel.h"
#include "internal/platform/cancellation_flag.h"
#include "internal/platform/count_down_latch.h"
#include "internal/platform/expected.h"
#include "internal/platform/feature_flags.h"
#include "internal/platform/medium_environment.h"
#include "internal/platform/service_address.h"
#include "internal/platform/single_thread_executor.h"
#include "internal/platform/wifi_credential.h"
#include "internal/platform/wifi_hotspot.h"

namespace nearby {
namespace connections {
namespace {

using FeatureFlags = FeatureFlags::Flags;

constexpr absl::string_view kServiceID{"com.google.location.nearby.apps.test"};
constexpr absl::string_view kSsid{"Direct-357a2d8c"};
constexpr absl::string_view kPassword{"12345678"};

class WifiHotspotTest : public testing::TestWithParam<FeatureFlags> {
 protected:
  WifiHotspotTest() {
    env_.Stop();
    env_.Start();
  }
  ~WifiHotspotTest() override { env_.Stop(); }

  MediumEnvironment& env_{MediumEnvironment::Instance()};
};

// Verifies that WifiHotspot instances report client STA availability on
// construction.
TEST_F(WifiHotspotTest, ConstructorDestructorWorks) {
  auto wifi_hotspot_a = std::make_unique<WifiHotspot>();
  auto wifi_hotspot_b = std::make_unique<WifiHotspot>();

  EXPECT_TRUE(wifi_hotspot_a->IsClientAvailable());
  EXPECT_TRUE(wifi_hotspot_b->IsClientAvailable());
}

// Verifies that connecting to a non-existent SoftAP fails and disconnecting
// succeeds.
TEST_F(WifiHotspotTest, CanConnectDisconnectHotspot) {
  auto wifi_hotspot_a = std::make_unique<WifiHotspot>();
  HotspotCredentials hotspot_credentials;
  hotspot_credentials.SetSSID(std::string(kSsid));
  hotspot_credentials.SetPassword(std::string(kPassword));

  EXPECT_FALSE(wifi_hotspot_a->ConnectWifiHotspot(hotspot_credentials));
  EXPECT_TRUE(wifi_hotspot_a->DisconnectWifiHotspot());
}

#if !defined(__APPLE__)

constexpr FeatureFlags kTestCases[] = {
    FeatureFlags{
        .enable_cancellation_flag = true,
    },
    FeatureFlags{
        .enable_cancellation_flag = false,
    },
};

INSTANTIATE_TEST_SUITE_P(ParametrisedWifiHotspotMediumTest, WifiHotspotTest,
                         testing::ValuesIn(kTestCases));

// Verifies starting, accepting connections on, and stopping a local SoftAP
// when AP mode is supported.
TEST_F(WifiHotspotTest, CanStartStopHotspot) {
  std::string service_id(kServiceID);
  auto wifi_hotspot_a = std::make_unique<WifiHotspot>();

  ASSERT_TRUE(wifi_hotspot_a->IsAPAvailable());
  EXPECT_NE(wifi_hotspot_a->CreateBwuHandler(nullptr), nullptr);
  EXPECT_TRUE(wifi_hotspot_a->StartWifiHotspot());
  EXPECT_TRUE(wifi_hotspot_a->StartAcceptingConnections(service_id, {}));
  EXPECT_TRUE(wifi_hotspot_a->StopWifiHotspot());
}

// Verifies that StartAcceptingConnections rejects an empty service ID even when
// a local SoftAP is active.
TEST_F(WifiHotspotTest, StartAcceptingConnectionsFailsWithEmptyServiceId) {
  auto wifi_hotspot_a = std::make_unique<WifiHotspot>();
  ASSERT_TRUE(wifi_hotspot_a->StartWifiHotspot());

  EXPECT_FALSE(wifi_hotspot_a->StartAcceptingConnections("", {}));
  EXPECT_FALSE(wifi_hotspot_a->IsAcceptingConnections(""));

  EXPECT_TRUE(wifi_hotspot_a->StopWifiHotspot());
}

// Verifies that a local SoftAP can accept STA connections when AP mode is
// available.
TEST_P(WifiHotspotTest, CanStartHotspotThatOtherConnect) {
  FeatureFlags feature_flags = GetParam();
  env_.SetFeatureFlags(feature_flags);

  std::string service_id(kServiceID);
  auto wifi_hotspot_a = std::make_unique<WifiHotspot>();
  auto wifi_hotspot_b = std::make_unique<WifiHotspot>();

  EXPECT_TRUE(wifi_hotspot_a->StartWifiHotspot());
  if (!wifi_hotspot_a->IsAcceptingConnections(service_id)) {
    EXPECT_TRUE(wifi_hotspot_a->StartAcceptingConnections(service_id, {}));
  }

  HotspotCredentials* hotspot_credentials =
      wifi_hotspot_a->GetCredentials(service_id);

  EXPECT_TRUE(wifi_hotspot_b->ConnectWifiHotspot(*hotspot_credentials));

  ServiceAddress service_address = {
      .address = {123, static_cast<char>(234), 23, 1},
      .port = 20,
  };
  CancellationFlag flag;
  ErrorOr<std::unique_ptr<EndpointChannel>> channel_result =
      wifi_hotspot_b->Connect(service_id, {service_address}, &flag);
  EXPECT_TRUE(channel_result.has_error());

  channel_result = wifi_hotspot_b->Connect(
      service_id, hotspot_credentials->GetAddressCandidates(), &flag);
  EXPECT_TRUE(channel_result.has_value());
  EXPECT_TRUE(channel_result.value());

  EXPECT_TRUE(wifi_hotspot_b->DisconnectWifiHotspot());
  EXPECT_TRUE(wifi_hotspot_a->StopWifiHotspot());
}

// Verifies that an outgoing STA socket connection respects cancellation flags.
TEST_P(WifiHotspotTest, CanStartHotspotThatOtherCanCancelConnect) {
  FeatureFlags feature_flags = GetParam();
  env_.SetFeatureFlags(feature_flags);

  std::string service_id(kServiceID);
  auto wifi_hotspot_a = std::make_unique<WifiHotspot>();
  auto wifi_hotspot_b = std::make_unique<WifiHotspot>();

  EXPECT_TRUE(wifi_hotspot_a->StartWifiHotspot());
  if (!wifi_hotspot_a->IsAcceptingConnections(service_id)) {
    EXPECT_TRUE(wifi_hotspot_a->StartAcceptingConnections(service_id, {}));
  }

  HotspotCredentials* hotspot_credentials =
      wifi_hotspot_a->GetCredentials(service_id);

  EXPECT_TRUE(wifi_hotspot_b->ConnectWifiHotspot(*hotspot_credentials));

  CancellationFlag flag(true);
  ErrorOr<std::unique_ptr<EndpointChannel>> channel_result =
      wifi_hotspot_b->Connect(
          service_id, hotspot_credentials->GetAddressCandidates(), &flag);

  // If FeatureFlag is disabled, Cancelled is false as no-op.
  if (!feature_flags.enable_cancellation_flag) {
    EXPECT_TRUE(channel_result.has_value());
    EXPECT_TRUE(channel_result.value());
    EXPECT_TRUE(wifi_hotspot_b->DisconnectWifiHotspot());
    EXPECT_TRUE(wifi_hotspot_a->StopWifiHotspot());
  } else {
    EXPECT_TRUE(channel_result.has_error());
    EXPECT_TRUE(wifi_hotspot_b->DisconnectWifiHotspot());
    EXPECT_TRUE(wifi_hotspot_a->StopWifiHotspot());
  }
}

// Verifies that STA connection fails when credentials do not match the active
// SoftAP.
TEST_F(WifiHotspotTest, CanStartHotspotTheOtherFailConnect) {
  auto wifi_hotspot_a = std::make_unique<WifiHotspot>();
  auto wifi_hotspot_b = std::make_unique<WifiHotspot>();

  EXPECT_TRUE(wifi_hotspot_a->StartWifiHotspot());

  HotspotCredentials hotspot_credentials;
  hotspot_credentials.SetSSID(std::string(kSsid));
  hotspot_credentials.SetPassword(std::string(kPassword));
  EXPECT_FALSE(wifi_hotspot_b->ConnectWifiHotspot(hotspot_credentials));
  EXPECT_TRUE(wifi_hotspot_b->DisconnectWifiHotspot());

  EXPECT_TRUE(wifi_hotspot_a->StopWifiHotspot());
}

#else  // defined(__APPLE__)

// Verifies that on Apple platforms SoftAP (AP) mode and server operations are
// disabled while Client (STA) mode and BWU handler creation remain enabled.
TEST_F(WifiHotspotTest, APIsDisabledAndClientIsEnabledOnApple) {
  std::string service_id(kServiceID);
  auto wifi_hotspot_a = std::make_unique<WifiHotspot>();

  EXPECT_FALSE(wifi_hotspot_a->IsAPAvailable());
  EXPECT_TRUE(wifi_hotspot_a->IsClientAvailable());
  EXPECT_FALSE(wifi_hotspot_a->IsHotspotStarted());
  EXPECT_FALSE(wifi_hotspot_a->StartWifiHotspot());
  EXPECT_FALSE(wifi_hotspot_a->IsHotspotStarted());
  EXPECT_TRUE(wifi_hotspot_a->StopWifiHotspot());
  EXPECT_FALSE(wifi_hotspot_a->StartAcceptingConnections(service_id, {}));
  EXPECT_FALSE(wifi_hotspot_a->StopAcceptingConnections(service_id));
  EXPECT_FALSE(wifi_hotspot_a->IsAcceptingConnections(service_id));
  EXPECT_NE(wifi_hotspot_a->CreateBwuHandler(nullptr), nullptr);
}

#endif  // !defined(__APPLE__)

// Verifies that WifiHotspot can connect as a Client (STA) to a remote peer's
// SoftAP even when local SoftAP creation (IsAPAvailable) is unsupported.
TEST_F(WifiHotspotTest, ClientStaCanConnectToRemoteHotspot) {
  WifiHotspotMedium remote_ap_medium;
  ASSERT_TRUE(remote_ap_medium.StartWifiHotspot());
  WifiHotspotServerSocket remote_server_socket =
      remote_ap_medium.ListenForService();
  ASSERT_TRUE(remote_server_socket.IsValid());

  HotspotCredentials* remote_credentials = remote_ap_medium.GetCredential();
  ASSERT_NE(remote_credentials, nullptr);
  remote_server_socket.PopulateHotspotCredentials(*remote_credentials);

  WifiHotspotSocket accepted_remote_socket;
  CountDownLatch accept_latch(1);
  SingleThreadExecutor remote_accept_executor;
  // Declared after remote_accept_executor so reverse destruction order closes
  // remote_server_socket (unblocking Accept()) before joining the executor.
  absl::Cleanup remote_cleanup = [&]() {
    remote_server_socket.Close();
    remote_ap_medium.StopWifiHotspot();
  };
  remote_accept_executor.Execute([&]() {
    accepted_remote_socket = remote_server_socket.Accept();
    accept_latch.CountDown();
  });

  auto wifi_hotspot_sta = std::make_unique<WifiHotspot>();
  EXPECT_TRUE(wifi_hotspot_sta->IsClientAvailable());
  EXPECT_TRUE(wifi_hotspot_sta->ConnectWifiHotspot(*remote_credentials));
  EXPECT_TRUE(wifi_hotspot_sta->IsConnectedToHotspot());

  CancellationFlag flag;
  ErrorOr<std::unique_ptr<EndpointChannel>> channel_result =
      wifi_hotspot_sta->Connect(std::string(kServiceID),
                                remote_credentials->GetAddressCandidates(),
                                &flag);
  ASSERT_TRUE(channel_result.has_value());
  ASSERT_NE(channel_result.value(), nullptr);
  EXPECT_EQ(channel_result.value()->GetMedium(),
            location::nearby::proto::connections::Medium::WIFI_HOTSPOT);
  channel_result.value()->Close();
  ASSERT_TRUE(accept_latch.Await(absl::Seconds(5)).result());
  ASSERT_TRUE(accepted_remote_socket.IsValid());
  accepted_remote_socket.Close();

  EXPECT_TRUE(wifi_hotspot_sta->DisconnectWifiHotspot());
  EXPECT_FALSE(wifi_hotspot_sta->IsConnectedToHotspot());
}

}  // namespace
}  // namespace connections
}  // namespace nearby
