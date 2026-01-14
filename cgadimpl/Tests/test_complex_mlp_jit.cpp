#include <iostream>
#include <random>
#include <iomanip>
#include <vector>
#include <cmath>
#include "ad/runtime/jit_compiler.hpp"
#include "ad/ag_all.hpp"

using namespace ag;

int main() {
    std::cout << "===== COMPLEX MLP JIT TEST =====\n";
    std::cout << std::fixed << std::setprecision(4);

    // ---------- Shapes ----------
    const int B = 8;     // batch size
    const int In = 16;   // input dim
    const int H1 = 64;
    const int H2 = 32;
    const int H3 = 16;
    const int H4 = 8;
    const int Out = 4;  // number of classes

    // ---------- Data ----------
    // Inputs
    Value X = make_tensor(Tensor::randn<float>(Shape{{B, In}}, TensorOptions()), "X");

    // One-hot labels Y[B,Out]
    Tensor Yt(Shape{{B, Out}}, TensorOptions());
    float* yt_data = Yt.data<float>();
    std::mt19937 gen(42);
    std::uniform_int_distribution<int> pick(0, Out - 1);
    for (int i = 0; i < B; ++i) {
        int k = pick(gen);
        for (int j = 0; j < Out; ++j) {
            yt_data[i * Out + j] = (j == k) ? 1.0f : 0.0f;
        }
    }
    Value Y = make_tensor(Yt, "Y_target");

    // ---------- Parameters ----------
    auto opts_param = TensorOptions().with_req_grad(true);
    auto W1 = make_tensor(Tensor::randn<float>(Shape{{In, H1}}, opts_param) * 0.1f, "W1");
    auto b1 = make_tensor(Tensor::zeros(Shape{{1, H1}}, opts_param), "b1");
    auto W2 = make_tensor(Tensor::randn<float>(Shape{{H1, H2}}, opts_param) * 0.1f, "W2");
    auto b2 = make_tensor(Tensor::zeros(Shape{{1, H2}}, opts_param), "b2");
    auto W3 = make_tensor(Tensor::randn<float>(Shape{{H2, H3}}, opts_param) * 0.1f, "W3");
    auto b3 = make_tensor(Tensor::zeros(Shape{{1, H3}}, opts_param), "b3");
    auto W4 = make_tensor(Tensor::randn<float>(Shape{{H3, H4}}, opts_param) * 0.1f, "W4");
    auto b4 = make_tensor(Tensor::zeros(Shape{{1, H4}}, opts_param), "b4");
    auto W5 = make_tensor(Tensor::randn<float>(Shape{{H4, Out}}, opts_param) * 0.1f, "W5");
    auto b5 = make_tensor(Tensor::zeros(Shape{{1, Out}}, opts_param), "b5");
    
    // ---------- Forward ----------
    Value L1 = relu(matmul(X, W1) + b1);
    Value L2 = relu(matmul(L1, W2) + b2);
    Value L3 = relu(matmul(L2, W3) + b3);
    Value L4 = relu(matmul(L3, W4) + b4);
    Value logits = matmul(L4, W5) + b5;
    
    Value loss = mse_loss(logits, Y);

    // ---------- Eager Execution ----------
    std::cout << "Running eager backward pass...\n";
    ag::backward(loss);
    
    float eager_loss = loss.val().to_cpu().data<float>()[0];
    std::cout << "Eager Loss: " << eager_loss << "\n";

    // ---------- JIT Compile ----------
    std::cout << "\nCompiling graph...\n";
    std::vector<Value> inputs = {X, Y};
    std::vector<Value> params = {W1, b1, W2, b2, W3, b3, W4, b4, W5, b5};
    
    ag::jit::CompileOptions opts;
    opts.include_backward = true;
    
    auto comp = ag::jit::compile(loss, inputs, params, opts);
    std::cout << "Graph compilation successful.\n";

    // ---------- Run Compiled Graph ----------
    std::cout << "\nRunning compiled graph...\n";
    std::vector<Tensor*> input_tensors = {&X.node->value, &Y.node->value};
    std::vector<Tensor*> param_tensors = {
        &W1.node->value, &b1.node->value,
        &W2.node->value, &b2.node->value,
        &W3.node->value, &b3.node->value,
        &W4.node->value, &b4.node->value,
        &W5.node->value, &b5.node->value
    };
    
    std::vector<Tensor> jit_outputs;
    bool success = comp.run(input_tensors, param_tensors, jit_outputs);
    
    if (!success) {
        std::cerr << "Compiled execution failed!\n";
        return 1;
    }
    std::cout << "Compiled execution successful.\n";

    // ---------- Verification ----------
    std::cout << "\n--- Verification ---\n";
    
    // Check Loss (Output 0)
    float compiled_loss = jit_outputs[0].to_cpu().data<float>()[0];
    std::cout << "Eager Loss:    " << eager_loss << "\n";
    std::cout << "Compiled Loss: " << compiled_loss << "\n";
    
    if (std::abs(eager_loss - compiled_loss) < 1e-4f) {
        std::cout << "PASS: Loss matches.\n";
    } else {
        std::cout << "FAIL: Loss mismatch.\n";
    }
    
    // Extract JIT outputs for easier access
    Tensor jit_loss = jit_outputs[0];
    Tensor jit_grad_w1 = jit_outputs[1];
    Tensor jit_grad_b1 = jit_outputs[2];

    // Extract Eager gradients for easier access
    Tensor eager_grad_w1 = W1.grad().to_cpu();
    Tensor eager_grad_b1 = b1.grad().to_cpu();

    std::cout << "JIT Loss: " << jit_loss.data<float>()[0] << "\n";
    
    // Debug: Print first few values of gradients
    std::cout << "Eager Grad W1 (first 5): ";
    for(int i=0; i<5; i++) std::cout << eager_grad_w1.data<float>()[i] << " ";
    std::cout << "\n";

    std::cout << "JIT Grad W1 (first 5): ";
    for(int i=0; i<5; i++) std::cout << jit_grad_w1.data<float>()[i] << " ";
    std::cout << "\n";

    // Check for NaNs in full tensor
    int nan_count = 0;
    float* jit_data = jit_grad_w1.data<float>();
    for(size_t i=0; i<jit_grad_w1.numel(); ++i) {
        if(std::isnan(jit_data[i])) nan_count++;
    }
    if (nan_count > 0) {
        std::cout << "FAIL: JIT Grad W1 has " << nan_count << " NaNs out of " << jit_grad_w1.numel() << "\n";
    }

    if (std::isnan(jit_loss.data<float>()[0])) {
        std::cout << "FAIL: JIT Loss is NaN\n";
    }

    
    // Check Gradients
    std::vector<std::string> param_names = {
        "W1", "b1", "W2", "b2", "W3", "b3", "W4", "b4", "W5", "b5"
    };
    std::vector<Value> param_values = {
        W1, b1, W2, b2, W3, b3, W4, b4, W5, b5
    };
    
    bool all_grads_match = true;
    
    for (size_t i = 0; i < param_values.size(); ++i) {
        Tensor eager_grad = param_values[i].grad().to_cpu();
        // JIT output index is i + 1 (because index 0 is loss)
        if (i + 1 >= jit_outputs.size()) {
            std::cerr << "Error: Missing JIT output for " << param_names[i] << "\n";
            all_grads_match = false;
            break;
        }
        
        Tensor jit_grad = jit_outputs[i + 1].to_cpu();
        
        // Calculate Mean Absolute Difference
        float mad = OwnTensor::reduce_mean(OwnTensor::abs(eager_grad - jit_grad, ag::current_stream())).data<float>()[0];
        
        std::cout << param_names[i] << " Grad MAD: " << mad;
        if (mad < 1e-4f) {
            std::cout << " [OK]\n";
        } else {
            std::cout << " [FAIL]\n";
            all_grads_match = false;
        }
    }
    
    if (all_grads_match) {
        std::cout << "PASS: All gradients match.\n";
    } else {
        std::cout << "FAIL: Some gradients mismatch.\n";
    }

    return 0;
}
