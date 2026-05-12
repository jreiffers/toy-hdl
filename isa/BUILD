load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "isa_types",
    hdrs = ["isa_types.h"],
)

cc_binary(
    name = "isa",
    srcs = ["isa.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":isa_types",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

genrule(
    name = "visitor_hdr_gen",
    outs = ["visitor.h"],
    cmd = "$(location :isa) --mode=visitor_hdr > $@",
    tools = [":isa"],
)

cc_library(
    name = "visitor",
    hdrs = [":visitor_hdr_gen"],
    deps = [
        ":isa_types",
        "@abseil-cpp//absl/types:span",
    ],
)

genrule(
    name = "asm_dispatch_gen",
    outs = ["asm_dispatch.cc"],
    cmd = "$(location :isa) --mode=asm_dispatch > $@",
    tools = [":isa"],
)

cc_library(
    name = "asm_dispatch",
    srcs = [":asm_dispatch_gen"],
    hdrs = ["asm_dispatch.h"],
    deps = [
        ":isa_types",
        ":visitor",
        "@abseil-cpp//absl/types:span",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
    ],
)

genrule(
    name = "encdec_gen",
    outs = ["encdec.cc"],
    cmd = "$(location :isa) --mode=encdec > $@",
    tools = [":isa"],
)

cc_library(
    name = "encdec",
    srcs = [":encdec_gen"],
    hdrs = ["encdec.h"],
    deps = [
        ":isa_types",
        ":visitor",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/types:span",
    ],
)

genrule(
    name = "instructions_gen",
    outs = ["instructions.cc"],
    cmd = "$(location :isa) --mode=instrs > $@",
    tools = [":isa"],
)

cc_library(
    name = "instructions",
    srcs = [":instructions_gen"],
    hdrs = ["instructions.h"],
    deps = [
        ":isa_types",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "emulator",
    srcs = ["emulator.cc"],
    hdrs = ["emulator.h"],
    deps = [
        ":visitor",
        ":encdec",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "emulator_test",
    srcs = ["emulator_test.cc"],
    deps = [
        ":assembler",
        ":emulator",
        ":encdec",
        "//jank",
        "@googletest//:gtest_main",
        "@abseil-cpp//absl/status:status_matchers",
    ],
)

cc_library(
    name = "assembler",
    srcs = ["assembler.cc"],
    hdrs = ["assembler.h"],
    deps = [
        ":asm_dispatch",
        ":isa_types",
        ":instructions",
        "//jank",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "encdec_test",
    srcs = ["encdec_test.cc"],
    deps = [
        ":encdec",
        "@googletest//:gtest_main",
        "@abseil-cpp//absl/status:status_matchers",
    ],
)
