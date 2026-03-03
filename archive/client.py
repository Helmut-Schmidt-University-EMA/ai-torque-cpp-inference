import requests

# regular call 
# url = "http://localhost:8080/predict"
# input_data = {"data": [0.1] * 135}
# response = requests.post(url, json=input_data)

# print("Status:", response.status_code)
# print("Raw response text:")
# print(response.text)


# streaming 
# url = "http://10.246.57.218:8080/stream"

# with requests.get(url, stream=True) as r:
#     for line in r.iter_lines():
#         if line:
#             print(line.decode())



# websocket
# import websocket
# import json

# ws = websocket.WebSocket()
# ws.connect("ws://localhost:8080/ws")

# # Send random input vector
# ws.send(json.dumps({"data": [0.1] * 135}))

# # Receive result
# response = ws.recv()
# print(response)

# # Keep listening for more messages
# while True:
#     msg = ws.recv()
#     print(msg)



# websocket demo
import websocket
import json

def main():
    ws_url = "ws://localhost:8080/ws2"
    try:
        ws = websocket.WebSocket()
        ws.connect(ws_url)
        print(f"Connected to {ws_url}")

        while True:
            try:
                msg = ws.recv()
                data = json.loads(msg)
                
                # Pretty-print the output
                if "output" in data:
                    output_str = ", ".join(f"{x:.6f}" for x in data["output"])
                    print(f"Prediction: [{output_str}]")
                else:
                    print(f"Server message: {data}")

            except json.JSONDecodeError:
                print(f"Received non-JSON message: {msg}")
            except websocket.WebSocketConnectionClosedException:
                print("WebSocket connection closed by server")
                break
            except Exception as e:
                print(f"Error: {e}")
                break

    except Exception as e:
        print(f"Failed to connect to WebSocket: {e}")

    finally:
        try:
            ws.close()
        except:
            pass
        print("Client exited.")

if __name__ == "__main__":
    main()
