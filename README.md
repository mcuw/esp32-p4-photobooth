# DIY Photobooth with WiFI/ IPP protocol

Make your own DIY Photobooth - privacy first, affordable and easy to setup.

## Prerequisites

You can use these links to buy a developer board. If you want to support this project then use the affiliate links marked with a *. For you it does not cost more.

ESP32-P4
- e.g. [GUITION 10.1" ESP32-P4 LCD Display Development Board](https://s.click.aliexpress.com/e/_c2JX4b3b)* with 1280x800 Capacitive Touch Screen, Wi-Fi 6, battery and speaker

espressif SDK
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/linux-setup.html) v6.1 target: esp32p4

Printer:

- [Canon Compact & Portable](https://www.usa.canon.com/shop/printers/compact-portable-printers) (not tested)
- [Canon SELPHY QX20/ CD1500](https://www.usa.canon.com/shop/printers?facet_printers_printer_series=635) (not tested)
- [Canon SELPHY CP1300](https://www.usa.canon.com/support/p/selphy-cp1300) (tested)

## Get Started

setup IDF for the ESP32-P4
```sh
    idf.py set-target esp32p4
```
configure your printer IP address
```sh
    idf.py menuconfig     # Photobooth: WLAN; IPP Client: Printer-IP (example 192.168.0.200)
```
build and flash the firmware to your ESP32-P4
```sh
    idf.py build flash monitor
```
parameter `monitor` is optional to open the serial output after the flash

main/test.jpg is only a test image placeholder -> you can replace it with a real baseline-JPEG
(Postcard: 1181x1748 px with 300 dpi) and activate "Beim Start test.jpg drucken" with menuconfig.
