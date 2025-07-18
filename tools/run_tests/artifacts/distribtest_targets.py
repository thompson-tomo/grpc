#!/usr/bin/env python3
# Copyright 2016 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Definition of targets run distribution package tests."""

import os.path
import sys

sys.path.insert(0, os.path.abspath(".."))
import python_utils.jobset as jobset


def create_docker_jobspec(
    name,
    dockerfile_dir,
    shell_command,
    environ={},
    flake_retries=0,
    timeout_retries=0,
    copy_rel_path=None,
    timeout_seconds=30 * 60,
):
    """Creates jobspec for a task running under docker."""
    environ = environ.copy()
    # the entire repo will be cloned if copy_rel_path is not set.
    if copy_rel_path:
        environ["RELATIVE_COPY_PATH"] = copy_rel_path

    docker_args = []
    for k, v in list(environ.items()):
        docker_args += ["-e", "%s=%s" % (k, v)]
    docker_env = {
        "DOCKERFILE_DIR": dockerfile_dir,
        "DOCKER_RUN_SCRIPT": "tools/run_tests/dockerize/docker_run.sh",
        "DOCKER_RUN_SCRIPT_COMMAND": shell_command,
    }
    jobspec = jobset.JobSpec(
        cmdline=["tools/run_tests/dockerize/build_and_run_docker.sh"]
        + docker_args,
        environ=docker_env,
        shortname="distribtest.%s" % (name),
        timeout_seconds=timeout_seconds,
        flake_retries=flake_retries,
        timeout_retries=timeout_retries,
    )
    return jobspec


def create_jobspec(
    name,
    cmdline,
    environ=None,
    shell=False,
    flake_retries=0,
    timeout_retries=0,
    use_workspace=False,
    timeout_seconds=10 * 60,
):
    """Creates jobspec."""
    environ = environ.copy()
    if use_workspace:
        environ["WORKSPACE_NAME"] = "workspace_%s" % name
        cmdline = [
            "bash",
            "tools/run_tests/artifacts/run_in_workspace.sh",
        ] + cmdline
    jobspec = jobset.JobSpec(
        cmdline=cmdline,
        environ=environ,
        shortname="distribtest.%s" % (name),
        timeout_seconds=timeout_seconds,
        flake_retries=flake_retries,
        timeout_retries=timeout_retries,
        shell=shell,
    )
    return jobspec


class CSharpDistribTest(object):
    """Tests C# NuGet package"""

    def __init__(
        self,
        platform,
        arch,
        docker_suffix=None,
        use_dotnet_cli=False,
        presubmit=False,
    ):
        self.name = "csharp_%s_%s" % (platform, arch)
        self.platform = platform
        self.arch = arch
        self.docker_suffix = docker_suffix
        self.labels = ["distribtest", "csharp", platform, arch]
        if presubmit:
            self.labels.append("presubmit")
        self.script_suffix = ""
        if docker_suffix:
            self.name += "_%s" % docker_suffix
            self.labels.append(docker_suffix)
        if use_dotnet_cli:
            self.name += "_dotnetcli"
            self.script_suffix = "_dotnetcli"
            self.labels.append("dotnetcli")
        else:
            self.labels.append("olddotnet")

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self, inner_jobs=None):
        del inner_jobs  # arg unused as there is little opportunity for parallelizing whats inside the distribtests
        if self.platform == "linux":
            return create_docker_jobspec(
                self.name,
                "tools/dockerfile/distribtest/csharp_%s_%s"
                % (self.docker_suffix, self.arch),
                "test/distrib/csharp/run_distrib_test%s.sh"
                % self.script_suffix,
                copy_rel_path="test/distrib",
            )
        elif self.platform == "macos":
            return create_jobspec(
                self.name,
                [
                    "test/distrib/csharp/run_distrib_test%s.sh"
                    % self.script_suffix
                ],
                environ={
                    "EXTERNAL_GIT_ROOT": "../../../..",
                    "SKIP_NETCOREAPP21_DISTRIBTEST": "1",
                    "SKIP_NET50_DISTRIBTEST": "1",
                },
                use_workspace=True,
            )
        elif self.platform == "windows":
            # TODO(jtattermusch): re-enable windows distribtest
            return create_jobspec(
                self.name,
                ["bash", "tools/run_tests/artifacts/run_distribtest_csharp.sh"],
                environ={},
                use_workspace=True,
            )
        else:
            raise Exception("Not supported yet.")

    def __str__(self):
        return self.name


