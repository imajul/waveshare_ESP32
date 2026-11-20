# Firmware de prueba – Waveshare ESP32-S3-Touch-LCD-4.3B

Interfaz táctil (LVGL 8.4, ESP-IDF 5.4.2) con una pestaña por periférico:

| Pestaña   | Qué prueba |
|-----------|------------|
| Sistema   | Chip, flash, PSRAM, RAM libre, MAC, escaneo del bus I2C |
| Táctil    | Dibujo libre, coordenadas y número de dedos (GT911) |
| Pantalla  | Colores sólidos, barras y degradados, apagado de retroiluminación |
| WiFi      | Búsqueda de redes 2,4 GHz |
| Reloj     | RTC PCF85063A (lectura y ajuste a la hora de compilación) |
| microSD   | Montaje FAT32, escritura/lectura de 1 MB con verificación y velocidad |
| E/S       | Salidas DO0/DO1 y entradas DI0/DI1 aisladas (CH422G) |
| RS485     | Envío de saludo, recepción, eco, velocidad seleccionable |
| CAN       | 125k–1M, modo sin ACK, envío de trama y registro de recepción |

## Grabar sin instalar nada

1. Abre https://espressif.github.io/esptool-js/ en Chrome o Edge.
2. Conecta la placa por USB-C y pulsa **Connect** (si no aparece: BOOT pulsado + RESET, soltar BOOT).
3. Dirección `0x0`, archivo `firmware-prueba-4.3B.bin`, pulsa **Program**.
4. Al terminar, pulsa RESET en la placa.

## Pines usados (de los ejemplos oficiales de Waveshare)

- LCD RGB565: HSYNC 46, VSYNC 3, DE 5, PCLK 7; datos 14,38,18,17,10,39,0,45,48,47,21,1,2,42,41,40
- I2C: SDA 8, SCL 9 — CH422G, GT911 (0x5D, INT 4), PCF85063A (0x51)
- CH422G: EXIO0=DI0, EXIO1=reset táctil, EXIO2=retroiluminación, EXIO3=reset LCD, EXIO4=CS SD, EXIO5=DI1, OC0/OC1=DO0/DO1
- RS485: TX 44, RX 43 · CAN: TX 15, RX 16 · SD (SPI): MOSI 11, CLK 12, MISO 13
- Consola de logs: USB nativo (USB Serial/JTAG), 115200

## Compilar

```bash
idf.py set-target esp32s3 && idf.py build && idf.py merge-bin
```
