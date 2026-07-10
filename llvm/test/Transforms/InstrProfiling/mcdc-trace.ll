; RUN: opt < %s -passes=instrprof -S | FileCheck %s

declare void @llvm.instrprof.mcdc.trace.begin(ptr, i64, i32, i32)
declare void @llvm.instrprof.mcdc.trace.complete(ptr, i64, i32, i1)

define void @f(i1 %value) {
entry:
  call void @llvm.instrprof.mcdc.trace.begin(ptr null, i64 85, i32 7, i32 1)
  call void @llvm.instrprof.mcdc.trace.complete(ptr null, i64 85, i32 7, i1 %value)
  ret void
}

; CHECK: call void @__mcdc_trace_begin
; CHECK: call void @__mcdc_trace_complete
; CHECK-NOT: @llvm.instrprof.mcdc.trace
