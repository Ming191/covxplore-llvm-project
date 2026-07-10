// RUN: %clang_cc1 -triple %itanium_abi_triple -std=c++11 -emit-llvm -o - %s -fprofile-instrument=clang -fcoverage-mapping -fcoverage-mcdc | FileCheck %s

extern bool left();
extern bool right();

bool f() {
  if (left() && right())
    return true;
  return false;
}

// CHECK: call void @llvm.instrprof.mcdc.trace.begin
// CHECK: call void @llvm.instrprof.mcdc.condbitmap.update
// CHECK: call void @llvm.instrprof.mcdc.trace.complete
