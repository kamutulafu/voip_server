#!/usr/bin/env python3
import socket
import sys
import threading
import time
_stats = {'a':0,'ab':0,'v':0,'vb':0,'vmax':0,'vdrop':0,'t':time.time()}
def _maybe_stats():
    now = time.time()
    if now - _stats['t'] >= 2.0:
        print("[STATS] audio %dpkt/%dB | video %dnal/%dB maxNAL=%dB drop=%d" % (_stats['a'], _stats['ab'], _stats['v'], _stats['vb'], _stats['vmax'], _stats['vdrop']), flush=True)
        _stats['t'] = now
audio_out_buffer = bytearray()

def _fwd_audio(udp_sock, payload, addr):
    global audio_out_buffer
    audio_out_buffer.extend(payload)
    
    # WeChat SDK 16000Hz mono requires exactly 320 frames (640 bytes) per listener->data() call
    while len(audio_out_buffer) >= 640:
        chunk = audio_out_buffer[:640]
        del audio_out_buffer[:640]
        try:
            udp_sock.sendto(chunk, addr)
            _stats['a'] += 1; _stats['ab'] += 640
        except Exception as e:
            print("[-] audio UDP send fail: %s" % e, flush=True)
    _maybe_stats()
def _fwd_video(tcp_sock, nal):
    n = len(nal)
    if n == 0 or tcp_sock is None:
        return
    try:
        # Send 4-byte length header followed by NAL
        tcp_sock.sendall(n.to_bytes(4, 'little') + nal)
        _stats['v'] += 1; _stats['vb'] += n
        if n > _stats['vmax']: _stats['vmax'] = n
    except Exception as e:
        print("[-] video TCP send fail %dB: %s" % (n, e), flush=True)
    _maybe_stats()


def handle_client(conn, addr, udp_sock, audio_udp_addr):
    print(f"[+] Client connected from {addr}")
    video_buffer = bytearray()
    first_audio = True
    first_video = True
    
    # Establish TCP connection to local C server for video
    video_tcp_sock = None
    try:
        video_tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        video_tcp_sock.connect(('127.0.0.1', 9002))
    except Exception as e:
        print(f"[-] Could not connect to local video TCP 9002 (C server might not be running yet): {e}")
        video_tcp_sock = None
        
    try:
        # Peek at the first 5 bytes to determine if it's HTTP or raw TCP
        peek_bytes = conn.recv(5, socket.MSG_PEEK)
        if not peek_bytes:
            return
        
        is_http = peek_bytes.startswith(b"POST") or peek_bytes.startswith(b"GET")
        
        if is_http:
            # 1. Read HTTP headers up to b"\r\n\r\n"
            header_data = b""
            while b"\r\n\r\n" not in header_data:
                chunk = conn.recv(1)
                if not chunk:
                    return
                header_data += chunk
            
            buffer = bytearray()
            while True:
                # Read chunk size line
                line = b""
                while b"\r\n" not in line:
                    chunk = conn.recv(1)
                    if not chunk:
                        break
                    line += chunk
                if not line:
                    break
                
                size_str = line.strip().decode('ascii', errors='ignore')
                if not size_str:
                    continue
                try:
                    size = int(size_str, 16)
                except ValueError:
                    print(f"[-] Invalid chunk size line: {line}")
                    break
                
                if size == 0:
                    # End of chunks
                    # Read final \r\n
                    conn.recv(2)
                    break
                
                # Read 'size' bytes
                chunk_data = bytearray()
                while len(chunk_data) < size:
                    chunk = conn.recv(size - len(chunk_data))
                    if not chunk:
                        break
                    chunk_data.extend(chunk)
                
                # Read trailing \r\n
                conn.recv(2)
                
                # Feed chunk data into video_buffer/process it
                buffer.extend(chunk_data)
                
                # Parse packets from buffer
                while len(buffer) >= 5:
                    m_type = buffer[0]
                    m_len = int.from_bytes(buffer[1:5], byteorder='little')
                    if len(buffer) < 5 + m_len:
                        break
                    
                    payload = buffer[5:5+m_len]
                    
                    # Process payload
                    if m_type == 0:
                        if first_audio:
                            print("[+] Successfully received first audio packet via HTTP!")
                            first_audio = False
                        try:
                            _fwd_audio(udp_sock, payload, audio_udp_addr)
                        except Exception as e:
                            print(f"[-] Failed to send audio UDP: {e}")
                    elif m_type == 1:
                        if first_video:
                            print("[+] Successfully received first video packet via HTTP!")
                            first_video = False
                        if len(payload) > 0:
                            try:
                                _fwd_video(video_tcp_sock, payload)
                            except Exception as e:
                                pass
                    
                    del buffer[:5+m_len]
            
            # Send HTTP 200 OK response
            try:
                resp = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK"
                conn.sendall(resp.encode())
            except:
                pass
                
        else:
            # Raw TCP stream
            while True:
                # Read 5-byte header
                header = bytearray()
                while len(header) < 5:
                    chunk = conn.recv(5 - len(header))
                    if not chunk:
                        break
                    header.extend(chunk)
                
                if len(header) < 5:
                    break
                
                m_type = header[0]
                m_len = int.from_bytes(header[1:5], byteorder='little')
                
                payload = bytearray()
                while len(payload) < m_len:
                    chunk = conn.recv(m_len - len(payload))
                    if not chunk:
                        break
                    payload.extend(chunk)
                
                if len(payload) < m_len:
                    break
                
                if m_type == 0:
                    if first_audio:
                        print("[+] Successfully received first audio packet via raw TCP!")
                        first_audio = False
                    try:
                        _fwd_audio(udp_sock, payload, audio_udp_addr)
                    except Exception as e:
                        print(f"[-] Failed to send raw audio UDP: {e}")
                elif m_type == 1:
                    if first_video:
                        print("[+] Successfully received first video packet via raw TCP!")
                        first_video = False
                    
                    if len(payload) > 0:
                        try:
                            _fwd_video(video_tcp_sock, payload)
                        except Exception as e:
                            pass
                        
    except Exception as e:
        print(f"[-] Error handling client: {e}")
    finally:
        conn.close()
        # Send remaining video buffer
        if len(video_buffer) > 0:
            try:
                _fwd_video(video_tcp_sock, video_buffer)
            except:
                pass
        if video_tcp_sock:
            video_tcp_sock.close()
        print(f"[-] Client {addr} disconnected.")

def main():
    # TCP server for ESP32
    tcp_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        tcp_server.bind(('0.0.0.0', 8081))
    except Exception as e:
        print(f"[-] Failed to bind TCP server on 8081: {e}")
        sys.exit(1)
    
    tcp_server.listen(5)
    print("[*] Media receiver server listening on port 8081 (Dual Mode TCP/HTTP)...")

    # UDP socket for sending to C program (voipcloud_demo)
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    audio_udp_addr = ('127.0.0.1', 9003)

    try:
        while True:
            conn, addr = tcp_server.accept()
            # Handle client in a thread to allow multiple concurrent requests or keep-alive
            t = threading.Thread(target=handle_client, args=(conn, addr, udp_sock, audio_udp_addr))
            t.daemon = True
            t.start()
    except KeyboardInterrupt:
        pass
    finally:
        tcp_server.close()

if __name__ == "__main__":
    main()
