## QNX Unit Tests

Standalone Bazel module for running C++ and Rust unit tests inside QEMU micro-virtual machines on QNX 8 (x86_64 and aarch64).

### Directory Structure

```
qnx_unit_tests/
├── .bazelrc                # Bazel config (host + qnx-x86_64/aarch64 cross-compilation)
├── .bazelversion           # Pinned Bazel version (8.6.0)
├── MODULE.bazel            # Bzlmod dependencies (QCC, Ferrocene, IFS, googletest)
├── BUILD                   # Copyright checker and formatting targets
├── defs.bzl                # Public API (cc_test_qnx, rust_test_qnx)
├── src/
│   ├── test_qnx.bzl            # Core macro wrapping cc_test/rust_test for QNX microvm execution
│   ├── runfiles_manifest.bzl   # Bazel rule to list runfiles (for --run_under mode)
│   ├── x86_64_qnx8/            # x86_64 QNX 8 specific files
│   │   ├── init.build.template
│   │   ├── run_qemu.sh
│   │   ├── run_qemu_shell.sh
│   │   ├── run_under_qnx.sh
│   │   ├── startup.sh
│   │   └── tools.build
│   ├── arm64_qnx8/             # aarch64 QNX 8 specific files
│   │   ├── init.build.template
│   │   ├── run_qemu.sh
│   │   ├── run_qemu_shell.sh
│   │   ├── run_under_qnx.sh
│   │   ├── startup.sh
│   │   └── tools.build
│   └── common/                 # Shared scripts and drivers
│       ├── prepare_test.sh
│       ├── run_test.sh
│       └── virtio9p/           # 9P2000.L resource manager for host-guest file sharing
├── third_party/
│   └── BUILD               # Stubs for QNX system libraries (libslog2, libpci)
├── tools/
│   └── qnx_credential_helper.py  # Bazel credential helper for qnx.com
├── examples/               # Standalone Bazel module demonstrating external usage
│   └── MODULE.bazel
└── test/                   # Example tests
    ├── BUILD
    ├── data.txt
    ├── main.cpp             # C++ (GTest) example
    └── main_rust.rs         # Rust example
```

### Prerequisites

- Bazel 8.6.0 (via Bazelisk)
- QEMU (`qemu-system-x86_64` and/or `qemu-system-aarch64`)
- KVM access (optional, but strongly recommended for performance)
- QNX SDP 8.0 credentials (for toolchain download)

### How It Works

The `test_qnx` macro (and its aliases `cc_test_qnx` / `rust_test_qnx`) wraps a standard `cc_test` or `rust_test` target for execution inside a QEMU microvm running QNX:

1. The test binary and its runfiles are packaged into a tar archive
2. An IFS boot image is built containing the QNX kernel, startup scripts, and the virtio-9p driver
3. QEMU boots the IFS image, mounts the test archive via virtio-9p, and executes the test
4. Test results (XML, coverage) are extracted from the shared directory after execution

### Usage

Add a test target and wrap it with the corresponding QNX macro (see `test/BUILD`):

**C++ (GTest)**

```python
load("@rules_cc//cc:defs.bzl", "cc_test")
load("@score_qnx_unit_tests//:defs.bzl", "cc_test_qnx")

cc_test(
    name = "main_cpp",
    srcs = ["main.cpp"],
    deps = [
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test_qnx(
    name = "main_cpp_qnx",
    cc_test = ":main_cpp",
)
```

**Rust**

```python
load("@rules_rust//rust:defs.bzl", "rust_test")
load("@score_qnx_unit_tests//:defs.bzl", "rust_test_qnx")

rust_test(
    name = "main_rust",
    srcs = ["main_rust.rs"],
)

rust_test_qnx(
    name = "main_rust_qnx",
    rust_test = ":main_rust",
)
```

Run the test:

```shell
# x86_64
bazel test --config=qnx-x86_64 //test:main_cpp_qnx

# aarch64
bazel test --config=qnx-aarch64 //test:main_cpp_qnx
```

