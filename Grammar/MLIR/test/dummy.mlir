// RUN: cyclebite-opt %s | cyclebite-opt | FileCheck %s

module {
    // CHECK-LABEL: func @bar()
    func.func @bar() {
        %0 = arith.constant 1 : i32
        // CHECK: %{{.*}} = cyclebite.foo %{{.*}} : i32
        %res = cyclebite.foo %0 : i32
        return
    }
}