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

#include "src/core/ext/transport/chttp2/transport/frame_goaway.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <string.h>

#include "absl/base/attributes.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"

void grpc_chttp2_goaway_parser_init(grpc_chttp2_goaway_parser* p) {
  p->debug_data = nullptr;
}

void grpc_chttp2_goaway_parser_destroy(grpc_chttp2_goaway_parser* p) {
  gpr_free(p->debug_data);
}

grpc_error_handle grpc_chttp2_goaway_parser_begin_frame(
    grpc_chttp2_goaway_parser* p, uint32_t length, uint8_t /*flags*/) {
  if (length < 8) {
    return GRPC_ERROR_CREATE(
        absl::StrFormat("goaway frame too short (%d bytes)", length));
  }

  gpr_free(p->debug_data);
  p->debug_length = length - 8;
  p->debug_data = static_cast<char*>(gpr_malloc(p->debug_length));
  p->debug_pos = 0;
  p->state = GRPC_CHTTP2_GOAWAY_LSI0;
  return absl::OkStatus();
}

grpc_error_handle grpc_chttp2_goaway_parser_parse(void* parser,
                                                  grpc_chttp2_transport* t,
                                                  grpc_chttp2_stream* /*s*/,
                                                  const grpc_slice& slice,
                                                  int is_last) {
  const uint8_t* const beg = GRPC_SLICE_START_PTR(slice);
  const uint8_t* const end = GRPC_SLICE_END_PTR(slice);
  const uint8_t* cur = beg;
  grpc_chttp2_goaway_parser* p =
      static_cast<grpc_chttp2_goaway_parser*>(parser);

  switch (p->state) {
    case GRPC_CHTTP2_GOAWAY_LSI0:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_LSI0;
        return absl::OkStatus();
      }
      p->last_stream_id = (static_cast<uint32_t>(*cur)) << 24;
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_LSI1:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_LSI1;
        return absl::OkStatus();
      }
      p->last_stream_id |= (static_cast<uint32_t>(*cur)) << 16;
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_LSI2:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_LSI2;
        return absl::OkStatus();
      }
      p->last_stream_id |= (static_cast<uint32_t>(*cur)) << 8;
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_LSI3:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_LSI3;
        return absl::OkStatus();
      }
      p->last_stream_id |= (static_cast<uint32_t>(*cur));
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_ERR0:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_ERR0;
        return absl::OkStatus();
      }
      p->error_code = (static_cast<uint32_t>(*cur)) << 24;
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_ERR1:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_ERR1;
        return absl::OkStatus();
      }
      p->error_code |= (static_cast<uint32_t>(*cur)) << 16;
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_ERR2:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_ERR2;
        return absl::OkStatus();
      }
      p->error_code |= (static_cast<uint32_t>(*cur)) << 8;
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_ERR3:
      if (cur == end) {
        p->state = GRPC_CHTTP2_GOAWAY_ERR3;
        return absl::OkStatus();
      }
      p->error_code |= (static_cast<uint32_t>(*cur));
      ++cur;
      [[fallthrough]];
    case GRPC_CHTTP2_GOAWAY_DEBUG:
      if (end != cur) {
        memcpy(p->debug_data + p->debug_pos, cur,
               static_cast<size_t>(end - cur));
      }
      CHECK((size_t)(end - cur) < UINT32_MAX - p->debug_pos);
      p->debug_pos += static_cast<uint32_t>(end - cur);
      p->state = GRPC_CHTTP2_GOAWAY_DEBUG;
      if (is_last) {
        t->http2_ztrace_collector.Append([p]() {
          return grpc_core::H2GoAwayTrace<true>{
              p->last_stream_id, p->error_code,
              std::string(absl::string_view(p->debug_data, p->debug_length))};
        });
        grpc_chttp2_add_incoming_goaway(
            t, p->error_code, p->last_stream_id,
            absl::string_view(p->debug_data, p->debug_length));
        gpr_free(p->debug_data);
        p->debug_data = nullptr;
      }
      return absl::OkStatus();
  }
  GPR_UNREACHABLE_CODE(return GRPC_ERROR_CREATE("Should never reach here"));
}

void grpc_chttp2_goaway_append(
    uint32_t last_stream_id, uint32_t error_code, const grpc_slice& debug_data,
    grpc_slice_buffer* slice_buffer,
    grpc_core::Http2ZTraceCollector* ztrace_collector) {
  grpc_slice header = GRPC_SLICE_MALLOC(9 + 4 + 4);
  uint8_t* p = GRPC_SLICE_START_PTR(header);
  uint32_t frame_length;
  CHECK(GRPC_SLICE_LENGTH(debug_data) < UINT32_MAX - 4 - 4);
  frame_length = 4 + 4 + static_cast<uint32_t> GRPC_SLICE_LENGTH(debug_data);

  ztrace_collector->Append([last_stream_id, error_code, debug_data]() {
    return grpc_core::H2GoAwayTrace<false>{
        last_stream_id, error_code,
        std::string(grpc_core::StringViewFromSlice(debug_data))};
  });

  // frame header: length
  *p++ = static_cast<uint8_t>(frame_length >> 16);
  *p++ = static_cast<uint8_t>(frame_length >> 8);
  *p++ = static_cast<uint8_t>(frame_length);
  // frame header: type
  *p++ = GRPC_CHTTP2_FRAME_GOAWAY;
  // frame header: flags
  *p++ = 0;
  // frame header: stream id
  *p++ = 0;
  *p++ = 0;
  *p++ = 0;
  *p++ = 0;
  // payload: last stream id
  *p++ = static_cast<uint8_t>(last_stream_id >> 24);
  *p++ = static_cast<uint8_t>(last_stream_id >> 16);
  *p++ = static_cast<uint8_t>(last_stream_id >> 8);
  *p++ = static_cast<uint8_t>(last_stream_id);
  // payload: error code
  *p++ = static_cast<uint8_t>(error_code >> 24);
  *p++ = static_cast<uint8_t>(error_code >> 16);
  *p++ = static_cast<uint8_t>(error_code >> 8);
  *p++ = static_cast<uint8_t>(error_code);
  CHECK(p == GRPC_SLICE_END_PTR(header));
  grpc_slice_buffer_add(slice_buffer, header);
  grpc_slice_buffer_add(slice_buffer, debug_data);
}
