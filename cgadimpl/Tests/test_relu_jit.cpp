#include "ad/ag_all.hpp"
#include <iostream>
#include <vector>
#include "ad/runtime/jit_compiler.hpp"
#include <chrono>

using namespace ag;

int main() {
    std::cout << "===== JIT RELU TEST =====\n";

    // ---------- Shapes & Data ----------
    const int B = 8;   // batch size
    const int In = 16; // input dim
    const int Out = 10; // output dim

    // Use default options (CPU, requires_grad=false)
    auto opts_const = TensorOptions();
    Tensor Xt = Tensor::randn<float>(Shape{{B, In}}, opts_const);
    Value X = make_tensor(Xt, "X");

    // ---------- Parameters ----------
    auto opts_param = TensorOptions().with_req_grad(true);
    auto W1 = make_tensor(Tensor::randn<float>(Shape{{In, Out}}, opts_param), "W1");
    auto b1 = make_tensor(Tensor::zeros(Shape{{1, Out}}, opts_param), "b1");
    
    // Target for MSE Loss
    auto opts_target = TensorOptions().with_req_grad(false);
    auto target = make_tensor(Tensor::randn<float>(Shape{{B, Out}}, opts_target), "target");

    // ---------- Forward Pass WITH RELU ----------
    // Z = relu(X @ W1 + b1)
    Value Z = relu(matmul(X, W1) + b1);

    // MSE Loss
    Value loss = mse_loss(Z, target);
    
    std::cout << "Eager forward pass completed.\n";
    
    // ---------- Eager Backward ----------
    ag::backward(loss);
    float eager_loss = loss.val().to_cpu().data<float>()[0];
    std::cout << "Eager Loss: " << eager_loss << "\n";

    // ---------- JIT Compilation ----------
    std::cout << "\nCompiling graph...\n";

    std::vector<Value> inputs = {X, target};
    std::vector<Value> params = {W1, b1};

    ag::jit::CompileOptions opts;
    opts.include_backward = true;
    auto comp = ag::jit::compile(loss, inputs, params, opts);

    std::cout << "Graph compilation successful.\n";

    // ---------- JIT Execution ----------
    std::cout << "\nRunning compiled graph...\n";

    std::vector<Tensor*> in_ptrs = {&X.node->value, &target.node->value};
    std::vector<Tensor*> par_ptrs = {&W1.node->value, &b1.node->value};

    std::vector<Tensor> jit_outputs;
    bool ok = comp.run(in_ptrs, par_ptrs, jit_outputs);
    
    if (!ok) {
        std::cerr << "FAIL: JIT execution failed.\n";
        return 1;
    }

    std::cout << "Compiled execution successful.\n";
    
    // ---------- Verification ----------
    float compiled_loss = jit_outputs[0].to_cpu().data<float>()[0];

    std::cout << "\n--- Verification ---\n";
    std::cout << "Eager Loss:    " << eager_loss << "\n";
    std::cout << "Compiled Loss: " << compiled_loss << "\n";

    if (std::abs(eager_loss - compiled_loss) < 1e-4f) {
        std::cout << "PASS: Loss matches.\n";
    } else {
        std::cout << "FAIL: Loss mismatch.\n";
    }
    
    return 0;
}
