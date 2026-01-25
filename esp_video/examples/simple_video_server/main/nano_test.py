import cv2
import socket
import numpy as np
import struct

# TCP Server Setup
TCP_IP = "0.0.0.0" # Listen on all interfaces
TCP_PORT = 5000

server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
# Allow Python to reuse the port immediately if you restart the script
server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

try:
    server_sock.bind((TCP_IP, TCP_PORT))
    server_sock.listen(1) # Allow 1 connection
    print(f"TCP Server listening on {TCP_IP}:{TCP_PORT}...")
    print("Waiting for ESP32 to connect...")
except Exception as e:
    print(f"Error binding server: {e}")
    exit()

def recvall(sock, count):
    """
    Helper function: TCP is a stream, so 'recv(4)' isn't guaranteed 
    to return 4 bytes. This loop ensures we get exactly what we asked for.
    """
    buf = b''
    while count:
        newbuf = sock.recv(count)
        if not newbuf: return None
        buf += newbuf
        count -= len(newbuf)
    return buf

while True:
    # 1. Accept Connection
    conn, addr = server_sock.accept()
    print(f"Connection established from: {addr}")
    
    try:
        while True:
            # 2. Read Header (4 bytes) to get Image Size
            # We use 'recvall' to ensure we get all 4 bytes
            length_data = recvall(conn, 4)
            if not length_data: break # Connection closed by ESP32
            
            # Unpack 4 bytes into an integer (Little Endian '<I')
            (length,) = struct.unpack('<I', length_data)
            
            # 3. Read Body (Exact number of bytes based on header)
            image_data = recvall(conn, length)
            if not image_data: break

            # 4. Decode
            nparr = np.frombuffer(image_data, np.uint8)
            frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

            if frame is not None:
                cv2.imshow("ESP32 TCP Stream", frame)
            else:
                print("Failed to decode frame")
            
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break
    except Exception as e:
        print(f"Error: {e}")
    finally:
        conn.close()
        print("Connection closed. Waiting for new connection...")

server_sock.close()
cv2.destroyAllWindows()