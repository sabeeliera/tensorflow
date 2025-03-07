/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

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

#include <optional>
#include <utility>
#include <vector>

#include "tensorflow/compiler/xla/service/gpu/gpu_fusible.h"
#include "tensorflow/compiler/xla/service/gpu/instruction_fusion.h"
#include "tensorflow/compiler/xla/service/gpu/tests/gpu_codegen_test.h"
#include "tensorflow/compiler/xla/service/hlo_module_config.h"
#include "tensorflow/compiler/xla/service/hlo_parser.h"
#include "tensorflow/compiler/xla/tests/hlo_test_base.h"
#include "tensorflow/core/platform/test.h"

namespace xla {
namespace gpu {
namespace {

class GpuFusionTest : public GpuCodegenTest {};

TEST_F(GpuFusionTest, FusedReshape) {
  const char* hlo_text = R"(
    HloModule test_module

    fused_computation {
      p0.param_0 = f32[4,1,1]{2,1,0} parameter(0)
      p1.param_1 = f32[4,1]{1,0} parameter(1)
      reshape = f32[4,1]{1,0} reshape(p0.param_0)
      ROOT add = f32[4,1] add(reshape, p1.param_1)
    }

    ENTRY BroadcastIntoAdd {
      p0 = f32[4,1,1]{2,1,0} parameter(0)
      p1 = f32[4,1]{1,0} parameter(1)
      ROOT fusion = f32[4,1]{1,0} fusion(p0, p1), kind=kLoop,
                                                  calls=fused_computation
    }
)";

  CompileAndVerifyIr(hlo_text,
                     R"(
; CHECK-LABEL: @fusion
; CHECK: fadd
; CHECK: }
      )");
}

// Check that we limit the number of operands to fusions we create.
TEST_F(GpuFusionTest, FusedBiggerThenThresholdButDoNotChangeTheFusionl) {
  constexpr int64_t kNumParams = MaxOperandsAndOutputsPerFusion() + 1;

  // Compute
  //   p0 + p1 + p2 + ... + pn,
  // Use so many parameters that they do not fit into one fusion.
  auto module = CreateNewVerifiedModule();
  HloComputation::Builder b(TestName());
  Shape input_shape = ShapeUtil::MakeShape(F32, {10, 100});
  Shape slice_shape = ShapeUtil::MakeShape(F32, {10, 2});
  Shape concat_shape = ShapeUtil::MakeShape(F32, {10, 2 * kNumParams});
  HloInstruction* input =
      b.AddInstruction(HloInstruction::CreateParameter(0, input_shape, "p"));

  std::vector<HloInstruction*> slice_params;
  for (int64_t i = 0; i < kNumParams; ++i) {
    slice_params.push_back(b.AddInstruction(HloInstruction::CreateSlice(
        slice_shape, input, {0, 0}, {10, 2}, {1, 1})));
  }
  b.AddInstruction(
      HloInstruction::CreateConcatenate(concat_shape, slice_params, 1));
  module->AddEntryComputation(b.Build());
  EXPECT_TRUE(GpuInstructionFusion(false).Run(module.get()).value());
  EXPECT_TRUE(module->entry_computation()->root_instruction()->opcode() ==
              HloOpcode::kFusion);
  for (HloInstruction* instr : module->entry_computation()->instructions()) {
    EXPECT_TRUE(instr->opcode() != HloOpcode::kSlice);
  }
}

class TransposeFusionTest : public GpuFusionTest {
 public:
  void CheckGpuFusion(absl::string_view hlo,
                      std::optional<absl::string_view> expected) {
    RunAndFilecheckHloRewrite(hlo, GpuInstructionFusion{/*may_duplicate=*/true},
                              expected);
  }
};

TEST_F(TransposeFusionTest, Elementary) {
  const char* hlo = R"(
HloModule module

ENTRY main {
  p = f32[16,32]{1,0} parameter(0)
  s = sqrt(p)
  ROOT c = f32[16,32]{0,1} copy(s)
}
  )";

  CheckGpuFusion(hlo, R"(
// CHECK: %fused_computation (param_0.1: f32[16,32]) -> f32[16,32] {
// CHECK-NEXT:   %param_0.1 = f32[16,32]{1,0} parameter(0)
// CHECK-NEXT:   %s.1 = f32[16,32]{1,0} sqrt(%param_0.1)
// CHECK-NEXT:   ROOT %c.1 = f32[16,32]{0,1} copy(%s.1)
// CHECK-NEXT: }
// CHECK: ROOT %fusion = f32[16,32]{0,1} fusion(%p), kind=kInput, calls=%fused_computation
)");
}

TEST_F(TransposeFusionTest, ReshapeSimpleFusion) {
  const char* hlo = R"(
HloModule module

ENTRY main {
  p = f32[256,16]{1,0} parameter(0)
  r = f32[16,16,16]{2,1,0} reshape(p)
  s = sqrt(r)
  ROOT c = f32[16,16,16]{1,2,0} copy(s)
}
  )";

  CheckGpuFusion(hlo, R"(
// CHECK: %fused_computation (param_0.2: f32[256,16]) -> f32[16,16,16] {
// CHECK-NEXT:   %param_0.2 = f32[256,16]{1,0} parameter(0)
// CHECK-NEXT:   %r.1 = f32[16,16,16]{2,1,0} reshape(%param_0.2)
// CHECK-NEXT:   %s.1 = f32[16,16,16]{2,1,0} sqrt(%r.1)
// CHECK-NEXT:   ROOT %c.1 = f32[16,16,16]{1,2,0} copy(%s.1)
// CHECK-NEXT: }
// CHECK:   ROOT %fusion = f32[16,16,16]{1,2,0} fusion(%p), kind=kInput, calls=%fused_computation
)");
}

}  // namespace
}  // namespace gpu
}  // namespace xla