Stream test output (useful for debugging):

```shell
bazel test --config=qnx-x86_64 //test:main_cpp_qnx --test_output=streamed
```

### Run-Under Mode

As an alternative to the `cc_test_qnx` / `rust_test_qnx` wrappers, you can run
existing `cc_test` or `rust_test` targets directly on QNX using the `--run_under`
configs. This packages and boots the test on-the-fly without requiring a wrapper
target:

```shell
# x86_64
bazel test --config=run-under-qnx-x86_64 //test:main_cpp

# aarch64
bazel test --config=run-under-qnx-aarch64 //test:main_cpp
```

This mode runs all C++ and Rust test targets (no tag filter).

### Shell Mode

Launch an interactive QNX shell inside the microvm:

```shell
# x86_64
bazel run --config=qnx-x86_64 //test:main_cpp_qnx_shell

# aarch64
bazel run --config=qnx-aarch64 //test:main_cpp_qnx_shell
```

The test binary is available at `/opt/tests/cc_test_qnx`. Before running it,
execute the preparation script to set up the environment (runfiles, libraries,
gtest filters):

```shell
. /proc/boot/prepare_test.sh
/persistent/unit_tests/cc_test_qnx
```

### Debugging

The shell mode supports remote debugging via GDB. Specify a debug port:

```shell
bazel run -c dbg --config=qnx-x86_64 //test:main_cpp_qnx_shell -- --debug-port 38080
```

Inside the QNX shell, run the prepare script before starting the debugger to set up
runfiles, libraries, and environment variables:

```shell
. /proc/boot/prepare_test.sh
```

Then connect from the host. Note that the CWD must be set to `/persistent/unit_tests`
since the prepare script copies the binary and its runfiles there:

```shell
ntox86_64-gdb \
    -ex "target qnx 127.0.0.1:38080" \
    -ex "set nto-cwd /persistent/unit_tests" \
    -ex "set nto-executable /persistent/unit_tests/cc_test_qnx" \
    <path-to-debug-binary-in-bazel-bin>
```

### Excluding Tests

The `cc_test_qnx` and `test_qnx` macros support filtering out specific test cases (gtest only):

```python
cc_test_qnx(
    name = "main_cpp_qnx",
    cc_test = ":main_cpp",
    excluded_tests_filter = [
        "FooTest.Test1",     # Skip a single test
        "BarTest.*",         # Skip an entire suite
    ],
)
```

### Environment Variables

| Variable | Default | Description |
|---|---|---|
| `DISABLE_KVM` | `0` | Set to `1` to disable KVM acceleration |
| `QEMU_CPU` | `host` | QEMU CPU model (e.g. `Cascadelake-Server-v5`) |
| `FSDEV_PATH` | (auto) | Override the virtio-9p shared directory path |

For tests, pass via `--test_env`:

```shell
bazel test --config=qnx-x86_64 //test:main_cpp_qnx --test_env=DISABLE_KVM=1
bazel test --config=qnx-x86_64 //test:main_cpp_qnx --test_env=QEMU_CPU=Cascadelake-Server-v5
```

For shell runs, set directly:

```shell
DISABLE_KVM=1 bazel run --config=qnx-x86_64 //test:main_cpp_qnx_shell
```

### Dependencies

This project uses the [Eclipse SCORE](https://github.com/eclipse-score) Bazel ecosystem:

| Module | Purpose |
|---|---|
| `score_bazel_cpp_toolchains` | QCC cross-compiler (GCC 12.2.0 for QNX SDP 8.0) |
| `score_toolchains_rust` | Ferrocene Rust toolchain for QNX |
| `score_rules_imagefs` | QNX IFS image generation (`qnx_ifs` rule) |
| `score_bazel_platforms` | Platform definitions and config_settings for QNX |
| `googletest` | Google Test framework |
| `rules_rust` | Rust build rules |
