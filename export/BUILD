load("@pip_deps//:requirements.bzl", "requirement")

py_binary(
    name = "export_kicad",
    srcs = ["export_kicad.py"],
    deps = [
        "//cpu:format_py_proto",
        "@abseil-py//absl/flags",
        "@abseil-py//absl:app",
        requirement("skidl"),
    ]
)
