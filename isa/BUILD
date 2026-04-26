load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

cc_binary(
    name = "isa",
    srcs = ["isa.cc"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/types:span",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:flat_hash_map",
    ],
)

