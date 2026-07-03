#!/usr/bin/env python3
import socket
import subprocess
import sys

def main():
    HOST = '0.0.0.0'
    PORT = 9001

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        server.bind((HOST, PORT))
    except Exception as e:
        print(f"[-] Failed to bind to {HOST}:{PORT}: {e}")
        sys.exit(1)

    server.listen(5)
    print(f"[*] Automation listener started. Listening on {HOST}:{PORT}...")
    print("[*] Waiting for server_token from ESP32...")

    # Start the media receiver script in the background
    print("[*] Starting media transcoder on port 8081...")
    transcoder_proc = subprocess.Popen(["python3", "-u", "media_recv_server.py"])

    try:
        while True:
            conn, addr = server.accept()
            print(f"\n[+] Connection from {addr}")
            try:
                # Read data
                data_bytes = conn.recv(2048)
                if not data_bytes:
                    continue

                data = data_bytes.decode('utf-8', errors='ignore')
                print(f"[+] Received raw data:\n{data}")
                
                token = None
                payload = "payload"

                # Check if it is an HTTP request
                if "HTTP/" in data:
                    # Parse HTTP headers
                    for line in data.split('\r\n'):
                        if line.lower().startswith("x-voip-token:"):
                            token = line.split(":", 1)[1].strip()
                        elif line.lower().startswith("x-voip-payload:"):
                            payload = line.split(":", 1)[1].strip()
                    
                    # Respond with HTTP 200 OK to satisfy Nginx/Client
                    response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK"
                    conn.sendall(response.encode())
                else:
                    # Raw TCP data
                    data_clean = data.strip()
                    if "|" in data_clean:
                        token, payload = data_clean.split("|", 1)
                    else:
                        token = data_clean

                # Validate token
                if not token or len(token) < 20 or token.startswith("GET") or token.startswith("POST"):
                    print(f"[-] Invalid token format or scanner detected. Ignoring.")
                    continue

                print(f"[+] Token: {token}")
                print(f"[+] Payload: {payload}")

                # Start the VoIP Server Demo
                print("[*] Starting VoIP cloud server...")
                proc = subprocess.Popen(["./run_test_server.sh", token, payload])
                print(f"[+] Server started (PID: {proc.pid})")

            except Exception as e:
                print(f"[-] Error processing connection: {e}")
            finally:
                conn.close()
    except KeyboardInterrupt:
        print("\n[*] Exiting automation listener...")
    finally:
        server.close()
        try:
            transcoder_proc.terminate()
            transcoder_proc.wait()
        except:
            pass

if __name__ == "__main__":
    main()
