#!/bin/bash
#
# Install an LLVM-based toolchain and bazel for building this repository.
#
# Prereqs: TOOLCHAIN must be defined (see ENV-VARS)

LLVM_VERSION=12.0.0
BAZEL_VERSION=4.0.0

set -e -o pipefail

echo "NOTE: building and running is only supported on x86-64 linux"

export TOOLCHAIN=${TOOLCHAIN:-/w/uluyol/fp-toolchain}

if ! [[ $TOOLCHAIN =~ ^/ ]]; then 
    echo "TOOLCHAIN must be set to an absolute path" >&2
    exit 2
fi

if [[ -e $TOOLCHAIN ]]; then
    echo "TOOLCHAIN must not already exist" >&2
    exit 3
fi

mkdir -p "$TOOLCHAIN"
(
cd "$TOOLCHAIN"

echo "fetch and extract llvm"
curl -sL "https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/clang+llvm-${LLVM_VERSION}-x86_64-linux-gnu-ubuntu-16.04.tar.xz" | tar xJf -
mv clang+llvm-${LLVM_VERSION}-x86_64-linux-gnu-ubuntu-16.04 clang+llvm

echo "fetch bazel"
curl -sL "https://github.com/bazelbuild/bazel/releases/download/${BAZEL_VERSION}/bazel-${BAZEL_VERSION}-installer-linux-x86_64.sh" > bazel-installer.sh
echo "install bazel"
bash bazel-installer.sh --prefix="$TOOLCHAIN/bazel" --skip-uncompress >/dev/null
rm bazel-installer.sh
)

cat <<EOF
Add the following script as 'bazel' to your PATH

#!/bin/bash

exec env \\
    BAZEL_CXXOPTS=-stdlib=libc++:-std=c++17 \\
    BAZEL_LINKOPTS=-Wl,-rpath=$TOOLCHAIN/clang+llvm/lib/:-lc++:-lm \\
    CC=$TOOLCHAIN/clang+llvm/bin/clang \\
    ${TOOLCHAIN}/bazel/bin/bazel --output_user_root=${TOOLCHAIN}/bazel-output_user_root "\$@"
EOF
