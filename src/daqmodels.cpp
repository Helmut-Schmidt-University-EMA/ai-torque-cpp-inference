#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <random>
#include <iomanip>
#include <torch/script.h>

// Cross-platform socket headers
#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

class TorchHTTPServer {
private:
    torch::jit::script::Module model;
    int port;
    int input_dimension;
    SOCKET server_socket;

    // HTTP Response Helpers
    std::string createHTTPResponse(const std::string& body, const std::string& content_type = "application/json") {
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: " << content_type << "\r\n"
                 << "Access-Control-Allow-Origin: *\r\n"
                 << "Content-Length: " << body.length() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << body;
        return response.str();
    }

    std::string createErrorResponse(const std::string& error, int code = 400) {
        std::ostringstream response;
        response << "HTTP/1.1 " << code << " Error\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Access-Control-Allow-Origin: *\r\n"
                 << "Connection: close\r\n\r\n"
                 << "{\"error\": \"" << error << "\"}";
        return response.str();
    }

    // Parse JSON input data
    std::vector<float> parseInputData(const std::string& body) {
        std::vector<float> values;
        
        size_t data_pos = body.find("\"data\":");
        if (data_pos == std::string::npos) return values;
        
        size_t bracket_start = body.find("[", data_pos);
        size_t bracket_end = body.find("]", bracket_start);
        
        if (bracket_start == std::string::npos || bracket_end == std::string::npos) return values;
        
        std::string data_section = body.substr(bracket_start + 1, bracket_end - bracket_start - 1);
        
        std::istringstream ss(data_section);
        std::string token;
        while (std::getline(ss, token, ',')) {
            try {
                values.push_back(std::stof(token));
            } catch (...) {
                // Skip invalid values
            }
        }
        
        return values;
    }

    // Run model inference
    std::string runInference(const std::vector<float>& input_data) {
        if (input_data.size() != input_dimension) {
            return "{\"error\": \"Expected " + std::to_string(input_dimension) + 
                   " inputs, got " + std::to_string(input_data.size()) + "\"}";
        }

        torch::Tensor input = torch::from_blob(
            const_cast<float*>(input_data.data()), 
            {1, input_dimension}, 
            torch::kFloat32
        ).clone();

        torch::NoGradGuard no_grad;
        torch::Tensor output = model.forward({input}).toTensor();

        std::ostringstream json;
        json << "{\"output\": [";
        
        auto output_data = output.accessor<float, 2>();
        for (int i = 0; i < output.size(1); i++) {
            if (i > 0) json << ", ";
            json << output_data[0][i];
        }
        
        json << "]}";
        return json.str();
    }

    // Stream inference demo
    void streamInference(SOCKET client_socket) {
        // Send chunked transfer encoding header
        std::ostringstream header;
        header << "HTTP/1.1 200 OK\r\n"
               << "Content-Type: text/plain\r\n"
               << "Transfer-Encoding: chunked\r\n"
               << "Access-Control-Allow-Origin: *\r\n"
               << "Connection: close\r\n\r\n";
        send(client_socket, header.str().c_str(), header.str().length(), 0);

        // Random number generator for demo
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(0.0f, 1.0f);

        // Stream 10 inference iterations
        for (int i = 0; i < 10; i++) {
            // Generate random input
            std::vector<float> random_input(input_dimension);
            for (auto& val : random_input) {
                val = dis(gen);
            }

            // Run inference
            std::string result = runInference(random_input);
            
            std::ostringstream chunk_data;
            chunk_data << "Iteration " << (i + 1) << ": " << result << "\n";
            std::string chunk = chunk_data.str();

            // Send chunk size in hex
            std::ostringstream chunk_header;
            chunk_header << std::hex << chunk.size() << "\r\n";
            send(client_socket, chunk_header.str().c_str(), chunk_header.str().length(), 0);
            
            // Send chunk data
            send(client_socket, chunk.c_str(), chunk.size(), 0);
            send(client_socket, "\r\n", 2, 0);

            // Delay between iterations
            #ifdef _WIN32
            Sleep(300);
            #else
            usleep(300000);
            #endif
        }

        // Send final chunk (0 size = end)
        send(client_socket, "0\r\n\r\n", 5, 0);
        std::cout << "Stream completed\n";
    }

