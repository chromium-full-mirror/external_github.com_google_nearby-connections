// Copyright 2022 Google LLC
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

#include "connections/implementation/mediums/wifi_hotspot_bwu_handler.h"

#include <memory>
#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "absl/cleanup/cleanup.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "connections/implementation/bwu_handler.h"
#include "connections/implementation/client_proxy.h"
#include "connections/implementation/endpoint_channel.h"
#include "connections/implementation/mediums/mediums.h"
#include "connections/implementation/offline_frames.h"
#include "internal/flags/nearby_flags.h"
#include "internal/platform/count_down_latch.h"
#include "internal/platform/exception.h"
#include "internal/platform/expected.h"
#include "internal/platform/feature_flags.h"
#include "internal/platform/flags/nearby_platform_feature_flags.h"
#include "internal/platform/logging.h"
#include "internal/platform/medium_environment.h"
#include "internal/platform/service_address.h"
#include "internal/platform/single_thread_executor.h"
#include "internal/platform/wifi_credential.h"
#include "internal/platform/wifi_hotspot.h"

namespace nearby {
namespace connections {

namespace {
using ::location::nearby::connections::OfflineFrame;
using UpgradePathInfo = ::location::nearby::connections::
    BandwidthUpgradeNegotiationFrame::UpgradePathInfo;
using ::location::nearby::proto::connections::OperationResultCode;
constexpr absl::Duration kWaitDuration = absl::Milliseconds(1000);
constexpr absl::string_view kServiceID{"com.google.location.nearby.apps.test"};
constexpr absl::string_view kEndpointID{"Hotspot_Server"};
}  // namespace

class WifiHotspotTest : public testing::Test {
 protected:
  WifiHotspotTest() { env_.Start(); }
  ~WifiHotspotTest() override { env_.Stop(); }
  void SetUp() override {
    nearby::NearbyFlags::GetInstance().OverrideInt64FlagValue(
        platform::config_package_nearby::nearby_platform_feature::
            kWifiHotspotConnectionIntervalMillis,
        1);
  }
  void TearDown() override {
    nearby::NearbyFlags::GetInstance().ResetOverridedValues();
  }

