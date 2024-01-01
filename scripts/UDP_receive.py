import socket

UDP_IP = "192.168.0.9"
UDP_PORT = 13869

# Create a UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

while True:
  data, addr = sock.recvfrom(1024)
  print(data.decode('utf-8'), end="")