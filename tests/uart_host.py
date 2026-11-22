#!/usr/bin/env python3
"""Build and run tests/uart_host.c: src/driver/uart.c's receive path against a model of the JieLi UART, on the PC.

    python tests/uart_host.py

Needs a host gcc (MSYS2 mingw64 on Windows: put C:\\msys64\\mingw64\\bin on PATH).
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    out = os.path.join(ROOT, 'build', 'host')
    os.makedirs(out, exist_ok=True)
    exe = os.path.join(out, 'uart_host.exe' if os.name == 'nt' else 'uart_host')
    subprocess.run(['gcc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-DUART_HOST_TEST', '-I', os.path.join(ROOT, 'src'),
                    os.path.join(ROOT, 'tests', 'uart_host.c'), os.path.join(ROOT, 'src', 'driver', 'uart.c'), '-o', exe],
                   check=True)
    return subprocess.run([exe]).returncode


if __name__ == '__main__':
    sys.exit(main())
