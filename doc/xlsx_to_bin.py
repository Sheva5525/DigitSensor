#!/usr/bin/env python3
"""
Преобразует calib.xlsx в calib.bin для STM32.

Формат бинарника (little-endian, 2056 байт):
    float out1[256]   (1024 B)
    float out2[256]   (1024 B)
    int32 ohm1        (4 B)
    int32 ohm2        (4 B)

Раскладка Excel (лист "calib"):
    B1        -> ohm1  (int)
    B2        -> ohm2  (int)
    B5:B260   -> out1[0..255]  (float)
    C5:C260   -> out2[0..255]  (float)

Использование:
    python xlsx_to_bin.py calib.xlsx calib.bin
"""
import sys
import struct
import openpyxl


POINTS     = 256
ROW_FIRST  = 5      # первая строка с данными
COL_OUT1   = 2      # B
COL_OUT2   = 3      # C


def main():
    if len(sys.argv) != 3:
        print("Использование: python xlsx_to_bin.py <calib.xlsx> <calib.bin>")
        sys.exit(1)

    src, dst = sys.argv[1], sys.argv[2]

    wb = openpyxl.load_workbook(src, data_only=True)
    if "Sheet1" not in wb.sheetnames:
        print(f"FAIL: нет листа 'Sheet1' в {src}")
        sys.exit(1)

    ws = wb["Sheet1"]

    def get_num(cell):
        v = ws[cell].value
        if v is None:
            print(f"FAIL: пустая ячейка {cell}")
            sys.exit(1)
        return v

    ohm1 = int(get_num("B1"))
    ohm2 = int(get_num("B2"))

    out1 = []
    out2 = []
    for i in range(POINTS):
        r = ROW_FIRST + i
        v1 = ws.cell(row=r, column=COL_OUT1).value
        v2 = ws.cell(row=r, column=COL_OUT2).value
        if v1 is None or v2 is None:
            print(f"FAIL: пустая ячейка в строке {r}")
            sys.exit(1)
        out1.append(float(v1))
        out2.append(float(v2))

    blob = struct.pack("<256f256fii", *out1, *out2, ohm1, ohm2)
    assert len(blob) == 2056

    with open(dst, "wb") as f:
        f.write(blob)

    print(f"OK: {dst}  {len(blob)} байт")
    print(f"    ohm1={ohm1}  ohm2={ohm2}")
    print(f"    out1[0]={out1[0]}  out1[255]={out1[255]}")
    print(f"    out2[0]={out2[0]}  out2[255]={out2[255]}")


if __name__ == "__main__":
    main()