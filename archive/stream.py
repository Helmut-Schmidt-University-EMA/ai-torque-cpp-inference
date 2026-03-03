# Python Server 

# import socket

# HOST = 'localhost'
# PORT = 12345

# # Set up server
# with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
#     server.bind((HOST, PORT))
#     server.listen(1)
#     print(f"[Python] Server listening on {HOST}:{PORT}...")
#     conn, addr = server.accept()
#     with conn:
#         print(f"[Python] Connection from {addr}")
#         data = "12.5,23.6,34.7\n"
#         conn.sendall(data.encode('utf-8'))
#         print("[Python] Data sent.")




# Python Client 
import socket

HOST = '127.0.0.1'  # Change to the server's IP if not on the same machine
PORT = 12345        # Must match the C++ server's PORT

def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        print("[Python] Connected to server.")

        try:
            while True:
                data = s.recv(4096)  # You can adjust buffer size if needed
                if not data:
                    break
                message = data.decode('utf-8').strip()
                print("[Python] Received:", message)

                # If desired: convert CSV string to float list
                float_values = list(map(float, message.split(',')))
                print("[Python] Parsed floats:", float_values)

        except KeyboardInterrupt:
            print("\n[Python] Connection closed by user.")

if __name__ == '__main__':
    main()
