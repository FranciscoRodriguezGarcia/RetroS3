import socket
import serial

SERIAL_PORT = "COM7"
BAUD_RATE = 115200

HOST = "0.0.0.0"
TCP_PORT = 5555

HEADER = 0xA5

def main():
    with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1) as esp:
        print(f"ESP32 conectada en {SERIAL_PORT}")

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind((HOST, TCP_PORT))
            server.listen(1)

            print(f"Esperando Android en TCP {TCP_PORT}...")

            while True:
                connection, address = server.accept()

                with connection:
                    print(f"Android conectado: {address}")

                    buffer = bytearray()
                    last_mask = None

                    try:
                        while True:
                            data = connection.recv(1024)

                            if not data:
                                break

                            buffer.extend(data)

                            # Cada paquete ocupa dos bytes:
                            # [0xA5, mask]
                            while len(buffer) >= 2:
                                if buffer[0] != HEADER:
                                    del buffer[0]
                                    continue

                                mask = buffer[1]
                                del buffer[:2]

                                esp.write(bytes([HEADER, mask]))

                                if mask != last_mask:
                                    print(f"Botones: 0x{mask:02X}")
                                    last_mask = mask

                    except (ConnectionError, OSError) as e:
                        print(f"Conexión interrumpida: {e}")

                    finally:
                        # Liberar botones al desconectarse.
                        esp.write(bytes([HEADER, 0x00]))
                        print("Android desconectado")

if __name__ == "__main__":
    main()