#include <iostream>
#include <torch/script.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <thread>

#define PORT 12345
#define LOOP_COUNT 25
#define DELAY_MS 500

int main() {
    // Load TorchScript model
    torch::jit::script::Module module;
    try {
        module = torch::jit::load("model.pt", torch::kCPU);
    } catch (const c10::Error &e) {
        std::cerr << "Error loading the model: " << e.what() << std::endl;
        return -1;
    }
    std::cout << "Model loaded successfully!\n";

    // Set input dimension (adjust to match your model)
    const int input_dimension = 135;

    // Set up TCP server
    int server_fd, new_socket;
    struct sockaddr_in address{};
    int opt = 1;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    listen(server_fd, 1);
    std::cout << "[TCP] Waiting for MATLAB to connect on port " << PORT << "...\n";

    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }

    std::cout << "[TCP] MATLAB connected.\n";

    // Main loop to send data
    for (int i = 0; i < LOOP_COUNT; ++i) {
        torch::Tensor input = torch::rand({1, input_dimension});
        torch::Tensor output = module.forward({input}).toTensor();

        // Convert tensor to comma-separated string
        std::ostringstream oss;
        auto flat_output = output.flatten();
        for (int j = 0; j < flat_output.size(0); ++j) {
            oss << flat_output[j].item<float>();
            if (j != flat_output.size(0) - 1) oss << ",";
        }
        oss << "\n";

        std::string message = oss.str();
        send(new_socket, message.c_str(), message.size(), 0);

        std::cout << "[TCP] Sent: " << message;

        std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_MS));
    }

    std::cout << "[TCP] Finished sending data.\n";

    close(new_socket);
    close(server_fd);
    return 0;
}


// #include <iostream>
// #include <torch/script.h>


// int main() {
//   // Load the model
//   torch::jit::script::Module module;
//   try {
//     module = torch::jit::load("model.pt", torch::kCPU);
//   } catch (const c10::Error &e) {
//     std::cerr << "Error loading the model: " << e.what() << std::endl;
//     return -1;
// }

//   std::cout << "Model loaded successfully!\n";

//   // TODO;: we wanna see how to make the input dimensions dynamic

//   // Create an input tensor (same shape as used during tracing)
//   int input_dimension = 135; // Make sure to make it matched with the input model 
//   torch::Tensor input = torch::rand({1, input_dimension});

//   // Forward pass
//   torch::Tensor output = module.forward({input}).toTensor();

//   // std::cout << "Model output: "
//   //           << output.slice(/*dim=*/1, /*start=*/0, /*end=*/10) << std::endl;
//   std::cout << "Model output:\n" << output << std::endl;
  
//   return 0;
// }
