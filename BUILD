load(
    "//bazel:python_bridge_build_system.bzl",
    "python_bridge_cc_binary",
    "python_bridge_cc_library",
    "python_bridge_package",
)

python_bridge_package()


python_bridge_cc_library(
		name = "module_interface",
		hdrs = glob(["include/python_bridge_tracer/*.h"]),
		strip_include_prefix = "include",
		external_deps = [
		  	"@io_opentracing_cpp//:opentracing",
		]
)

python_bridge_cc_library(
		name = "bridge_tracer_lib",
		srcs = glob(["src/lib/*.h", "src/lib/*.cpp"]),
		deps = [
		  	":module_interface",
		],
		external_deps = [
		  	"@io_opentracing_cpp//:opentracing",
		]
)

python_bridge_cc_library(
    name = "bridge_tracer_module_lib",
    srcs = glob([
      "src/module/*.h",
      "src/module/*.cpp",
    ]),
    deps = [
        ":module_interface",
        ":bridge_tracer_lib",
    ],
    external_deps = [
        "@io_opentracing_cpp//:opentracing",
    ],
)

cc_binary(
    name = "bridge_tracer.so",
    linkshared = True,
    deps = [
      "//:bridge_tracer_module_lib_py3",
    ],
    visibility = [
        "//visibility:public",
    ],
)

cc_binary(
    name = "bridge_tracer27m.so",
    linkshared = True,
    deps = [
      "//:bridge_tracer_module_lib_py27m",
    ],
    visibility = [
        "//visibility:public",
    ],
)
