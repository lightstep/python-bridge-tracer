def python_bridge_package():
    native.package(default_visibility = ["//visibility:public"])

# Taken from https://github.com/bazelbuild/bazel/issues/2670#issuecomment-369674735
# with modifications.
def python_bridge_private_include_copts(includes, is_system=False):
    copts = []
    prefix = ''

    # convert "@" to "external/" unless in the main workspace
    repo_name = native.repository_name()
    if repo_name != '@':
        prefix = 'external/{}/'.format(repo_name[1:])

    inc_flag = "-isystem " if is_system else "-I"

    for inc in includes:
        copts.append("{}{}{}".format(inc_flag, prefix, inc))
        copts.append("{}$(GENDIR){}/{}".format(inc_flag, prefix, inc))

    return copts

def python_bridge_include_copts():
  return python_bridge_private_include_copts([
      "include",
      "src",
      "test",
  ]) + python_bridge_private_include_copts([
    "3rd_party/randutils/include", 
    "3rd_party/base64/include", 
  ], is_system=True)

def python_bridge_copts(is_3rd_party=False):
  if is_3rd_party:
    return [
      "-std=c++11",
    ]
  return [
      "-DPy_LIMITED_API",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-Wnon-virtual-dtor",
      "-Woverloaded-virtual",
      "-Wold-style-cast",
      "-Wno-overloaded-virtual",
      "-Wno-missing-field-initializers",
      "-Wno-noexcept-type",
      "-Wvla",
      "-std=c++11",
  ]


def python_bridge_include_prefix(path):
    if path.startswith('src/') or path.startswith('include/'):
        return '/'.join(path.split('/')[1:])
    return None

def python_bridge_cc_library(name,
                     srcs = [],
                     hdrs = [],
                     private_hdrs = [],
                     copts = [],
                     linkopts = [],
                     includes = [],
                     visibility = None,
                     external_deps = [],
                     deps = [],
                     data = [],
                     is_3rd_party = False,
                     strip_include_prefix = None):
  for version, tag in [("3", ""), ("27", "m"), ("27", "mu")]:
    suffix = version + tag
    external_deps_prime = external_deps + [
      "@com_github_python_cpython%s//:cpython_header_lib" % version,
      "@vendored_pyconfig%s//:pyconfig_lib" % suffix,
    ]
    name_prime = name + "_py" + suffix
    deps_prime = [dep + "_py" + suffix for dep in deps]
    native.cc_library(
        name = name_prime,
        srcs = srcs + private_hdrs,
        hdrs = hdrs,
        copts = python_bridge_include_copts() + python_bridge_copts(is_3rd_party) + copts,
        linkopts = linkopts,
        includes = includes,
        deps = external_deps_prime + deps_prime,
        data = data,
        linkstatic = 1,
        include_prefix = python_bridge_include_prefix(native.package_name()),
        visibility = visibility,
        strip_include_prefix = strip_include_prefix,
    )

def python_bridge_cc_binary(
        name,
        args = [],
        srcs = [],
        data = [],
        linkshared = False,
        testonly = 0,
        visibility = None,
        external_deps = [],
        deps = [],
        linkopts = []):
    native.cc_binary(
        name = name,
        args = args,
        srcs = srcs,
        data = data,
        copts = python_bridge_include_copts() + python_bridge_copts(),
        linkopts = linkopts,
        linkshared = linkshared,
        testonly = testonly,
        linkstatic = 1,
        visibility = visibility,
        stamp = 1,
        deps = external_deps + deps,
    )

def python_bridge_cc_test(
        name,
        args = [],
        srcs = [],
        data = [],
        testonly = 0,
        visibility = None,
        external_deps = [],
        deps = [],
        linkopts = []):
    native.cc_test(
        name = name,
        args = args,
        srcs = srcs,
        data = data,
        copts = python_bridge_include_copts() + python_bridge_copts(),
        linkopts = linkopts,
        testonly = testonly,
        linkstatic = 1,
        visibility = visibility,
        stamp = 1,
        deps = external_deps + deps,
    )

def python_bridge_test(
        name,
        args = [],
        srcs = [],
        main = None,
        data = [],
        testonly = 0,
        visibility = None,
        external_deps = [],
        python_version = "PY3",
        deps = []):
  native.py_test(
        name = name,
        args = args,
        srcs = srcs,
        main = main,
        python_version = python_version,
        data = data,
        testonly = testonly,
        visibility = visibility,
        stamp = 1,
        deps = external_deps + deps,
    )

