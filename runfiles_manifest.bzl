# *******************************************************************************
# Copyright (c) 2026 Contributors to the Eclipse Foundation
#
# See the NOTICE file(s) distributed with this work for additional
# information regarding copyright ownership.
#
# This program and the accompanying materials are made available under the
# terms of the Apache License Version 2.0 which is available at
# https://www.apache.org/licenses/LICENSE-2.0
#
# SPDX-License-Identifier: Apache-2.0
# *******************************************************************************
"""Rule to generate a text manifest of runfiles for given targets.

This is used to distinguish run_under infrastructure runfiles from test binary
runfiles when Bazel merges them into a single tree under --run_under.
"""

def _short_path_to_runfiles_path(file):
    """Convert a File's short_path to its path inside the .runfiles tree.

    External repo files have short_path like '../repo_name/path/to/file'.
    Inside the merged runfiles tree they appear as 'external/repo_name/path/to/file'.
    """
    path = file.short_path
    if path.startswith("../"):
        return "external/" + path[3:]
    return path

def _runfiles_manifest_impl(ctx):
    all_files = []
    for target in ctx.attr.targets:
        info = target[DefaultInfo]
        all_files.append(info.files)
        if info.default_runfiles:
            all_files.append(info.default_runfiles.files)

    combined = depset(transitive = all_files)

    # Use args object for efficient depset iteration without forcing to list
    args = ctx.actions.args()
    args.add_all(combined, map_each = _short_path_to_runfiles_path, uniquify = True)
    args.set_param_file_format("multiline")

    ctx.actions.write(
        output = ctx.outputs.manifest,
        content = args,
    )

    return [DefaultInfo(files = depset([ctx.outputs.manifest]))]

runfiles_manifest = rule(
    implementation = _runfiles_manifest_impl,
    attrs = {
        "targets": attr.label_list(
            mandatory = True,
            doc = "Targets whose runfiles should be listed in the manifest.",
        ),
    },
    outputs = {
        "manifest": "%{name}.txt",
    },
    doc = "Generates a text file listing all runfiles paths for the given targets, one per line.",
)
