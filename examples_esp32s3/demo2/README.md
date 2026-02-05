
### testing environment

- esp-idf：v5.4
- arduino-esp32：3.2.0
- FastEPD lib：3f7a187 (HEAD -> main, origin/main, origin/HEAD) 

### Compile and download

The project can be directly compiled and downloaded.

- `idf.py build`
- `idf.py -p [your_COM] flash`

### engineering setup

Add `espressif/arduino-esp32: ==3.2.0` to the file `main/idf_component.yml`

Add `arduino-esp32` to the `CMakeLists.txt` file of FastEPD.

~~~cmake

idf_component_register(
    SRCS "src/FastEPD.cpp"
         "src/FastEPD.inl"
         "src/Group5.cpp"
         "src/bb_ep_gfx.inl"
         "src/g5dec.inl"
    INCLUDE_DIRS "src"
    REQUIRES driver esp_timer esp_lcd arduino-esp32
)

~~~

To use the arduino-esp32 component, the following settings are required

1. Use `idf.py menuconfig` to enter the configuration interface.
2. Component config -> FreeRTOS -> Kerne -> (`1000`)configTICK_RATE_HZ
3. Arduino Configuration -> \<`esp32p4`> Arduino target variant (board)
