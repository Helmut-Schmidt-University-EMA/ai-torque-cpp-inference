#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <random>
#include <iomanip>
#include <torch/script.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <signal.h>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>  
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

volatile sig_atomic_t keep_running = 1;

void signalHandler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n🛑 Received SIGINT (Ctrl+C). Shutting down gracefully...\n";
        keep_running = 0;
    }
}

std::string base64_encode(const unsigned char* buffer, size_t length) {
    BIO *bio, *b64;
    BUF_MEM *buffer_ptr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, buffer, length);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &buffer_ptr);

    std::string result(buffer_ptr->data, buffer_ptr->length);
    BIO_free_all(bio);

    return result;
}

enum WebSocketOpcode {
    WS_CONTINUATION = 0x0,
    WS_TEXT = 0x1,
    WS_BINARY = 0x2,
    WS_CLOSE = 0x8,
    WS_PING = 0x9,
    WS_PONG = 0xA
};

struct WebSocketFrame {
    bool fin;
    uint8_t opcode;
    bool masked;
    uint64_t payload_length;
    std::vector<uint8_t> payload;
    bool valid;
};

struct SensorData {
    std::vector<std::vector<float>> load_sigma_A;
    std::vector<std::vector<float>> load_sigma_B;
    std::vector<std::vector<float>> load_sigma_C;
    std::vector<std::vector<float>> load_sigma_D;
    
    std::vector<std::vector<float>> flux_phi_A;
    std::vector<std::vector<float>> flux_phi_B;
    std::vector<std::vector<float>> flux_phi_C;
    std::vector<std::vector<float>> flux_phi_D;
    
    std::vector<std::vector<float>> amp_sensor_1;
    std::vector<std::vector<float>> amp_sensor_2;
    std::vector<std::vector<float>> amp_sensor_3;
    std::vector<std::vector<float>> amp_sensor_4;
    
    std::vector<std::vector<float>> phase_sensor_1;
    std::vector<std::vector<float>> phase_sensor_2;
    std::vector<std::vector<float>> phase_sensor_3;
    std::vector<std::vector<float>> phase_sensor_4;
    
    std::vector<float> fulldata;
};

class TorchHTTPServer {
private:
    torch::jit::script::Module model;
    int port;
    int input_dimension;
    SOCKET server_socket;

    bool isSocketAlive(SOCKET sock) {
        char buffer[1];
        int result = recv(sock, buffer, 1, MSG_PEEK | MSG_DONTWAIT);
        
        #ifdef _WIN32
        if (result == SOCKET_ERROR) {
            int error = WSAGetLastError();
            return (error == WSAEWOULDBLOCK);
        }
        #else
        if (result == -1) {
            return (errno == EAGAIN || errno == EWOULDBLOCK);
        }
        #endif
        
        return result != 0;
    }

    bool safeSend(SOCKET sock, const char* data, size_t length) {
        if (!isSocketAlive(sock)) {
            return false;
        }
        
        size_t total_sent = 0;
        while (total_sent < length) {
            int sent = send(sock, data + total_sent, length - total_sent, 0);
            if (sent == SOCKET_ERROR || sent == 0) {
                return false;
            }
            total_sent += sent;
        }
        return true;
    }

    int safeRecv(SOCKET sock, char* buffer, size_t length, int timeout_ms = 1000) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        
        int activity = select(sock + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity <= 0) {
            return -1;
        }
        
