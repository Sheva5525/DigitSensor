import serial
from ymodem.Socket import ModemSocket
from ymodem.Protocol import ProtocolType

def send_file_ymodem(port_name, baud_rate, file_path):
    ser = serial.Serial(
        port=port_name,
        baudrate=baud_rate,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=1
    )

    socket = ModemSocket(
        read=lambda size, timeout=1: ser.read(size),
        write=lambda data, timeout=1: ser.write(data),
        protocol_type=ProtocolType.YMODEM
    )

    print(f"Отправка файла {file_path} через {port_name}...")
    
    success = socket.send([file_path])
    
    if success:
        print("Файл успешно отправлен!")
    else:
        print("Ошибка при передаче файла.")

    ser.close()

if __name__ == "__main__":
    send_file_ymodem(port_name="COM5", baud_rate=115200, file_path="script.amx")
    