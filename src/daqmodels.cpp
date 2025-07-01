#include <iostream>
#include <torch/script.h>


int main() {
  // Load the model
  torch::jit::script::Module module;
  try {
    module = torch::jit::load("model.pt", torch::kCPU);
  } catch (const c10::Error &e) {
    std::cerr << "Error loading the model: " << e.what() << std::endl;
    return -1;
}

  std::cout << "Model loaded successfully!\n";

  // TODO;: we wanna see how to make the input dimensions dynamic

  // Create an input tensor (same shape as used during tracing)
  int input_dimension = 135; // Make sure to make it matched with the input model 
  torch::Tensor input = torch::rand({1, input_dimension});

  // Forward pass
  torch::Tensor output = module.forward({input}).toTensor();

  // std::cout << "Model output: "
  //           << output.slice(/*dim=*/1, /*start=*/0, /*end=*/10) << std::endl;
  std::cout << "Model output:\n" << output << std::endl;
  
  return 0;
}
