# ESP32 Current Monitor

Vienas ESP-IDF projektas dviem Seeed Studio XIAO ESP32-S3.

## B / DISPLAY atnaujinimas

CH1115 128x64 OLED rodo:

- `UP` uptime
- `Bxx%` B plokštės baterijos procentą
- `Sxx%` ESP-NOW RSSI paverstą į 0-100 %
- `OFF` / `OFFLINE`, jei 3 s negaunamas paketas

Mygtuku perjungiami du vaizdai:

- GRID: L1/L2/L3 su A ir kVA
- GENERATOR: didelis bendras A ir kVA

Kol A turi tik vieną SCT, B rodo L1 realų matavimą, L2/L3 = 0.00. Generator mode bendras matavimas todėl kol kas lygus L1. Trijų fazių protokolą pridėsime sekančiame A etape.

## B pinai

- BAT ADC: D1 / GPIO2
- MODE mygtukas: D2 / GPIO3
- OLED SDA: D4 / GPIO5
- OLED SCL: D5 / GPIO6

Baterijos daliklis:

`BAT+ -> 10k -> GPIO2 -> 10k -> GND`

Papildomai galima 100 nF (`104`) nuo GPIO2 į GND.

Mygtukas:

`GPIO3 -> mygtukas -> GND`

Naudojamas vidinis pull-up.

## Flash B

```bash
rm -rf build/display
idf.py --preset display build
idf.py --preset display -p /dev/cu.usbmodem14101 flash monitor
```

A šiam B atnaujinimui perflashinti nereikia: ESP-NOW paketo formatas lieka protocol v2.
