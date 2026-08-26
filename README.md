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


## UI adjustment
- Restored readable original 5x7 top status font
- Uptime remains without `UP`
- GRID rows remain 5 px from left edge
- GRID rows moved lower for more spacing below status line


## Status bar layout
- Uptime aligned left
- Antenna icon + signal percent centered
- Battery icon + battery percent aligned right
- Original readable 5x7 font retained


## LR test build
- ESP-NOW A and B use `WIFI_PROTOCOL_LR` only.
- B OLED shows raw RSSI in dBm for range testing.
- Signal icon stays visible; lost link shows `- -`.
- Both A and B must be flashed with this build for the LR test.


## 3x SCT preparation

A / SENSOR kodas paruoštas trims SCT-013-030:

- L1: D0 / GPIO1 / ADC1_CH0
- L2: D1 / GPIO2 / ADC1_CH1
- L3: D2 / GPIO3 / ADC1_CH2

Kol L2/L3 fiziškai neprijungti:

```c
#define SCT_L1_ENABLED 1
#define SCT_L2_ENABLED 0
#define SCT_L3_ENABLED 0
```

Kai prijungsi L2 ir L3 analogines grandines:

```c
#define SCT_L1_ENABLED 1
#define SCT_L2_ENABLED 1
#define SCT_L3_ENABLED 1
```

Kiekvienai fazei yra atskiras kalibracijos faktorius:

```c
#define SCT_L1_CALIBRATION_FACTOR 0.900f
#define SCT_L2_CALIBRATION_FACTOR 0.900f
#define SCT_L3_CALIBRATION_FACTOR 0.900f
```

L2 ir L3 pradžioje naudoja 0.900f tik kaip startinę reikšmę. Kiekvieną fizinį SCT vėliau kalibruosime atskirai.

Protokolas pakeistas į VERSION 3 ir jau siunčia L1/L2/L3 duomenis, todėl perėjus prie šios versijos reikia perflashinti abu A ir B.
