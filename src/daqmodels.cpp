#include <iostream>
#include <torch/script.h>

int main() {
  // Load the model
  torch::jit::script::Module module;
  try {
    module = torch::jit::load("model.pt");
  } catch (const c10::Error &e) {
    std::cerr << "Error loading the model\n";
    return -1;
  }

  std::cout << "Model loaded successfully!\n";

  // TODO;: we wanna see how to make the input dimensions dynamic

  // Create an input tensor (same shape as used during tracing)
  torch::Tensor input = torch::rand({1, 1, 28, 28});

  // Forward pass
  torch::Tensor output = module.forward({input}).toTensor();

  std::cout << "Model output: "
            << output.slice(/*dim=*/1, /*start=*/0, /*end=*/10) << std::endl;

  return 0;
}
