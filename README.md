
---

# 📘 DOKUMENTASI RESMI: VOTOL Smart Dashboard V12 (Polytron Fox-R)

**Deskripsi Proyek:**
VOTOL Smart Dashboard adalah instrumen kokpit tambahan untuk motor listrik dengan *controller* VOTOL (khususnya Polytron Fox-R). Alat ini membaca data spesifik pabrikan (seperti Suhu, RPM, Voltase, dan Arus) langsung dari jalur CAN Bus menggunakan modul MCP2515. Data tersebut divisualisasikan secara elegan melalui layar OLED dengan font modern.

Alat ini dilengkapi dengan modul RTC (DS3231) untuk jam *offline*, fitur *auto-sync* waktu via WiFi (NTP), dan perlindungan algoritma canggih agar pengereman regeneratif (*Regen Braking*) tidak disalahartikan sebagai proses pengecasan.

---

## 🛠️ 1. Alat dan Bahan Utama

1. **Mikrokontroler:** ESP32-C3 Super Mini (Prosesor ringkas, WiFi tertanam).
2. **Modul Pembaca CAN:** MCP2515 (Transceiver SPI). *Catatan Penting: Modul harus menggunakan crystal 8MHz. Jika tertulis 16.000 pada crystal-nya, kode harus disesuaikan ke `MCP_16MHZ*`.
3. **Layar Display:** OLED SSD1306 ukuran 0.91 inch (128x32 pixel), antarmuka I2C.
4. **Modul Waktu (RTC):** DS3231 (Akurat dengan baterai kancing), antarmuka I2C.
5. **Tombol (Push Button):** 1 buah *tactile switch* untuk navigasi halaman.
6. **Kabel Jumper & Timah Solder:** Secukupnya untuk merakit komponen.
7. **Jumper Cap (120 Ohm):** Wajib dipasang pada terminal resistor modul MCP2515 untuk mencegah *noise* data pada CAN Bus.

---

## ⚡ 2. Skema Wiring (Jalur Kabel)

Karena kita menggunakan ESP32-C3 Super Mini, pemasangan pin dioptimalkan sebagai berikut:

| Komponen | Pin Modul | Sambung ke ESP32-C3 | Keterangan Tambahan |
| --- | --- | --- | --- |
| **OLED & RTC** | **SDA** | **GPIO 8** | Digabung (Paralel) untuk layar OLED & RTC DS3231. |
| *(Jalur I2C)* | **SCL** | **GPIO 9** | Digabung (Paralel) untuk layar OLED & RTC DS3231. |
|  | VCC | 3.3V | Tegangan operasi I2C. |
|  | GND | GND | Gabung ke Ground utama ESP32. |
| **MCP2515** | **CS** | **GPIO 7** | Jalur Chip Select SPI. |
| *(Jalur SPI)* | **SI / MOSI** | **GPIO 6** | Jalur Data Masuk (SPI). |
|  | **SO / MISO** | **GPIO 5** | Jalur Data Keluar (SPI). |
|  | **SCK** | **GPIO 4** | Jalur Clock SPI. |
|  | **INT** | **GPIO 10** | Jalur Interrupt (Penting untuk kecepatan pembacaan tanpa *lag*). |
|  | **VCC** | **5V (VBUS)** | **WAJIB 5V!** IC Transceiver TJA1050 tidak bisa membaca motor jika diberi 3.3V. |
|  | GND | GND | Gabung ke Ground utama dan Ground Motor. |
| **CAN Motor** | **CAN H** | Terminal **H** (MCP2515) | Kabel CAN High dari Motor (Biasanya Oranye/Kuning). |
|  | **CAN L** | Terminal **L** (MCP2515) | Kabel CAN Low dari Motor (Biasanya Hijau/Coklat). |
| **Tombol Nav.** | Kaki 1 | **GPIO 3** | Input tombol ganti halaman. |
|  | Kaki 2 | GND |  |

---

## 📱 3. Antarmuka Layar (UI) & Navigasi

Perangkat ini didesain agar tidak memecah konsentrasi pengendara. Tekan tombol (GPIO 3) untuk memutar siklus 5 Halaman:

