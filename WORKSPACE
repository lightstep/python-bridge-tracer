workspace(name = "opentracing_python_cpp_bridge")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

git_repository(
    name = "io_opentracing_cpp",
    # remote = "https://github.com/opentracing/opentracing-cpp",
    # commit = "ac50154a7713877f877981c33c3375003b6ebfe1",
    remote = "https://github.com/rnburn/opentracing-cpp",
    commit = "43898de4d6f1a0c87f544a8de4ab045377b34281",
)

http_archive(
    name = "com_github_python_cpython3",
    build_file = "//bazel:cpython.BUILD",
    strip_prefix = "cpython-3.7.3",
    urls = [
        "https://github.com/python/cpython/archive/v3.7.3.tar.gz",
    ],
)

http_archive(
    name = "com_github_python_cpython27",
    build_file = "//bazel:cpython.BUILD",
    strip_prefix = "cpython-2.7.16",
    urls = [
        "https://github.com/python/cpython/archive/v2.7.16.tar.gz",
    ],
)

new_local_repository(
    name = "vendored_pyconfig3",
    path = ".",
    build_file = "//bazel:vendored_pyconfig3.BUILD",
)

new_local_repository(
    name = "vendored_pyconfig27m",
    path = ".",
    build_file = "//bazel:vendored_pyconfig27m.BUILD",
)

new_local_repository(
    name = "vendored_pyconfig27mu",
    path = ".",
    build_file = "//bazel:vendored_pyconfig27mu.BUILD",
)

git_repository(
    name = "io_bazel_rules_python",
    remote = "https://github.com/bazelbuild/rules_python.git",
    commit = "965d4b4a63e6462204ae671d7c3f02b25da37941",
)


# Only needed for PIP support:
load("@io_bazel_rules_python//python:pip.bzl", "pip_repositories")

pip_repositories()

load("@io_bazel_rules_python//python:pip.bzl", "pip_import")

pip_import(
    name = "python_pip_deps",
    requirements = "//:requirements.txt",
)

load("@python_pip_deps//:requirements.bzl", "pip_install")
pip_install()