  MediumEnvironment& env_{MediumEnvironment::Instance()};
};

// Verifies that WifiHotspotBwuHandler can initialize and revert initiator
// state.
TEST_F(WifiHotspotTest, CanCreateBwuHandler) {
  ClientProxy client;
  Mediums mediums;

  auto handler = std::make_unique<WifiHotspotBwuHandler>(
      &mediums.GetWifiHotspot(), nullptr);

  handler->InitializeUpgradedMediumForEndpoint(&client, std::string(kServiceID),
                                               std::string(kEndpointID));
  handler->RevertInitiatorState();
  SUCCEED();
  handler.reset();
}

#if !defined(__APPLE__)

// Verifies SoftAP Host BWU initialization and STA endpoint channel creation
// when the platform supports hosting a SoftAP.
TEST_F(WifiHotspotTest, SoftAPBWUInit_STACreateEndpointChannel) {
  CountDownLatch start_latch(1);
  CountDownLatch accept_latch(1);
  CountDownLatch end_latch(1);

  ClientProxy client_hotspot_ap, client_hotspot_sta;
  Mediums mediums_HS_ap, mediums_HS_sta;
  ExceptionOr<OfflineFrame> upgrade_frame;

  auto handler_1 = std::make_unique<WifiHotspotBwuHandler>(
      &mediums_HS_ap.GetWifiHotspot(),
      [&](ClientProxy* client,
          std::unique_ptr<BwuHandler::IncomingSocketConnection>
              mutable_connection) {
        LOG(INFO) << "Server socket connection accept call back";
        accept_latch.CountDown();
        EXPECT_TRUE(end_latch.Await(kWaitDuration).result());
      });

  // client_hotspot_ap works as Hotspot SoftAP
  SingleThreadExecutor server_executor;
  server_executor.Execute([&]() {
    std::string upgrade_path_available_frame =
        handler_1->InitializeUpgradedMediumForEndpoint(
            &client_hotspot_ap, std::string(kServiceID),
            std::string(kEndpointID));
    EXPECT_FALSE(upgrade_path_available_frame.empty());

    upgrade_frame = parser::FromBytes(upgrade_path_available_frame);
    start_latch.CountDown();
  });

  client_hotspot_sta.AddCancellationFlag(std::string(kEndpointID));
  // client_hotspot_sta works as Hotspot STA which will connect to
  // client_hotspot_ap
  SingleThreadExecutor client_executor;
  // Wait till client_hotspot_ap started as hotspot and then connect to it
  EXPECT_TRUE(start_latch.Await(kWaitDuration).result());
  std::unique_ptr<BwuHandler> handler_2 =
      std::make_unique<WifiHotspotBwuHandler>(&mediums_HS_sta.GetWifiHotspot(),
                                              nullptr);

  client_executor.Execute([&]() {
    UpgradePathInfo upgrade_path_info;
    ErrorOr<std::unique_ptr<EndpointChannel>> result_error =
        handler_2->CreateUpgradedEndpointChannel(
            &client_hotspot_sta, std::string(kServiceID),
            std::string(kEndpointID), upgrade_path_info);
    EXPECT_TRUE(result_error.has_error());

    auto bwu_frame =
        upgrade_frame.result().v1().bandwidth_upgrade_negotiation();

    ErrorOr<std::unique_ptr<EndpointChannel>> result =
        handler_2->CreateUpgradedEndpointChannel(
            &client_hotspot_sta, std::string(kServiceID),
            std::string(kEndpointID), bwu_frame.upgrade_path_info());
    EXPECT_EQ(handler_2->GetUpgradeMedium(),
              location::nearby::proto::connections::Medium::WIFI_HOTSPOT);

    if (!client_hotspot_sta.GetCancellationFlag(std::string(kEndpointID))
             ->Cancelled()) {
      ASSERT_TRUE(result.has_value());
      std::unique_ptr<EndpointChannel> new_channel = std::move(result.value());
      EXPECT_TRUE(accept_latch.Await(kWaitDuration).result());
      EXPECT_EQ(new_channel->GetMedium(),
                location::nearby::proto::connections::Medium::WIFI_HOTSPOT);
      new_channel->Close();
    } else {
      EXPECT_FALSE(result.has_value());
      EXPECT_TRUE(result.has_error());
      EXPECT_EQ(
          result.error().operation_result_code(),
          OperationResultCode::
              CLIENT_CANCELLATION_CANCEL_WIFI_HOTSPOT_OUTGOING_CONNECTION);
      accept_latch.CountDown();
    }
    EXPECT_TRUE(mediums_HS_sta.GetWifiHotspot().IsConnectedToHotspot());
    handler_2->RevertResponderState(std::string(kServiceID));
    end_latch.CountDown();
  });
  handler_2->OnEndpointDisconnect(&client_hotspot_sta,
                                  std::string(kEndpointID));

  EXPECT_TRUE(accept_latch.Await(kWaitDuration).result());
  EXPECT_TRUE(end_latch.Await(kWaitDuration).result());
  EXPECT_FALSE(mediums_HS_sta.GetWifiHotspot().IsConnectedToHotspot());
}

#else  // defined(__APPLE__)

// Verifies that SoftAP Host BWU initialization returns an empty frame on Apple
// platforms where hosting a local SoftAP is unsupported.
TEST_F(WifiHotspotTest, InitializeUpgradedMediumFailsOnAppleWhenApUnavailable) {
  ClientProxy client_ap;
  Mediums mediums_ap;
  EXPECT_FALSE(mediums_ap.GetWifiHotspot().IsAPAvailable());
  WifiHotspotBwuHandler handler_ap(&mediums_ap.GetWifiHotspot(), nullptr);

  EXPECT_TRUE(handler_ap
                  .InitializeUpgradedMediumForEndpoint(&client_ap,
                                                       std::string(kServiceID),
                                                       std::string(kEndpointID))
                  .empty());
  handler_ap.RevertInitiatorState();
}

#endif  // !defined(__APPLE__)

// Verifies that the Client (STA) BWU role independently joins a remote peer's
// SoftAP and establishes an upgraded WIFI_HOTSPOT EndpointChannel even on
// platforms (such as Apple) where local SoftAP creation (IsAPAvailable) is
// unsupported.
TEST_F(WifiHotspotTest, ClientStaRoleJoinsRemoteSoftApIndependently) {
  // Simulate a remote Android/Windows peer hosting a SoftAP via platform API.
  WifiHotspotMedium remote_ap_medium;
  ASSERT_TRUE(remote_ap_medium.StartWifiHotspot());
  WifiHotspotServerSocket remote_server_socket =
      remote_ap_medium.ListenForService();
  ASSERT_TRUE(remote_server_socket.IsValid());

  HotspotCredentials* remote_credentials = remote_ap_medium.GetCredential();
  ASSERT_NE(remote_credentials, nullptr);
  remote_server_socket.PopulateHotspotCredentials(*remote_credentials);

  CountDownLatch accept_latch(1);
  WifiHotspotSocket accepted_remote_socket;
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

  UpgradePathInfo::WifiHotspotCredentials proto_credentials;
  proto_credentials.set_ssid(remote_credentials->GetSSID());
  proto_credentials.set_password(remote_credentials->GetPassword());
  proto_credentials.set_frequency(remote_credentials->GetFrequency());
  for (const auto& candidate : remote_credentials->GetAddressCandidates()) {
    ServiceAddressToProto(candidate,
                          *proto_credentials.add_address_candidates());
  }
  ExceptionOr<OfflineFrame> upgrade_frame =
      parser::FromBytes(parser::ForBwuWifiHotspotPathAvailable(
          std::move(proto_credentials),
          /*supports_disabling_encryption=*/false));
  ASSERT_TRUE(upgrade_frame.ok());

  ClientProxy client_sta;
  client_sta.AddCancellationFlag(std::string(kEndpointID));
  Mediums mediums_sta;
  EXPECT_TRUE(mediums_sta.GetWifiHotspot().IsClientAvailable());
  WifiHotspotBwuHandler handler_sta(&mediums_sta.GetWifiHotspot(), nullptr);

  ErrorOr<std::unique_ptr<EndpointChannel>> result =
      handler_sta.CreateUpgradedEndpointChannel(
          &client_sta, std::string(kServiceID), std::string(kEndpointID),
          upgrade_frame.result()
              .v1()
              .bandwidth_upgrade_negotiation()
              .upgrade_path_info());
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(accept_latch.Await(kWaitDuration).result());
  ASSERT_TRUE(accepted_remote_socket.IsValid());
  EXPECT_EQ(result.value()->GetMedium(),
            location::nearby::proto::connections::Medium::WIFI_HOTSPOT);
  EXPECT_TRUE(mediums_sta.GetWifiHotspot().IsConnectedToHotspot());

  result.value()->Close();
  accepted_remote_socket.Close();
  handler_sta.RevertResponderState(std::string(kServiceID));
  EXPECT_FALSE(mediums_sta.GetWifiHotspot().IsConnectedToHotspot());
}

// Verifies that the Client (STA) BWU role returns a legacy STA connection
// failure error and leaves the STA disconnected when the remote SoftAP is
// unreachable.
TEST_F(WifiHotspotTest, ClientStaRoleFailsWhenRemoteSoftApUnreachable) {
  ClientProxy client_sta;
  client_sta.AddCancellationFlag(std::string(kEndpointID));
  Mediums mediums_sta;
  WifiHotspotBwuHandler handler_sta(&mediums_sta.GetWifiHotspot(), nullptr);

  UpgradePathInfo unreachable_path_info;
  auto* unreachable_creds =
      unreachable_path_info.mutable_wifi_hotspot_credentials();
  unreachable_creds->set_ssid("Direct-UnreachablePeerAp");
  unreachable_creds->set_password("12345678");
  unreachable_creds->set_gateway("192.168.43.1");
  unreachable_creds->set_port(8888);

  ErrorOr<std::unique_ptr<EndpointChannel>> unreachable_result =
      handler_sta.CreateUpgradedEndpointChannel(
          &client_sta, std::string(kServiceID), std::string(kEndpointID),
          unreachable_path_info);

  ASSERT_TRUE(unreachable_result.has_error());
  EXPECT_EQ(unreachable_result.error().operation_result_code(),
            OperationResultCode::
                CONNECTIVITY_WIFI_HOTSPOT_LEGACY_STA_CONNECTION_FAILURE);
  EXPECT_FALSE(mediums_sta.GetWifiHotspot().IsConnectedToHotspot());
}

// Verifies that when an endpoint is cancelled after joining a remote SoftAP,
// CreateUpgradedEndpointChannel returns a cancellation error and
// RevertResponderState disconnects the STA cleanly.
TEST_F(WifiHotspotTest, ClientStaRoleRevertsCleanlyWhenCancelledMidUpgrade) {
  WifiHotspotMedium remote_ap_medium;
  ASSERT_TRUE(remote_ap_medium.StartWifiHotspot());
  WifiHotspotServerSocket remote_server_socket =
      remote_ap_medium.ListenForService();
  ASSERT_TRUE(remote_server_socket.IsValid());
  absl::Cleanup remote_cleanup = [&]() {
    remote_server_socket.Close();
    remote_ap_medium.StopWifiHotspot();
  };

  HotspotCredentials* remote_credentials = remote_ap_medium.GetCredential();
  ASSERT_NE(remote_credentials, nullptr);
  remote_server_socket.PopulateHotspotCredentials(*remote_credentials);

  UpgradePathInfo::WifiHotspotCredentials proto_credentials;
  proto_credentials.set_ssid(remote_credentials->GetSSID());
  proto_credentials.set_password(remote_credentials->GetPassword());
  proto_credentials.set_frequency(remote_credentials->GetFrequency());
  for (const auto& candidate : remote_credentials->GetAddressCandidates()) {
    ServiceAddressToProto(candidate,
                          *proto_credentials.add_address_candidates());
  }
  ExceptionOr<OfflineFrame> upgrade_frame =
      parser::FromBytes(parser::ForBwuWifiHotspotPathAvailable(
          std::move(proto_credentials),
          /*supports_disabling_encryption=*/false));
  ASSERT_TRUE(upgrade_frame.ok());

  ClientProxy client_sta;
  client_sta.AddCancellationFlag(std::string(kEndpointID));
  Mediums mediums_sta;
  WifiHotspotBwuHandler handler_sta(&mediums_sta.GetWifiHotspot(), nullptr);

  FeatureFlags::Flags flags = FeatureFlags::GetInstance().GetFlags();
  flags.enable_cancellation_flag = true;
  env_.SetFeatureFlags(flags);
  client_sta.CancelEndpoint(std::string(kEndpointID));

  ErrorOr<std::unique_ptr<EndpointChannel>> cancelled_result =
      handler_sta.CreateUpgradedEndpointChannel(
          &client_sta, std::string(kServiceID), std::string(kEndpointID),
          upgrade_frame.result()
              .v1()
              .bandwidth_upgrade_negotiation()
              .upgrade_path_info());

  ASSERT_TRUE(cancelled_result.has_error());
  EXPECT_EQ(cancelled_result.error().operation_result_code(),
            OperationResultCode::
                CLIENT_CANCELLATION_CANCEL_WIFI_HOTSPOT_OUTGOING_CONNECTION);
  EXPECT_TRUE(mediums_sta.GetWifiHotspot().IsConnectedToHotspot());

  handler_sta.RevertResponderState(std::string(kServiceID));
  EXPECT_FALSE(mediums_sta.GetWifiHotspot().IsConnectedToHotspot());
}

// Verifies that invalid gateway ports (0, >65535, <0) in Wi-Fi Hotspot
// credentials are rejected.
TEST_F(WifiHotspotTest, CreateUpgradedEndpointChannel_RejectGatewayPort0) {
  ClientProxy client;
  client.AddCancellationFlag(std::string(kEndpointID));
  Mediums mediums;
  WifiHotspotBwuHandler handler(&mediums.GetWifiHotspot(), nullptr);

  UpgradePathInfo path_info;
  auto* credentials = path_info.mutable_wifi_hotspot_credentials();
  credentials->set_ssid("SSID");
  credentials->set_password("password");
  credentials->set_gateway("192.168.43.1");

  // Port 0
  credentials->set_port(0);
  auto result = handler.CreateUpgradedEndpointChannel(
      &client, std::string(kServiceID), std::string(kEndpointID), path_info);
  EXPECT_TRUE(result.has_error());
  EXPECT_EQ(result.error().operation_result_code().value(),
            OperationResultCode::CONNECTIVITY_WIFI_HOTSPOT_INVALID_CREDENTIAL);

  // Port > 65535
  credentials->set_port(65536);
  result = handler.CreateUpgradedEndpointChannel(
      &client, std::string(kServiceID), std::string(kEndpointID), path_info);
  EXPECT_TRUE(result.has_error());
  EXPECT_EQ(result.error().operation_result_code().value(),
            OperationResultCode::CONNECTIVITY_WIFI_HOTSPOT_INVALID_CREDENTIAL);

  // Port < 0
  credentials->set_port(-1);
  result = handler.CreateUpgradedEndpointChannel(
      &client, std::string(kServiceID), std::string(kEndpointID), path_info);
  EXPECT_TRUE(result.has_error());
  EXPECT_EQ(result.error().operation_result_code().value(),
            OperationResultCode::CONNECTIVITY_WIFI_HOTSPOT_INVALID_CREDENTIAL);
}

}  // namespace connections
}  // namespace nearby
