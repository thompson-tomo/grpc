//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"
#ifdef GRPC_POSIX_SOCKET_RESOLVE_ADDRESS

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <string.h>
#include <sys/types.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/iomgr/block_annotate.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolve_address_posix.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/util/host_port.h"
#include "src/core/util/time.h"
#include "src/core/util/useful.h"
namespace grpc_core {

grpc_event_engine::experimental::EventEngine* NativeDNSResolver::engine() {
  // Optimistically, if the engine is already created, return it.
  auto engine_ptr = engine_ptr_.load(std::memory_order_relaxed);
  if (engine_ptr != nullptr) return engine_ptr;
  // Otherwise, create the engine under the lock.
  // This is only if some other thread did not already create it.
  MutexLock lock(&mu_);
  if (engine_ != nullptr) return engine_.get();
  engine_ = grpc_event_engine::experimental::GetDefaultEventEngine();
  engine_ptr_.store(engine_.get(), std::memory_order_relaxed);
  return engine_.get();
}

DNSResolver::TaskHandle NativeDNSResolver::LookupHostname(
    std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
        on_done,
    absl::string_view name, absl::string_view default_port,
    Duration /* timeout */, grpc_pollset_set* /* interested_parties */,
    absl::string_view /* name_server */) {
  engine()->Run([on_done = std::move(on_done), name = std::string(name),
                 default_port = std::string(default_port)]() {
    ExecCtx exec_ctx;
    auto result = GetDNSResolver()->LookupHostnameBlocking(name, default_port);
    on_done(std::move(result));
  });
  return kNullHandle;
}

absl::StatusOr<std::vector<grpc_resolved_address>>
NativeDNSResolver::LookupHostnameBlocking(absl::string_view name,
                                          absl::string_view default_port) {
  ExecCtx exec_ctx;
  struct addrinfo hints;
  struct addrinfo *result = nullptr, *resp;
  int s;
  size_t i;
  grpc_error_handle err;
  std::vector<grpc_resolved_address> addresses;
  std::string host;
  std::string port;
  // parse name, splitting it into host and port parts
  SplitHostPort(name, &host, &port);
  if (host.empty()) {
    err =
        GRPC_ERROR_CREATE(absl::StrCat("unparsable host:port \"", name, "\""));
    goto done;
  }
  if (port.empty()) {
    if (default_port.empty()) {
      err = GRPC_ERROR_CREATE(absl::StrCat("no port in name \"", name, "\""));
      goto done;
    }
    port = std::string(default_port);
  }
  // Call getaddrinfo
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;      // ipv4 or ipv6
  hints.ai_socktype = SOCK_STREAM;  // stream socket
  hints.ai_flags = AI_PASSIVE;      // for wildcard IP address
  GRPC_SCHEDULING_START_BLOCKING_REGION;
  s = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
  GRPC_SCHEDULING_END_BLOCKING_REGION;
  if (s != 0) {
    // Retry if well-known service name is recognized
    const char* svc[][2] = {{"http", "80"}, {"https", "443"}};
    for (i = 0; i < GPR_ARRAY_SIZE(svc); i++) {
      if (port == svc[i][0]) {
        GRPC_SCHEDULING_START_BLOCKING_REGION;
        s = getaddrinfo(host.c_str(), svc[i][1], &hints, &result);
        GRPC_SCHEDULING_END_BLOCKING_REGION;
        break;
      }
    }
  }
  if (s != 0) {
    err = absl::UnknownError(absl::StrCat(
        "getaddrinfo(\"", name, "\"): ", gai_strerror(s), " (", s, ")"));
    goto done;
  }
  // Success path: fill in addrs
  for (resp = result; resp != nullptr; resp = resp->ai_next) {
    grpc_resolved_address addr;
    memcpy(&addr.addr, resp->ai_addr, resp->ai_addrlen);
    addr.len = resp->ai_addrlen;
    addresses.push_back(addr);
  }
  err = absl::OkStatus();
done:
  if (result) {
    freeaddrinfo(result);
  }
  if (err.ok()) {
    return addresses;
  }
  auto error_result = grpc_error_to_absl_status(err);
  return error_result;
}

DNSResolver::TaskHandle NativeDNSResolver::LookupSRV(
    std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
        on_resolved,
    absl::string_view /* name */, Duration /* timeout */,
    grpc_pollset_set* /* interested_parties */,
    absl::string_view /* name_server */) {
  engine()->Run([on_resolved] {
    ExecCtx exec_ctx;
    on_resolved(absl::UnimplementedError(
        "The Native resolver does not support looking up SRV records"));
  });
  return kNullHandle;
};

DNSResolver::TaskHandle NativeDNSResolver::LookupTXT(
    std::function<void(absl::StatusOr<std::string>)> on_resolved,
    absl::string_view /* name */, Duration /* timeout */,
    grpc_pollset_set* /* interested_parties */,
    absl::string_view /* name_server */) {
  // Not supported
  engine()->Run([on_resolved] {
    ExecCtx exec_ctx;
    on_resolved(absl::UnimplementedError(
        "The Native resolver does not support looking up TXT records"));
  });
  return kNullHandle;
};

bool NativeDNSResolver::Cancel(TaskHandle /*handle*/) { return false; }

}  // namespace grpc_core

#endif