class PythonDistribTest(object):
    """Tests Python package"""

    def __init__(
        self, platform, arch, docker_suffix, source=False, presubmit=False
    ):
        self.source = source
        if source:
            self.name = "python_dev_%s_%s_%s" % (platform, arch, docker_suffix)
        else:
            self.name = "python_%s_%s_%s" % (platform, arch, docker_suffix)
        self.platform = platform
        self.arch = arch
        self.docker_suffix = docker_suffix
        self.labels = ["distribtest", "python", platform, arch, docker_suffix]
        if presubmit:
            self.labels.append("presubmit")

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self, inner_jobs=None):
        # TODO(jtattermusch): honor inner_jobs arg for this task.
        del inner_jobs
        if not self.platform == "linux":
            raise Exception("Not supported yet.")

        if self.source:
            return create_docker_jobspec(
                self.name,
                "tools/dockerfile/distribtest/python_dev_%s_%s"
                % (self.docker_suffix, self.arch),
                "test/distrib/python/run_source_distrib_test.sh",
                copy_rel_path="test/distrib",
                timeout_seconds=45 * 60,
            )
        else:
            return create_docker_jobspec(
                self.name,
                "tools/dockerfile/distribtest/python_%s_%s"
                % (self.docker_suffix, self.arch),
                "test/distrib/python/run_binary_distrib_test.sh",
                copy_rel_path="test/distrib",
                timeout_seconds=45 * 60,
            )

    def __str__(self):
        return self.name


class RubyDistribTest(object):
    """Tests Ruby package"""

    def __init__(
        self,
        platform,
        arch,
        docker_suffix,
        ruby_version=None,
        source=False,
        presubmit=False,
        protobuf_version="",
    ):
        self.package_type = "binary"
        if source:
            self.package_type = "source"
        self.name = "ruby_%s_%s_%s_version_%s_package_type_%s" % (
            platform,
            arch,
            docker_suffix,
            ruby_version or "unspecified",
            self.package_type,
        )
        if not protobuf_version == "":
            self.name += "_protobuf_%s" % protobuf_version
        self.platform = platform
        platform_label = self.platform
        self.arch = arch
        self.docker_suffix = docker_suffix
        self.ruby_version = ruby_version
        self.protobuf_version = protobuf_version
        if platform_label.startswith("linux"):
            platform_label = "linux"
        self.labels = [
            "distribtest",
            "ruby",
            platform_label,
            arch,
            docker_suffix,
        ]
        if presubmit:
            self.labels.append("presubmit")

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self, inner_jobs=None):
        # TODO(jtattermusch): honor inner_jobs arg for this task.
        del inner_jobs
        arch_to_gem_arch = {
            "x64": "x86_64",
            "x86": "x86",
        }
        if self.platform not in ["linux-gnu", "linux-musl"]:
            raise Exception("Not supported yet.")

        dockerfile_name = "tools/dockerfile/distribtest/ruby_%s_%s" % (
            self.docker_suffix,
            self.arch,
        )
        if self.ruby_version is not None:
            dockerfile_name += "_%s" % self.ruby_version
        return create_docker_jobspec(
            self.name,
            dockerfile_name,
            "test/distrib/ruby/run_distrib_test.sh %s %s %s %s"
            % (
                arch_to_gem_arch[self.arch],
                self.platform,
                self.package_type,
                self.protobuf_version,
            ),
            copy_rel_path="test/distrib",
        )

    def __str__(self):
        return self.name


