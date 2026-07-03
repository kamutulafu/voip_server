import socket
import struct
import threading
import subprocess
import time
import os

def handle_client(conn, addr):
    print(f"[+] 客户端 {addr} 已连接，开始接收媒体流...")
    audio_file = open("record.pcm", "wb")
    video_file = open("record.mjpeg", "wb")
    
    # 启动 ffplay 进程，通过管道接收数据实时播放
    audio_player = None
    video_player = None
    
    # 查找本地的 ffplay.exe
    ffplay_cmd = 'ffplay'
    for root, dirs, files in os.walk(os.path.join(os.path.dirname(__file__), 'ffmpeg')):
        if 'ffplay.exe' in files:
            ffplay_cmd = os.path.join(root, 'ffplay.exe')
            break

    try:
        # 音频：16kHz 16bit 单声道 PCM
        audio_player = subprocess.Popen([ffplay_cmd, '-f', 's16le', '-ar', '16000', '-ac', '1', '-nodisp', '-i', '-'], stdin=subprocess.PIPE, stderr=subprocess.DEVNULL)
        # 视频：ESP32 端已用硬件 JPEG 编码器把每帧压成 JPEG 再发送（type==1 的 payload 即一张完整 JPEG）。
        # 用 mjpeg 解复用即可，无需再指定分辨率/像素格式。低延迟参数减少播放器缓冲。
        video_player = subprocess.Popen(
            [ffplay_cmd, '-f', 'mjpeg', '-flags', 'low_delay', '-fflags', 'nobuffer',
             '-framedrop', '-i', '-'],
            stdin=subprocess.PIPE, stderr=subprocess.DEVNULL)
        print("[+] ffplay 播放器已启动！")
    except Exception as e:
        print(f"[-] 启动 ffplay 失败，请确保系统已安装 ffmpeg 并添加到环境变量。错误: {e}")

    first_audio = True
    first_video = True
    try:
        while True:
            # 读取 5 字节的头部 (1 字节 type + 4 字节 length, 小端序)
            header = conn.recv(5)
            if not header or len(header) < 5:
                break
            
            media_type, payload_len = struct.unpack('<BI', header)
            
            # 读取 payload
            payload = b""
            while len(payload) < payload_len:
                chunk = conn.recv(payload_len - len(payload))
                if not chunk:
                    break
                payload += chunk
                
            if len(payload) != payload_len:
                break
                
            if media_type == 0:
                if first_audio:
                    print("[+] 成功收到第一帧【音频】数据！")
                    first_audio = False
                audio_file.write(payload)
                if audio_player and audio_player.poll() is None:
                    try:
                        audio_player.stdin.write(payload)
                        audio_player.stdin.flush()
                    except:
                        pass
            elif media_type == 1:
                if first_video:
                    print("[+] 成功收到第一帧【视频】数据！")
                    first_video = False
                video_file.write(payload)
                if video_player and video_player.poll() is None:
                    try:
                        video_player.stdin.write(payload)
                        video_player.stdin.flush()
                    except:
                        pass
            else:
                print(f"未知类型: {media_type}")
                
    except Exception as e:
        print(f"[-] 连接异常: {e}")
    finally:
        print(f"[-] 客户端 {addr} 已断开。")
        audio_file.close()
        video_file.close()
        if audio_player:
            audio_player.terminate()
        if video_player:
            video_player.terminate()
        conn.close()

def main():
    HOST = '0.0.0.0'
    PORT = 8081
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.bind((HOST, PORT))
    server.listen(5)
    print(f"[*] 本地媒体测试服务器启动，正在监听 {HOST}:{PORT}")
    print("[*] 收到的数据除保存外，将通过 ffplay 实时播放。")
    
    server.settimeout(1.0) # 设置超时，使 accept 非阻塞以响应 Ctrl+C
    try:
        while True:
            try:
                conn, addr = server.accept()
                t = threading.Thread(target=handle_client, args=(conn, addr))
                t.daemon = True # 设置为守护线程
                t.start()
            except socket.timeout:
                continue
    except KeyboardInterrupt:
        print("\n[*] 接收到 Ctrl+C，正在关闭服务端...")
    finally:
        server.close()

if __name__ == "__main__":
    main()
