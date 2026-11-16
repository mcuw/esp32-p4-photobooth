# photobooth_ipp
ESP-IDF v6.1, Ziel: esp32p4

    idf.py set-target esp32p4
    idf.py menuconfig     # Photobooth: WLAN; IPP Client: Drucker-IP (Standard 192.168.0.200)
    idf.py build flash monitor

main/test.jpg ist nur ein Platzhalter -> durch echtes Baseline-JPEG ersetzen
(Postkarte: 1181x1748 px bei 300 dpi) und "Beim Start test.jpg drucken" aktivieren.
