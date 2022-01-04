# Project Structure

* [experiments/](experiments/) contains all of the scripts and plotting tools for running different types of experiments.

* [go/](go/) contains all of the Go code belonging to this project. Notable things are [deploy-heyp](go/cmd/deploy-heyp), [host-agent-sim](go/cmd/host-agent-sim) (for faking large numbers of host agents to stress test cluster-agent), [proc-heyp](go/cmd/proc-heyp) for processing deploy-heyp runs, and [dc-control-sim](go/cmd/dc-control-sim) for evaluating non-dynamic properties of usage collection and enforcement techniques.

* [heyp/](heyp/) contains all of the C++ code belonging to this project including the [cluster-agent](heyp/cluster-agent) and [heyp/host-agent](heyp/host-agent).

# Getting Started

## Install dependencies

Make sure that you have the latest version of Go (version 1.17 or later). Then run 
```
$ export TOOLCHAIN=/path/to/install-cpp-build-tools
$ tools/install-work-toolchain.bash # install tools to $TOOLCHAIN, no root needed
```
and follow the instructions of `tools/install-work-toolchain.bash`. NOTE: some scripts require that TOOLCHAIN is set to the correct value.

## Prep to run experiments

Once you've done this, to run any experiments, you'll need to build the C++ code and collect runtime dependencies.

Building C++ code:
```
$ bazel build --config=clang-opt //heyp/...
# or use --config=clang-dbg, --config=clang-asan, or --config=clang-tsan based on your needs
```

Collecting runtime dependencies:
```
$ tools/collect-aux-bins.bash
# NOTE: you can pass arguments to only rebuild certain tools
```

Now you're ready to run [web](experiments/web) and
[scalability](experiments/scalability) experiments. For example
```
$ ./0-rebuild-cmds-and-bundle.bash
$ ./1-gen-configs.bash results/2022-01-04-inc inc # see config.star for the meaning of inc
$ while ! ./2-run-all.bash results/2022-01-04-inc; do sleep 30; done
$ ./3-proc-all.py results/2022-01-04-inc
# Any of the ./4- scripts, if needed
```

NOTE: running the data processing additionally requires `R` with `ggplot2`, `jsonlite`, `parallel`, `reshape2`.

# Other Notes

* To run tests for Go files, use `./wrapgo.bash test ./go/X`
* To run tests for C++ files, use `bazel test --config=clang-opt //heyp/path/to:test` or just `bazel test --config=clang-opt //heyp/...`
* To rebuild the Go protobuf files, use [tools/regen.bash](tools/regen.bash).
* [wrapgo.bash](wrapgo.bash) is a wrapper around `go` so that we can link against C++.
* In order to use C++ algorithms from Go, add code to [heyp/alg/export.cc](heyp/alg/export.cc) and rebuild [go/calg/libexport_bundle.a](go/calg/libexport_bundle.a) by calling [tools/mk-export-bundle.bash](tools/mk-export-bundle.bash)
* [tools/bazel-compilation-database/generate.sh](tools/bazel-compilation-database/generate.sh) can be used to generate auto-complete info for clangd.
