load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "jank",
    srcs = ["context.cc", "lexer.cc", "parser.cc", "print.cc"],
    hdrs = ["ast.h", "context.h", "lexer.h", "parser.h", "print.h"],
    deps = [
      "@abseil-cpp//absl/container:inlined_vector",
      "@abseil-cpp//absl/container:flat_hash_map",
      "@abseil-cpp//absl/container:flat_hash_set",
      "@abseil-cpp//absl/strings",
      "@abseil-cpp//absl/strings:str_format",
    ],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "lexer_test",
    srcs = ["lexer_test.cc"],
    deps = [
        ":jank",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "parser_test",
    srcs = ["parser_test.cc"],
    deps = [
        ":jank",
        "@googletest//:gtest_main",
    ],

)
