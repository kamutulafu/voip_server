#!/usr/bin/env python3
import socket
import subprocess
import sys
import os
import signal
import time
import threading

current_voip_status = 0

def kill_existing_voip_processes():
    print("[*] Cleaning up old voipcloud_demo instances...")
    subprocess.run(["pkill", "-9", "-f", "voipcloud_demo"], stderr=subprocess.DEVNULL)
    subprocess.run(["pkill", "-9", "-f", "run_test_server.sh"], stderr=subprocess.DEVNULL)

def stdout_reader(proc):
    global current_voip_status
    current_voip_status = 0
    try:
        for line in iter(proc.stdout.readline, b''):
            line_str = line.decode('utf-8', errors='ignore').strip()
            if line_str:
                print(f"[VOIP] {line_str}")
                if line_str.startswith("voip_status ="):
                    try:
                        current_voip_status = int(line_str.split("=")[1].strip())
                    except:
                        pass
    except Exception as e:
        print(f"[VOIP Reader Error] {e}")

def main():
    HOST = '0.0.0.0'
    PORT = 9001

    kill_existing_voip_processes()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        server.bind((HOST, PORT))
    except Exception as e:
        print(f"[-] Failed to bind to {HOST}:{PORT}: {e}")
        sys.exit(1)

    server.listen(5)
    print(f"[*] Automation listener started. Listening on {HOST}:{PORT}...")

    print("[*] Starting media transcoder on port 8081...")
    transcoder_proc = subprocess.Popen(["python3", "-u", "media_recv_server.py"])

    voip_proc = None

    try:
        while True:
            conn, addr = server.accept()
            conn.settimeout(5.0)
            print(f"\n[+] Connection from {addr}")
            try:
                data_bytes = conn.recv(2048)
                if not data_bytes:
                    continue

                data = data_bytes.decode('utf-8', errors='ignore')
                
                token = None
                payload = "payload"

                if "HTTP/" in data:
                    if "GET /status" in data:
                        global current_voip_status
                        response = f"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n{current_voip_status}"
                        conn.sendall(response.encode())
                        conn.close()
                        continue
                        
                    for line in data.split('\r\n'):
                        if line.lower().startswith("x-voip-token:"):
                            token = line.split(":", 1)[1].strip()
                        elif line.lower().startswith("x-voip-payload:"):
                            payload = line.split(":", 1)[1].strip()
                    
                    response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK"
                    conn.sendall(response.encode())
                else:
                    data_clean = data.strip()
                    if "|" in data_clean:
                        token, payload = data_clean.split("|", 1)
                    else:
                        token = data_clean

                if not token or len(token) < 15 or token.startswith("GET") or token.startswith("POST"):
                    print(f"[-] Invalid token format. Ignoring.")
                    continue

                print(f"[+] 成功获取 Token: {token}")

                if voip_proc and voip_proc.poll() is None:
                    print("[*] Stopping previous VoIP session...")
                    kill_existing_voip_processes()
                    time.sleep(0.5)

                print("[*] Starting new VoIP cloud server session...")
                voip_proc = subprocess.Popen(["./run_test_server.sh", token, payload], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                print(f"[+] Server started (PID: {voip_proc.pid})")
                
                threading.Thread(target=stdout_reader, args=(voip_proc,), daemon=True).start()

            except Exception as e:
                print(f"[-] Error processing connection: {e}")
            finally:
                conn.close()
    except KeyboardInterrupt:
        print("\n[*] Exiting automation listener...")
    finally:
        server.close()
        kill_existing_voip_processes()
        try:
            transcoder_proc.terminate()
            transcoder_proc.wait()
        except:
            pass

if __name__ == "__main__":
    main()
