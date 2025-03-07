/* Copyright 2022 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/compiler/xla/service/gpu/move_copy_to_users.h"

#include <optional>
#include <utility>

#include "tensorflow/compiler/xla/tests/hlo_test_base.h"
#include "tensorflow/core/platform/test.h"

namespace xla {
namespace {

class MoveCopyToUsersTest : public HloTestBase {
 public:
  void CheckMoveCopyToUsers(absl::string_view hlo,
                            std::optional<absl::string_view> expected) {
    RunAndFilecheckHloRewrite(hlo, MoveCopyToUsers{}, expected);
  }
};

TEST_F(MoveCopyToUsersTest, Pad) {
  const char* hlo = R"(
HloModule module

ENTRY main {
  input = s8[1,17,9,9]{3,2,1,0} parameter(0)
  copy = s8[1,17,9,9]{1,3,2,0} copy(input)
  constant = s8[] constant(0)
  ROOT pad = s8[1,32,9,9]{1,3,2,0} pad(copy, constant), padding=0_0x0_15x0_0x0_0
}
)";

  CheckMoveCopyToUsers(hlo, R"(
// CHECK: [[constant_0:%[^ ]+]] = s8[] constant(0)
// CHECK: [[pad_1_1:%[^ ]+]] = s8[1,32,9,9]{3,2,1,0} pad([[input_2:%[^ ]+]], [[constant_0]]), padding=0_0x0_15x0_0x0_0
// CHECK: ROOT [[copy_1_3:%[^ ]+]] = s8[1,32,9,9]{1,3,2,0} copy([[pad_1_1]])
)");
}

TEST_F(MoveCopyToUsersTest, Unary) {
  const char* hlo = R"(
HloModule module

ENTRY main {
  input = f32[1,17,9,9]{3,2,1,0} parameter(0)
  copy = f32[1,17,9,9]{1,3,2,0} copy(input)
  ROOT pad = f32[1,17,9,9]{1,3,2,0} sqrt(copy)
}
)";

  CheckMoveCopyToUsers(hlo, R"(
// CHECK: [[input_0:%[^ ]+]] = f32[1,17,9,9]{3,2,1,0} parameter(0)
// CHECK: [[sqrt_1:%[^ ]+]] = f32[1,17,9,9]{3,2,1,0} sqrt([[input_0]])
// CHECK: ROOT [[copy_1_2:%[^ ]+]] = f32[1,17,9,9]{1,3,2,0} copy([[sqrt_1]])
)");
}

TEST_F(MoveCopyToUsersTest, Reverse) {
  const char* hlo = R"(
HloModule module

ENTRY main {
  input = f32[1,17,9,9]{3,2,1,0} parameter(0)
  copy = f32[1,17,9,9]{1,3,2,0} copy(input)
  ROOT pad = f32[1,17,9,9]{1,3,2,0} reverse(copy), dimensions={1,2}
}
)";

  CheckMoveCopyToUsers(hlo, R"(
// CHECK: [[input_0:%[^ ]+]] = f32[1,17,9,9]{3,2,1,0} parameter(0)
// CHECK: [[sqrt_1:%[^ ]+]] = f32[1,17,9,9]{3,2,1,0} reverse([[input_0]]), dimensions={1,2}
// CHECK: ROOT [[copy_1_2:%[^ ]+]] = f32[1,17,9,9]{1,3,2,0} copy([[sqrt_1]])
)");
}

TEST_F(MoveCopyToUsersTest, Convert) {
  const char* hlo = R"(
HloModule module

ENTRY main {
  input = f32[1,17,9,9]{3,2,1,0} parameter(0)
  copy = f32[1,17,9,9]{1,3,2,0} copy(input)
  ROOT converted = f16[1,17,9,9]{1,3,2,0} convert(copy)
}
)";

  CheckMoveCopyToUsers(hlo, R"(
// CHECK: [[input_0:%[^ ]+]] = f32[1,17,9,9]{3,2,1,0} parameter(0)
// CHECK: [[sqrt_1:%[^ ]+]] = f16[1,17,9,9]{3,2,1,0} convert([[input_0]])
// CHECK: ROOT [[copy_1_2:%[^ ]+]] = f16[1,17,9,9]{1,3,2,0} copy([[sqrt_1]])
)");
}

TEST_F(MoveCopyToUsersTest, Binary) {
  const char* hlo = R"(
HloModule module

ENTRY main {
  input = f32[1,17,9,9]{3,2,1,0} parameter(0)
  input2 = f32[1,17,9,9]{3,2,1,0} parameter(1)
  copy = f32[1,17,9,9]{1,3,2,0} copy(input)
  copy2 = f32[1,17,9,9]{1,3,2,0} copy(input2)
  ROOT add = f32[1,17,9,9]{1,3,2,0} add(copy, copy2)
}
)";

  CheckMoveCopyToUsers(hlo, R"(
// CHECK: [[input_0:%[^ ]+]] = f32[1,17,9,9]{3,2,1,0} parameter(0)
// CHECK: [[input2_1:%[^ ]+]] = f32[1,17,9,9]{3,2,1,0} parameter(1)
// CHECK: [[add_1_2:%[^ ]+]] = f32[1,17,9,9]{3,2,1,0} add([[input_0]], [[input2_1]])
// CHECK: ROOT [[copy_1_3:%[^ ]+]] = f32[1,17,9,9]{1,3,2,0} copy([[add_1_2]])
)");
}

TEST_F(MoveCopyToUsersTest, BinaryDifferentLayoutNoChange) {
  const char* hlo = R"(
HloModule module

ENTRY main {
  input = f32[1,17,9,9]{3,2,0,1} parameter(0)
  input2 = f32[1,17,9,9]{3,2,1,0} parameter(1)
  copy = f32[1,17,9,9]{1,3,2,0} copy(input)
  copy2 = f32[1,17,9,9]{1,3,2,0} copy(input2)
  ROOT add = f32[1,17,9,9]{1,3,2,0} add(copy, copy2)
}
)";

  CheckMoveCopyToUsers(hlo, std::nullopt);
}

}  // namespace
}  // namespace xla