        return recv(sock, buffer, length, 0);
    }

    std::string generateWebSocketAccept(const std::string& key) {
        std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::string concat = key + magic;
        
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(concat.c_str()), concat.length(), hash);
        
        return base64_encode(hash, SHA_DIGEST_LENGTH);
    }

    bool performWebSocketHandshake(SOCKET client_socket, const std::string& request) {
        std::cout << "📥 Raw request:\n" << request << "\n";
        std::cout << "📏 Request length: " << request.length() << " bytes\n";
        
        std::string ws_key;
        size_t key_pos = request.find("Sec-WebSocket-Key:");
        
        if (key_pos != std::string::npos) {
            size_t key_start = request.find_first_not_of(" \t", key_pos + 18);
            size_t key_end = request.find("\r\n", key_start);
            if (key_end == std::string::npos) {
                key_end = request.find("\n", key_start);
            }
            ws_key = request.substr(key_start, key_end - key_start);
            
            // Trim any trailing whitespace
            size_t last = ws_key.find_last_not_of(" \t\r\n");
            if (last != std::string::npos) {
                ws_key = ws_key.substr(0, last + 1);
            }
        } else {
            std::cout << "❌ No Sec-WebSocket-Key found in request\n";
            return false;
        }
    
        std::cout << "🔑 WebSocket-Key: [" << ws_key << "]\n";
        std::string accept_key = generateWebSocketAccept(ws_key);
        std::cout << "🔑 Accept-Key: [" << accept_key << "]\n";
    
        // Build response with explicit \r\n line endings (crucial for LabVIEW)
        std::string response = 
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept_key + "\r\n"
            "\r\n";
    
        std::cout << "📤 Sending handshake response (" << response.length() << " bytes)\n";
        
        // Send the response and verify it was sent completely
        if (!safeSend(client_socket, response.c_str(), response.length())) {
            std::cout << "❌ Failed to send handshake response\n";
            return false;
        }
    
        // Small delay to ensure LabVIEW receives the complete handshake
        #ifdef _WIN32
        Sleep(50);
        #else
        usleep(50000);
        #endif
    
        std::cout << "✓ WebSocket handshake completed\n";
        return true;
    }

    WebSocketFrame decodeWebSocketFrame(SOCKET client_socket) {
        WebSocketFrame frame;
        frame.valid = false;

        char header[2];
        int bytes = safeRecv(client_socket, header, 2, 5000);
        if (bytes != 2) return frame;

        frame.fin = (header[0] & 0x80) != 0;
        frame.opcode = header[0] & 0x0F;
        frame.masked = (header[1] & 0x80) != 0;
        frame.payload_length = header[1] & 0x7F;

        if (frame.payload_length == 126) {
            char extended[2];
            if (safeRecv(client_socket, extended, 2) != 2) return frame;
            frame.payload_length = (static_cast<uint8_t>(extended[0]) << 8) | 
                                   static_cast<uint8_t>(extended[1]);
        } else if (frame.payload_length == 127) {
            char extended[8];
            if (safeRecv(client_socket, extended, 8) != 8) return frame;
            frame.payload_length = 0;
            for (int i = 0; i < 8; i++) {
                frame.payload_length = (frame.payload_length << 8) | static_cast<uint8_t>(extended[i]);
            }
        }

        uint8_t mask[4] = {0};
        if (frame.masked) {
            if (safeRecv(client_socket, reinterpret_cast<char*>(mask), 4) != 4) return frame;
        }

        frame.payload.resize(frame.payload_length);
        size_t total_received = 0;
        while (total_received < frame.payload_length) {
            int bytes_received = safeRecv(client_socket, 
                                         reinterpret_cast<char*>(frame.payload.data() + total_received),
                                         frame.payload_length - total_received);
            if (bytes_received <= 0) return frame;
            total_received += bytes_received;
        }

        if (frame.masked) {
            for (size_t i = 0; i < frame.payload_length; i++) {
                frame.payload[i] ^= mask[i % 4];
            }
        }

        frame.valid = true;
        return frame;
    }

    bool sendWebSocketFrame(SOCKET client_socket, const std::string& message, uint8_t opcode = WS_TEXT) {
        std::vector<uint8_t> frame;
        
        frame.push_back(0x80 | opcode);
        
        size_t msg_len = message.length();
        if (msg_len < 126) {
            frame.push_back(static_cast<uint8_t>(msg_len));
        } else if (msg_len < 65536) {
            frame.push_back(126);
            frame.push_back((msg_len >> 8) & 0xFF);
            frame.push_back(msg_len & 0xFF);
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; i--) {
                frame.push_back((msg_len >> (i * 8)) & 0xFF);
            }
        }

        frame.insert(frame.end(), message.begin(), message.end());

        return safeSend(client_socket, reinterpret_cast<char*>(frame.data()), frame.size());
    }

    // Helper to flatten 2D array into 1D array
    std::vector<float> flatten2D(const std::vector<std::vector<float>>& data) {
        std::vector<float> result;
        for (const auto& row : data) {
            for (const auto& val : row) {
                result.push_back(val);
            }
        }
        return result;
    }

    std::string format1DArray(const std::vector<float>& data) {
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < data.size(); i++) {
            if (i > 0) ss << ",";
            ss << std::fixed << std::setprecision(4) << data[i];
        }
        ss << "]";
        return ss.str();
    }

    SensorData parseModelOutput(const std::vector<float>& output) {
        SensorData data;
        
        data.load_sigma_A.resize(4, std::vector<float>(2));
        data.load_sigma_B.resize(4, std::vector<float>(2));
        data.load_sigma_C.resize(4, std::vector<float>(2));
        data.load_sigma_D.resize(4, std::vector<float>(2));
        
        data.flux_phi_A.resize(4, std::vector<float>(2));
        data.flux_phi_B.resize(4, std::vector<float>(2));
        data.flux_phi_C.resize(4, std::vector<float>(2));
        data.flux_phi_D.resize(4, std::vector<float>(2));
        
        data.amp_sensor_1.resize(4, std::vector<float>(2));
        data.amp_sensor_2.resize(4, std::vector<float>(2));
        data.amp_sensor_3.resize(4, std::vector<float>(2));
        data.amp_sensor_4.resize(4, std::vector<float>(2));
        
        data.phase_sensor_1.resize(4, std::vector<float>(2));
        data.phase_sensor_2.resize(4, std::vector<float>(2));
        data.phase_sensor_3.resize(4, std::vector<float>(2));
        data.phase_sensor_4.resize(4, std::vector<float>(2));
        
        data.fulldata = output;
        
        int idx = 0;
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.load_sigma_A[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.load_sigma_B[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.load_sigma_C[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.load_sigma_D[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.flux_phi_A[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.flux_phi_B[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.flux_phi_C[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.flux_phi_D[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.amp_sensor_1[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.amp_sensor_2[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.amp_sensor_3[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.amp_sensor_4[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.phase_sensor_1[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.phase_sensor_2[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.phase_sensor_3[i][j] = output[idx++];
        
        for (int i = 0; i < 4 && idx < output.size(); i++)
            for (int j = 0; j < 2 && idx < output.size(); j++)
                data.phase_sensor_4[i][j] = output[idx++];
        
        return data;
    }

    // Completely flattened JSON - no nesting at all
    std::string createFlattenedJSON(const SensorData& data) {
        std::ostringstream json;
        json << "{";
        json << "\"load_sigma_A\":" << format1DArray(flatten2D(data.load_sigma_A)) << ",";
        json << "\"load_sigma_B\":" << format1DArray(flatten2D(data.load_sigma_B)) << ",";
        json << "\"load_sigma_C\":" << format1DArray(flatten2D(data.load_sigma_C)) << ",";
        json << "\"load_sigma_D\":" << format1DArray(flatten2D(data.load_sigma_D)) << ",";
        json << "\"flux_phi_A\":" << format1DArray(flatten2D(data.flux_phi_A)) << ",";
        json << "\"flux_phi_B\":" << format1DArray(flatten2D(data.flux_phi_B)) << ",";
        json << "\"flux_phi_C\":" << format1DArray(flatten2D(data.flux_phi_C)) << ",";
        json << "\"flux_phi_D\":" << format1DArray(flatten2D(data.flux_phi_D)) << ",";
        json << "\"amp_sensor_1\":" << format1DArray(flatten2D(data.amp_sensor_1)) << ",";
        json << "\"amp_sensor_2\":" << format1DArray(flatten2D(data.amp_sensor_2)) << ",";
        json << "\"amp_sensor_3\":" << format1DArray(flatten2D(data.amp_sensor_3)) << ",";
        json << "\"amp_sensor_4\":" << format1DArray(flatten2D(data.amp_sensor_4)) << ",";
        json << "\"phase_sensor_1\":" << format1DArray(flatten2D(data.phase_sensor_1)) << ",";
        json << "\"phase_sensor_2\":" << format1DArray(flatten2D(data.phase_sensor_2)) << ",";
        json << "\"phase_sensor_3\":" << format1DArray(flatten2D(data.phase_sensor_3)) << ",";
        json << "\"phase_sensor_4\":" << format1DArray(flatten2D(data.phase_sensor_4)) << ",";
        json << "\"fulldata\":" << format1DArray(data.fulldata);
        json << "}";
        return json.str();
    }

    void handleWebSocket(SOCKET client_socket) {
        std::cout << "✓ WebSocket connection established\n";

        if (!sendWebSocketFrame(client_socket, "{\"status\":\"connected\"}")) {
            std::cout << "ℹ Client disconnected immediately\n";
            return;
        }

        while (keep_running) {
            WebSocketFrame frame = decodeWebSocketFrame(client_socket);
            
            if (!frame.valid) {
                std::cout << "ℹ WebSocket client disconnected\n";
                break;
            }

            if (frame.opcode == WS_CLOSE) {
                std::cout << "ℹ WebSocket close frame received\n";
                sendWebSocketFrame(client_socket, "", WS_CLOSE);
                break;
            }

            if (frame.opcode == WS_PING) {
                if (!sendWebSocketFrame(client_socket, "", WS_PONG)) {
                    break;
                }
                continue;
            }

            if (frame.opcode == WS_TEXT) {
                std::string message(frame.payload.begin(), frame.payload.end());
                std::vector<float> input_data = parseInputData(message);
                
                if (!input_data.empty()) {
                    std::vector<float> output = runInferenceGetOutput(input_data);
                    if (!output.empty()) {
                        SensorData sensor_data = parseModelOutput(output);
                        std::string result = createFlattenedJSON(sensor_data);
                        
                        if (!sendWebSocketFrame(client_socket, result)) {
                            std::cout << "ℹ Client disconnected during response\n";
                            break;
                        }
                    }
                } else {
                    sendWebSocketFrame(client_socket, "{\"error\":\"Invalid input\"}");
                }
            }
        }

        std::cout << "ℹ WebSocket connection closed\n";
    }

    void handleWebSocketDemo(SOCKET client_socket) {
        std::cout << "▶ WebSocket demo streaming started\n";


        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(0.0f, 1.0f);

        const int max_iterations = 20;
        for (int i = 0; i < max_iterations && keep_running; i++) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(client_socket, &read_fds);
            
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 50000;

            int activity = select(client_socket + 1, &read_fds, NULL, NULL, &timeout);
            
            if (activity > 0 && FD_ISSET(client_socket, &read_fds)) {
                WebSocketFrame frame = decodeWebSocketFrame(client_socket);
                
                if (!frame.valid || frame.opcode == WS_CLOSE) {
                    std::cout << "ℹ WebSocket demo client disconnected\n";
                    sendWebSocketFrame(client_socket, "", WS_CLOSE);
                    return;
                }
                
                if (frame.opcode == WS_PING) {
                    sendWebSocketFrame(client_socket, "", WS_PONG);
                }
            }

            std::vector<float> random_input(input_dimension);
            for (auto& val : random_input) {
                val = dis(gen);
            }

            std::vector<float> output = runInferenceGetOutput(random_input);
            if (!output.empty()) {
                SensorData sensor_data = parseModelOutput(output);
                std::string result = createFlattenedJSON(sensor_data);

                if (!sendWebSocketFrame(client_socket, result)) {
                    std::cout << "ℹ Client disconnected during demo\n";
                    return;
                }
            }

            #ifdef _WIN32
            Sleep(500);
            #else
            usleep(500000);
            #endif
        }

        sendWebSocketFrame(client_socket, "{\"status\":\"completed\"}");
        std::cout << "✓ WebSocket demo completed\n";
    }

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
        std::ostringstream body;
        body << "{\"error\":\"" << error << "\"}";
        
        std::ostringstream response;
        response << "HTTP/1.1 " << code << " Error\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Access-Control-Allow-Origin: *\r\n"
                 << "Content-Length: " << body.str().length() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << body.str();
        return response.str();
    }

    std::vector<float> parseInputData(const std::string& body) {
        std::vector<float> values;
        
        try {
            size_t data_pos = body.find("\"data\":");
            if (data_pos == std::string::npos) {
                return values;
            }
            
            size_t bracket_start = body.find("[", data_pos);
            size_t bracket_end = body.find("]", bracket_start);
            
            if (bracket_start == std::string::npos || bracket_end == std::string::npos) {
                return values;
            }
            
            std::string data_section = body.substr(bracket_start + 1, bracket_end - bracket_start - 1);
            
            std::istringstream ss(data_section);
            std::string token;
            while (std::getline(ss, token, ',')) {
                token.erase(0, token.find_first_not_of(" \t\n\r"));
                token.erase(token.find_last_not_of(" \t\n\r") + 1);
                
                if (!token.empty()) {
                    try {
                        values.push_back(std::stof(token));
                    } catch (...) {}
                }
            }
        } catch (...) {
            values.clear();
        }
        
        return values;
    }

    std::vector<float> runInferenceGetOutput(const std::vector<float>& input_data) {
        std::vector<float> result;
        
        try {
            if (input_data.size() != static_cast<size_t>(input_dimension)) {
                return result;
            }

            torch::Tensor input = torch::from_blob(
                const_cast<float*>(input_data.data()), 
                {1, static_cast<long>(input_dimension)}, 
                torch::kFloat32
            ).clone();

            torch::NoGradGuard no_grad;
            torch::Tensor output = model.forward({input}).toTensor();

            auto output_data = output.accessor<float, 2>();
            for (int i = 0; i < output.size(1); i++) {
                result.push_back(output_data[0][i]);
            }
            
        } catch (const std::exception& e) {
            std::cerr << "❌ Inference error: " << e.what() << "\n";
        }
        
        return result;
    }

    void streamInference(SOCKET client_socket) {
        std::cout << "▶ Starting SSE stream\n";
        
        int flag = 1;
        setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));
        
        std::ostringstream header;
        header << "HTTP/1.1 200 OK\r\n"
               << "Content-Type: text/event-stream\r\n"
               << "Cache-Control: no-cache\r\n"
               << "Access-Control-Allow-Origin: *\r\n"
               << "Connection: keep-alive\r\n\r\n";
        
        if (!safeSend(client_socket, header.str().c_str(), header.str().length())) {
            return;
        }
    
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    
        for (int i = 0; i < 10 && keep_running; i++) {
            if (!isSocketAlive(client_socket)) break;
    
            std::vector<float> random_input(input_dimension);
            for (auto& val : random_input) val = dis(gen);
    
            std::vector<float> output = runInferenceGetOutput(random_input);
            if (!output.empty()) {
                SensorData sensor_data = parseModelOutput(output);
                std::string result = createFlattenedJSON(sensor_data);
                
                // SSE format: "data: " + content + "\n\n"
                std::string sse_message = "data: " + result + "\n\n";
                
                if (!safeSend(client_socket, sse_message.c_str(), sse_message.length())) {
                    break;
                }
                
                std::cout << "✓ Sent chunk " << (i + 1) << "/10\n";
            }
    
            #ifdef _WIN32
            Sleep(1000);
            #else
            usleep(1000000);
            #endif
        }
        
        std::cout << "✓ Stream completed\n";
    }

    void handleRequest(SOCKET client_socket) {
        try {
            char buffer[8192];
            
            #ifdef _WIN32
            DWORD timeout = 5000; 
            setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
            #else
            struct timeval timeout;
            timeout.tv_sec = 5;
            timeout.tv_usec = 0;
            setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            #endif
            
            int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
            
            if (bytes_received <= 0) {
                std::cout << "⚠ No data received from client\n";
                return;
            }
        
            buffer[bytes_received] = '\0';
            std::string request(buffer);
        
            std::istringstream request_stream(request);
            std::string method, path, version;
            request_stream >> method >> path >> version;
        
            std::cout << "📨 " << method << " " << path << " (from " 
                      << (request.find("LabVIEW") != std::string::npos ? "LabVIEW" : "browser") 
                      << ")\n";
        
            if (request.find("Upgrade: websocket") != std::string::npos ||
                request.find("Upgrade: WebSocket") != std::string::npos) {
                
                std::cout << "🔄 WebSocket upgrade request detected\n";
                
                if (path == "/ws2") {
                    if (performWebSocketHandshake(client_socket, request)) {
                        handleWebSocketDemo(client_socket);
                    }
                } else if (path == "/ws") {
                    if (performWebSocketHandshake(client_socket, request)) {
                        handleWebSocket(client_socket);
                    }
                } else {
                    std::cout << "❌ Unknown WebSocket path: " << path << "\n";
                }
                return;
            }
        
            std::string response;
        
            if (method == "POST" && path == "/predict") {
                size_t body_pos = request.find("\r\n\r\n");
                if (body_pos != std::string::npos) {
                    std::string body = request.substr(body_pos + 4);
                    std::vector<float> input_data = parseInputData(body);
        
                    if (input_data.empty()) {
                        response = createErrorResponse("Invalid input format");
                    } else {
                        std::vector<float> output = runInferenceGetOutput(input_data);
                        if (!output.empty()) {
                            SensorData sensor_data = parseModelOutput(output);
                            std::string result = createFlattenedJSON(sensor_data);
                            response = createHTTPResponse(result);
                        } else {
                            response = createErrorResponse("Inference failed");
                        }
                    }
                } else {
                    response = createErrorResponse("No request body found");
                }
            }
            else if (method == "GET" && path == "/") {
                std::ostringstream info;
                info << "{"
                     << "\"service\":\"HSU AI Torque Sensor\","
                     << "\"status\":\"running\","
                     << "\"port\":" << port << ","
                     << "\"input_dimension\":" << input_dimension << ","
                     << "\"endpoints\":{"
                     << "\"websocket\":\"ws://localhost:" << port << "/ws\","
                     << "\"websocket_demo\":\"ws://localhost:" << port << "/ws2\","
                     << "\"http_stream\":\"http://localhost:" << port << "/stream\","
                     << "\"predict\":\"http://localhost:" << port << "/predict\""
                     << "}}";
                response = createHTTPResponse(info.str());
            }
            else if (method == "GET" && path == "/stream") {
                streamInference(client_socket);
                return;
            }
            else if (method == "OPTIONS") {
                std::ostringstream cors_response;
                cors_response << "HTTP/1.1 200 OK\r\n"
                              << "Access-Control-Allow-Origin: *\r\n"
                              << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                              << "Access-Control-Allow-Headers: Content-Type\r\n"
                              << "Connection: close\r\n\r\n";
                response = cors_response.str();
            }
            else {
                response = createErrorResponse("Not found", 404);
            }
        
            safeSend(client_socket, response.c_str(), response.length());
        
        } catch (const std::exception& e) {
            std::cerr << "⚠ Request handling error: " << e.what() << "\n";
        }
    }

public:
    TorchHTTPServer(const std::string& model_path, int port, int input_dim) 
        : port(port), input_dimension(input_dim), server_socket(INVALID_SOCKET) {
        
        try {
            model = torch::jit::load(model_path, torch::kCPU);
            model.eval();
            std::cout << "✓ Model loaded: " << model_path << "\n";
        } catch (const c10::Error &e) {
            throw std::runtime_error("Model loading failed: " + std::string(e.what()));
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
            throw std::runtime_error("Socket creation failed");
        }

        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            throw std::runtime_error("Bind failed on port " + std::to_string(port));
        }

        if (listen(server_socket, 10) == SOCKET_ERROR) {
            throw std::runtime_error("Listen failed");
        }

        std::cout << "\n╔════════════════════════════════════════════════╗\n"
                  << "║          HSU AI Torque Sensor Server          ║\n"
                  << "╠════════════════════════════════════════════════╣\n"
                  << "║ Port: " << std::setw(40) << std::left << port << "║\n"
                  << "║ Input Dimension: " << std::setw(31) << std::left << input_dimension << "║\n"
                  << "╠════════════════════════════════════════════════╣\n"
                  << "║ Endpoints:                                     ║\n"
                  << "║  GET  /          Server info                   ║\n"
                  << "║  POST /predict   Run inference                 ║\n"
                  << "║  GET  /stream    HTTP streaming                ║\n"
                  << "║  WS   /ws        WebSocket inference           ║\n"
                  << "║  WS   /ws2       WebSocket auto-stream         ║\n"
                  << "╠════════════════════════════════════════════════╣\n"
                  << "║ Press Ctrl+C to stop                           ║\n"
                  << "╚════════════════════════════════════════════════╝\n\n";

        while (keep_running) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);

            if (client_socket == INVALID_SOCKET) {
                if (!keep_running) break;
                continue;
            }

            handleRequest(client_socket);
            closesocket(client_socket);
        }

        std::cout << "✓ Server stopped gracefully\n";
    }
};

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);

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
        std::cerr << "❌ Fatal error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "👋 Shutdown complete\n";
    return 0;
}