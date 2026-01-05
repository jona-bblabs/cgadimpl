# #!/bin/bash
set -euo pipefail

# --- Configuration ---
BUILD_TYPE="Debug"

# This is the most reliable way to tell CMake which compiler to use.
export CUDACXX=/usr/local/cuda-13.0/bin/nvcc

# --- Path Setup ---
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CGADIMPL_DIR="$ROOT/cgadimpl"
CGADIMPL_BUILD="$CGADIMPL_DIR/build"
KERNELS_DIR="$ROOT/kernels"
KERNELS_BUILD="$KERNELS_DIR/build"
TENSOR_DIR="$ROOT/tensor"

MLIR_DIR="/home/blubridge-038/Desktop/llvm-project/build/lib/cmake/mlir"
LLVM_DIR="/home/blubridge-038/Desktop/llvm-project/build/lib/cmake/llvm"
NOVA_BUILD_DIR="$ROOT/Nova-Compiler/build"

echo "== Build Type:    $BUILD_TYPE"
echo "== Using CUDA CXX: $(which nvcc)"

# --- Incremental Build (default) ---
# To force a clean rebuild, run: rm -rf cgadimpl/build kernels/build tensor/lib
# This script now does incremental builds by default for faster compilation

# =========================================================================
# ====> STEP 0: BUILD NOVA-COMPILER (SUBMODULE) <====
# =========================================================================
echo "== Building Nova-Compiler"
mkdir -p "$NOVA_BUILD_DIR"
cd "$NOVA_BUILD_DIR"
cmake .. -DMLIR_DIR="$MLIR_DIR" -DLLVM_DIR="$LLVM_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
make -j$(nproc)
cd "${ROOT}"

# =========================================================================
# ====> STEP 1: BUILD THE TENSOR LIBRARY (INCREMENTAL) <====
# =========================================================================
echo "== Building tensor library"
cd "${TENSOR_DIR}"
make -j$(nproc)
cd "${ROOT}"

# --- STEP 2: Configure and build the core cgadimpl library ---
echo "== Configuring core"
cmake -S "$CGADIMPL_DIR" -B "$CGADIMPL_BUILD" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DMLIR_DIR="$MLIR_DIR" \
  -DLLVM_DIR="$LLVM_DIR" \
  -Dmlir-compiler_DIR="$NOVA_BUILD_DIR"

echo "== Building core"
cmake --build "$CGADIMPL_BUILD" -- -j$(nproc)

# # --- STEP 3: Configure and build the kernel plugins ---
# echo "== Configuring kernel plugins"
# cmake -S "$KERNELS_DIR" -B "$KERNELS_BUILD" \
#   -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
#   -DCGADIMPL_INCLUDE_DIR="$CGADIMPL_DIR/include"

# echo "== Building kernel plugins"
# cmake --build "$KERNELS_BUILD" -- -j$(nproc)

# # --- STEP 4: Stage build artifacts for testing ---
# echo "== Copying kernel plugins to test directory"
# cp "$KERNELS_BUILD/cpu/libagkernels_cpu.so" "$CGADIMPL_BUILD/"
# cp "$KERNELS_BUILD/gpu/libagkernels_cuda.so" "$CGADIMPL_BUILD/"

# --- STEP 5: Run tests ---
echo "== Staging complete. Running tests..."
cd "$CGADIMPL_BUILD"
ctest --output-on-failure
cd "$ROOT"

echo "✅ Build and test run process finished."