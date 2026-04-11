py_library(
    name = "util",
    srcs = ["util.py"],
    deps = [
        "//cpu:format_py_proto",
    ],
)

py_binary(
    name = "export_kicad",
    srcs = ["export_kicad.py"],
    deps = [
        ":util",
        "//cpu:format_py_proto",
        "@abseil-py//absl/flags",
        "@abseil-py//absl:app",
        "@pip_deps//skidl",
        "@pip_deps//graphviz",
    ],
    imports = ["."],
)

py_binary(
    name = "export_breadboard",
    srcs = ["export_breadboard.py"],
    deps = [
        ":util",
        "//cpu:format_py_proto",
        "@abseil-py//absl/flags",
        "@abseil-py//absl:app",
    ],
    imports = ["."],
)

py_binary(
    name = "placer_kicad",
    srcs = ["placer_kicad.py"],
    deps = [
        ":util",
        "//cpu:format_py_proto",
        "@abseil-py//absl/flags",
        "@abseil-py//absl:app",
        "@pip_deps//kicad_python",
    ],
    imports = ["."],
)