class PHP8DistribTest(object):
    """Tests PHP8 package"""

    def __init__(self, platform, arch, docker_suffix=None, presubmit=False):
        self.name = "php8_%s_%s_%s" % (platform, arch, docker_suffix)
        self.platform = platform
        self.arch = arch
        self.docker_suffix = docker_suffix
        self.labels = ["distribtest", "php", "php8", platform, arch]
        if presubmit:
            self.labels.append("presubmit")
        if docker_suffix:
            self.labels.append(docker_suffix)

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self, inner_jobs=None):
        # TODO(jtattermusch): honor inner_jobs arg for this task.
        del inner_jobs
        if self.platform == "linux":
            return create_docker_jobspec(
                self.name,
                "tools/dockerfile/distribtest/php8_%s_%s"
                % (self.docker_suffix, self.arch),
                "test/distrib/php/run_distrib_test.sh",
                copy_rel_path="test/distrib",
            )
        elif self.platform == "macos":
            return create_jobspec(
                self.name,
                ["test/distrib/php/run_distrib_test_macos.sh"],
                environ={"EXTERNAL_GIT_ROOT": "../../../.."},
                timeout_seconds=30 * 60,
                use_workspace=True,
            )
        else:
            raise Exception("Not supported yet.")

    def __str__(self):
        return self.name


class CppDistribTest(object):
    """Tests Cpp make install by building examples."""

    def __init__(
        self, platform, arch, docker_suffix=None, testcase=None, presubmit=False
    ):
        if platform == "linux":
            self.name = "cpp_%s_%s_%s_%s" % (
                platform,
                arch,
                docker_suffix,
                testcase,
            )
        else:
            self.name = "cpp_%s_%s_%s" % (platform, arch, testcase)
        self.platform = platform
        self.arch = arch
        self.docker_suffix = docker_suffix
        self.testcase = testcase
        self.labels = [
            "distribtest",
            "cpp",
            platform,
            arch,
            testcase,
        ]
        if presubmit:
            self.labels.append("presubmit")
        if docker_suffix:
            self.labels.append(docker_suffix)

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self, inner_jobs=None):
        environ = {}
        if inner_jobs is not None:
            # set number of parallel jobs for the C++ build
            environ["GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS"] = str(
                inner_jobs
            )

        if self.platform == "linux":
            return create_docker_jobspec(
                self.name,
                "tools/dockerfile/distribtest/cpp_%s_%s"
                % (self.docker_suffix, self.arch),
                "test/distrib/cpp/run_distrib_test_%s.sh" % self.testcase,
                timeout_seconds=2 * 60 * 60,
            )
        elif self.platform == "windows":
            return create_jobspec(
                self.name,
                ["test\\distrib\\cpp\\run_distrib_test_%s.bat" % self.testcase],
                environ={},
                timeout_seconds=2 * 60 * 60,
                use_workspace=True,
            )
        else:
            raise Exception("Not supported yet.")

    def __str__(self):
        return self.name


