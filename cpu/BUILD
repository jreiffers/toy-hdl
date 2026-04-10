load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "transistor_lib",
    srcs = ["transistor_lib.cc"],
    hdrs = ["transistor_lib.h"],
    deps = ["@abseil-cpp//absl/types:span"],
)

cc_library(
    name = "eval",
    srcs = ["eval.cc"],
    hdrs = ["eval.h"],
    deps = [
      ":gate_lib",
      ":transistor_lib",
      "@abseil-cpp//absl/algorithm:container",
      "@abseil-cpp//absl/status:status",
    ],
)

cc_library(
    name = "gate_lib",
    srcs = ["gate_lib.cc"],
    hdrs = ["gate_lib.h"],
    deps = [
      "@abseil-cpp//absl/container:inlined_vector",
      "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "gate_lib_test",
    srcs = ["gate_lib_test.cc"],
    deps = [
        ":gate_lib",
        "@googletest//:gtest_main",
        "@abseil-cpp//absl/status:status_matchers",
    ],

)

cc_library(
    name = "cpu_modules",
    hdrs = ["alu.h", "pc_gen.h", "register_file.h"],
    deps = [":gate_lib"],
)

cc_library(
    name = "transistor_opt",
    srcs = ["transistor_opt.cc"],
    hdrs = ["transistor_opt.h"],
    deps = [":transistor_lib"],
)

cc_library(
    name = "lower_gates",
    srcs = ["lower_gates.cc"],
    hdrs = ["lower_gates.h"],
    deps = [":transistor_lib", ":gate_lib"],
)

cc_library(
    name = "gate_opt",
    srcs = ["gate_opt.cc"],
    hdrs = ["gate_opt.h"],
    deps = [
        ":gate_lib",
        ":print",
        "@abseil-cpp//absl/algorithm:container",
    ],
)

cc_library(
    name = "compiler",
    srcs = ["compiler.cc"],
    hdrs = ["compiler.h"],
    deps = [
        ":gate_lib",
        ":transistor_lib",
        ":gate_opt",
        ":lower_gates",
        ":transistor_opt",
        "@abseil-cpp//absl/types:span",
    ]
)

cc_binary(
    name = "main",
    srcs = ["main.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":compiler",
        ":cpu_modules",
        ":eval",
        ":gate_lib",
        ":print",
        ":transistor_lib",
    ],
)

cc_library(
    name = "print",
    srcs = ["print.cc"],
    hdrs = ["print.h"],
    deps = [
        ":transistor_lib",
        ":gate_lib",
    ]
)

cc_test(
    name = "cpu_modules_test",
    srcs = ["alu_test.cc", "pcgen_test.cc", "register_file_test.cc"],
    deps = [
        ":cpu_modules",
        ":eval",
        ":gate_opt",
        ":lower_gates",
        ":compiler",
        "@googletest//:gtest_main",
        "@abseil-cpp//absl/status:status_matchers",
    ],
)

cc_test(
    name = "compiler_test",
    srcs = ["compiler_test.cc"],
    deps = [
        ":cpu_modules",
        ":eval",
        ":gate_opt",
        ":lower_gates",
        ":compiler",
        "@googletest//:gtest_main",
        "@abseil-cpp//absl/status:status_matchers",
    ],
)

cc_test(
    name = "eval_test",
    srcs = ["eval_test.cc"],
    deps = [
        ":eval",
        ":print",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "transistor_lib_test",
    srcs = ["transistor_lib_test.cc"],
    deps = [
        ":transistor_lib",
        ":eval",
        "@googletest//:gtest_main",
    ],
)
