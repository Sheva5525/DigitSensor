import os
import struct
import time
import serial

ACK = b"\x06"
NAK = b"\x15"
START_SIGNAL = b"S"  # Символ готовности от МК


def send_file_with_handshake(port, baudrate, file_path):
    if not os.path.exists(file_path):
        print(f"Ошибка: Файл '{file_path}' не найден.")
        return

    file_size = os.path.getsize(file_path)
    print(f"Файл: {file_path} ({file_size} байт)")

    try:
        ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=10,  # Ждем ответа от МК до 10 секунд
        )
    except Exception as e:
        print(f"Не удалось открыть порт {port}: {e}")
        return

    ser.reset_input_buffer()
    ser.reset_output_buffer()

    print("Ожидание сигнала готовности 'S' от микроконтроллера...")
    print(
        "(Перезагрузите плату Black Pill, если она уже проскочила этот момент)"
    )

    # Цикл ожидания стартового символа от МК
    while True:
        ch = ser.read(1)
        if not ch:
            print("[ОШИБКА] МК так и не прислал сигнал готовности 'S' (таймаут).")
            ser.close()
            return
        if ch == START_SIGNAL:
            print("Сигнал 'S' получен! МК готов.")
            break

    # Формируем 4 байта длины
    len_bytes = struct.pack("<I", file_size)

    # Отправляем длину и сразу за ней данные файла одним пакетом
    print("Отправка длины файла...")
    ser.write(len_bytes)

    print("Отправка содержимого файла...")
    with open(file_path, "rb") as f:
        ser.write(f.read())
    ser.flush()

    print("Данные ушли. Ожидание финального ACK/NAK...")
    response = ser.read(1)
    ser.close()

    if response == ACK:
        print("[УСПЕХ] Файл успешно принят микроконтроллером!")
    elif response == NAK:
        print("[ОШИБКА] МК вернул NAK. Проверьте MAX_FILE_SIZE на плате.")
    else:
        print(f"[ОШИБКА] Неизвестный ответ от МК: {response}")


if __name__ == "__main__":
    send_file_with_handshake("COM5", 115200, "script.amx")