* **Halaman 1 (Jam & Tanggal):** Tampilan utama. Menampilkan jam berukuran besar (18pt) di sebelah kiri, dan kolom tanggal di kanan dengan susunan ke bawah: Hari, Tanggal/Bulan, dan Tahun.
* **Halaman 2 (Suhu):** Memantau suhu secara *real-time* untuk ECU (Controller), Motor BLDC, dan BATT (Baterai).
* **Halaman 3 (BMS Data):** Membaca tegangan (VOLT) baterai dan keluaran arus aktual (ARUS / Ampere).
* **Halaman 4 (Power):** Menampilkan konsumsi listrik instan (WATT) dengan angka raksasa dan indikator `+` atau `-`.
* **Halaman 5 (System Info):** Menampilkan SSID WiFi tersimpan, Password, Teks Splash Screen, dan Versi Firmware (V12). Sangat berguna jika Anda lupa nama *hotspot* yang harus disiapkan.

**⭐ FItur Pintar Otomatis (Smart Overlays):**

1. **Auto-Sleep Layar Info:** Jika berada di Halaman 2, 3, 4, atau 5 selama **30 detik** tanpa penekanan tombol, layar akan mengembalikan diri otomatis ke Halaman 1 (Jam).
2. **Pop-up Mode Berkendara:** Setiap kali tuas rem ditekan/dilepas atau gigi diganti (PARK, DRIVE, SPORT, REVERSE), layar akan memunculkan teks tersebut di tengah layar selama **3 detik**.
3. **High-Speed Override:** Jika motor dipacu di atas **70 km/h**, angka kecepatan akan mendominasi layar secara otomatis (font 18pt) dengan teks "KM/H" di kanannya, menimpa halaman apa pun.
4. **Anti-Regen Charging Mode:** Saat dicolok ke *charger* fisik (Ori bawaan maupun Fast Charger CCS), layar akan menampilkan teks "ORI CHARGER" atau "FAST CHARGER". Layar akan bergantian setiap 2 detik antara menampilkan Arus Masuk (Ampere) dan Persentase Baterai (SOC). Mode ini kebal terhadap arus balik dari pengereman (*Regen Braking*).

---

## ⚙️ 4. Panduan Pengaturan (Tanpa Coding)

Dashboard pintar ini menyimpan pengaturannya di memori permanen internal (EEPROM). Anda bisa mengubah parameter alat lewat kabel USB tanpa perlu melakukan *flash* ulang.

**Persiapan Mengubah Pengaturan:**

1. Colokkan alat ke laptop via kabel USB Type-C.
2. Buka Arduino IDE, pastikan port terdeteksi, lalu buka **Serial Monitor** (Ikon kaca pembesar).
3. Set baudrate di sudut kanan bawah ke **115200**.

**A. Mengubah WiFi (Untuk Sinkronisasi Jam):**
Ketik teks berikut di kolom Serial Monitor:
`WIFI,NamaHotspotAnda,Passwordnya`
*(Contoh: `WIFI,iPhone 15,rahasia123`)*
Tekan Enter. ESP32 akan menjawab konfirmasi dan *restart* mandiri. Cek Halaman 5 untuk memastikan WiFi sudah berubah.

**B. Mengubah Teks Splash Screen:**
Ketik teks berikut di kolom Serial Monitor (Maksimal 10 Huruf):
`SPLASH,TeksSambutan`
*(Contoh: `SPLASH,VESPA`)*
Tekan Enter. Alat akan *restart* dan memunculkan teks baru saat pertama kali menyala.

---

## 🔧 5. Pemecahan Masalah Umum (Troubleshooting)

1. **Layar Tetap Gelap Saat Dinyalakan:**
* Cek kabel SDA (8) dan SCL (9). Jika terbalik, I2C tidak akan merespons.
* Pastikan pin VCC dari layar OLED terhubung ke 3.3V yang stabil.


2. **Serial Monitor Tidak Muncul Tulisan Apa-apa:**
* Di Arduino IDE, buka menu `Tools` -> pastikan `USB CDC On Boot` diset ke **Enabled**.


3. **Data Sensor Menunjukkan Angka "0" atau Freeze:**
* Cek jalur kabel hijau/coklat/oranye CAN H dan CAN L. Seringkali pin ini tertukar dari soket motor. Putar kunci kontak OFF, tukar posisinya di modul MCP2515, lalu ON-kan lagi.
* Pastikan Jumper Resistor 120 Ohm di MCP2515 tertancap sempurna.
* Modul MCP2515 Anda kurang tegangan. Ingat, ESP32-C3 logikanya 3.3V, tapi tegangan suplai IC MCP2515 **WAJIB 5 Volt (VBUS)**.



---

---
### Thanks to

- @zexry619 (https://github.com/zexry619/votol-esp32-can-bus)
- @yudhaime

---
