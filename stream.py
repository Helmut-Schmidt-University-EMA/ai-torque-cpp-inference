import socket

HOST = 'localhost'
PORT = 12345

# Set up server
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
    server.bind((HOST, PORT))
    server.listen(1)
    print(f"[Python] Server listening on {HOST}:{PORT}...")
    conn, addr = server.accept()
    with conn:
        print(f"[Python] Connection from {addr}")
        data = "12.5,23.6,34.7\n"
        conn.sendall(data.encode('utf-8'))
        print("[Python] Data sent.")