    // Route handler
    void handleRequest(SOCKET client_socket) {
        char buffer[8192];
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_received <= 0) return;

        buffer[bytes_received] = '\0';
        std::string request(buffer);

        std::istringstream request_stream(request);
        std::string method, path, version;
        request_stream >> method >> path >> version;

        std::cout << "Received: " << method << " " << path << std::endl;

        std::string response;

        // POST /predict - Run inference
        if (method == "POST" && path == "/predict") {
            size_t body_pos = request.find("\r\n\r\n");
            if (body_pos != std::string::npos) {
                std::string body = request.substr(body_pos + 4);
                std::vector<float> input_data = parseInputData(body);
                
                if (input_data.empty()) {
                    response = createErrorResponse("Invalid input format. Expected JSON: {\"data\": [values...]}");
                } else {
                    std::string result = runInference(input_data);
                    response = createHTTPResponse(result);
                    std::cout << "Inference completed\n";
                }
            } else {
                response = createErrorResponse("No request body found");
            }
        } 
        // GET / - Server info
        else if (method == "GET" && path == "/") {
            std::ostringstream info;
            info << "{"
                 << "\"welcome\": \"HSU AI Torque Sensor\", "
                 << "\"status\": \"running\", "
                 << "\"port\": " << port << ", "
                 << "\"input_dimension\": " << input_dimension
                 << "}";
            response = createHTTPResponse(info.str());
        } 
        // GET /stream - Streaming inference demo
        else if (method == "GET" && path == "/stream") {
            streamInference(client_socket);
            return; // Stream handles its own response
        }
        // 404 for unknown routes
        else {
            response = createErrorResponse("Not found", 404);
        }

        send(client_socket, response.c_str(), response.length(), 0);
    }

public:
    TorchHTTPServer(const std::string& model_path, int port, int input_dim) 
        : port(port), input_dimension(input_dim), server_socket(INVALID_SOCKET) {
        
        try {
            model = torch::jit::load(model_path, torch::kCPU);
            model.eval();
            std::cout << "✓ Model loaded successfully: " << model_path << "\n";
        } catch (const c10::Error &e) {
            throw std::runtime_error("Error loading model: " + std::string(e.what()));
        }

        #ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
        #endif
    }

    ~TorchHTTPServer() {
        if (server_socket != INVALID_SOCKET) {
            closesocket(server_socket);
        }
        #ifdef _WIN32
        WSACleanup();
        #endif
    }

    void start() {
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == INVALID_SOCKET) {
            throw std::runtime_error("Failed to create socket");
        }

        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            throw std::runtime_error("Failed to bind to port " + std::to_string(port));
        }

        if (listen(server_socket, 5) == SOCKET_ERROR) {
            throw std::runtime_error("Failed to listen on socket");
        }

        std::cout << "\n=== Server Started ===\n"
                  << "Port: " << port << "\n"
                  << "Input dimension: " << input_dimension << "\n"
                  << "Endpoints:\n"
                  << "  GET  http://localhost:" << port << "/        - Server info\n"
                  << "  POST http://localhost:" << port << "/predict - Run inference\n"
                  << "  GET  http://localhost:" << port << "/stream  - Streaming demo\n"
                  << "======================\n\n";

        while (true) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);

            if (client_socket != INVALID_SOCKET) {
                handleRequest(client_socket);
                closesocket(client_socket);
            }
        }
    }
};

int main(int argc, char* argv[]) {
    try {
        std::string model_path = "model.pt";
        int port = 8080;
        int input_dimension = 135;

        if (argc > 1) model_path = argv[1];
        if (argc > 2) port = std::atoi(argv[2]);
        if (argc > 3) input_dimension = std::atoi(argv[3]);

        TorchHTTPServer server(model_path, port, input_dimension);
        server.start();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
// TODO:: we need to cache the model 

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
