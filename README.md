# OC — самый первый шаг к своей ОС

Это минимальный учебный старт: C++ программа собирает загрузочный BIOS-образ (`build/os.img`) без NASM/GRUB/кросс-компилятора. Образ можно запустить в QEMU из UCRT64.

Пока это не полноценное ядро, а базовый boot sector: он загружается BIOS-ом, очищает экран и печатает приветствие. Это самый простой фундамент, от которого можно идти к загрузчику, protected mode и настоящему C++ kernel.

## Требования

- `g++` из UCRT64/MinGW или другой C++17 компилятор
- `make` (опционально, но удобно)
- `qemu-system-i386`

## Быстрый запуск в UCRT64

```sh
make run
```

Если `make` недоступен, выполните команды вручную:

```sh
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -pedantic src/build_image.cpp -o build/build_image
./build/build_image build/os.img
qemu-system-i386 -drive format=raw,file=build/os.img
```

На экране QEMU должно появиться сообщение:

```text
OC booted!
Hello from a tiny C++-built OS image.
Next step: load a real kernel.
```

## Что здесь происходит

`src/build_image.cpp` создаёт ровно 512 байт загрузочного сектора. BIOS загружает эти 512 байт по адресу `0x7C00`, проверяет сигнатуру `0x55AA` в конце сектора и передаёт управление коду. Код использует BIOS interrupt `0x10`, чтобы вывести текст на экран.

## Следующие шаги

1. Разделить проект на bootloader и kernel.
2. Добавить загрузку дополнительных секторов с диска.
3. Перейти из real mode в protected mode.
4. Скомпилировать freestanding C++ kernel и передавать ему управление.