def targets():
    """Gets list of supported targets"""
    return [
        # C++
        # The "dummy" C++ distribtest so that the set of tasks to run isn't empty
        # when grpc_distribtest_standalone runs on PRs.
        CppDistribTest("linux", "x64", "debian11", "dummy", presubmit=True),
        CppDistribTest("linux", "x64", "debian11", "cmake", presubmit=False),
        CppDistribTest(
            "linux", "x64", "debian11", "cmake_as_submodule", presubmit=False
        ),
        CppDistribTest(
            "linux",
            "x64",
            "debian11",
            "cmake_as_externalproject",
            presubmit=False,
        ),
        CppDistribTest(
            "linux", "x64", "debian11", "cmake_fetchcontent", presubmit=False
        ),
        CppDistribTest(
            "linux", "x64", "debian11", "cmake_module_install", presubmit=False
        ),
        CppDistribTest(
            "linux", "x64", "debian11", "cmake_pkgconfig", presubmit=False
        ),
        CppDistribTest(
            "linux",
            "x64",
            "debian11_aarch64_cross",
            "cmake_aarch64_cross",
            presubmit=False,
        ),
        CppDistribTest("windows", "x86", testcase="cmake", presubmit=True),
        CppDistribTest(
            "windows",
            "x86",
            testcase="cmake_as_externalproject",
            presubmit=True,
        ),
        CppDistribTest(
            "windows",
            "x86",
            testcase="cmake_for_dll",
            presubmit=True,
        ),
        # C#
        CSharpDistribTest(
            "linux", "x64", "debian11", use_dotnet_cli=True, presubmit=True
        ),
        CSharpDistribTest("linux", "x64", "ubuntu2204", use_dotnet_cli=True),
        CSharpDistribTest(
            "linux", "x64", "alpine", use_dotnet_cli=True, presubmit=True
        ),
        CSharpDistribTest(
            "linux", "x64", "dotnet31", use_dotnet_cli=True, presubmit=True
        ),
        CSharpDistribTest(
            "linux", "x64", "dotnet5", use_dotnet_cli=True, presubmit=True
        ),
        CSharpDistribTest("macos", "x64", use_dotnet_cli=True, presubmit=True),
        CSharpDistribTest("windows", "x86", presubmit=True),
        CSharpDistribTest("windows", "x64", presubmit=True),
        # Python
        PythonDistribTest("linux", "x64", "bullseye", presubmit=True),
        PythonDistribTest("linux", "x86", "bullseye", presubmit=True),
        PythonDistribTest("linux", "x64", "fedora40"),
        PythonDistribTest("linux", "x64", "arch"),
        PythonDistribTest("linux", "x64", "alpine"),
        PythonDistribTest("linux", "x64", "ubuntu2404"),
        PythonDistribTest(
            "linux", "aarch64", "python39_buster", presubmit=True
        ),
        PythonDistribTest("linux", "aarch64", "alpine", presubmit=True),
        PythonDistribTest(
            "linux", "x64", "alpine3.18", source=True, presubmit=True
        ),
        PythonDistribTest(
            "linux", "x64", "bullseye", source=True, presubmit=True
        ),
        PythonDistribTest(
            "linux", "x86", "bullseye", source=True, presubmit=True
        ),
        PythonDistribTest("linux", "x64", "fedora40", source=True),
        PythonDistribTest("linux", "x64", "arch", source=True),
        PythonDistribTest("linux", "x64", "ubuntu2404", source=True),
        # Ruby
        RubyDistribTest(
            "linux-gnu",
            "x64",
            "debian11",
            ruby_version="ruby_3_2",
            source=True,
            presubmit=True,
        ),
        RubyDistribTest(
            "linux-gnu",
            "x64",
            "debian11",
            ruby_version="ruby_3_1",
            presubmit=True,
        ),
        RubyDistribTest(
            "linux-gnu",
            "x64",
            "debian11",
            ruby_version="ruby_3_2",
            presubmit=True,
        ),
        RubyDistribTest(
            "linux-gnu",
            "x64",
            "debian11",
            ruby_version="ruby_3_3",
            presubmit=True,
        ),
        RubyDistribTest(
            "linux-gnu",
            "x64",
            "debian11",
            ruby_version="ruby_3_3",
            protobuf_version="3.25",
            presubmit=True,
        ),
        RubyDistribTest(
            "linux-gnu",
            "x64",
            "debian11",
            ruby_version="ruby_3_4",
            presubmit=True,
        ),
        RubyDistribTest("linux-gnu", "x64", "ubuntu2204", presubmit=True),
        RubyDistribTest("linux-gnu", "x64", "ubuntu2404", presubmit=True),
        RubyDistribTest(
            "linux-musl",
            "x64",
            "alpine",
            ruby_version="ruby_3_1",
            presubmit=True,
        ),
        RubyDistribTest(
            "linux-musl",
            "x64",
            "alpine",
            ruby_version="ruby_3_2",
            presubmit=True,
        ),
        RubyDistribTest(
            "linux-musl",
            "x64",
            "alpine",
            ruby_version="ruby_3_3",
            presubmit=True,
        ),
        RubyDistribTest(
            "linux-musl",
            "x64",
            "alpine",
            ruby_version="ruby_3_4",
            presubmit=True,
        ),
        # PHP8
        PHP8DistribTest("linux", "x64", "debian12", presubmit=True),
        PHP8DistribTest("macos", "x64", presubmit=True),
    ]
