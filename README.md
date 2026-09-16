# The project used mcu is the esp32s3-wroom-1-N16R8

# _12valve_test project_

(See the README.md file in the upper level  directory for more information about examples.)

This is the  buildable project which used to test the valve for diagnose the fault . 


##  folder contents

The project  contains  source files in C language [main.c](main/main.c). The file is located in folder [main](main).

Below is short explanation of remaining files in the project folder.

```
├── CMakeLists.txt
├── sdkconfig
├── main
│   ├── CMakeLists.txt
│   └── main.c
└── components/
    │
    ├── valve/
    │   ├── CMakeLists.txt
    │   ├── valve.c
    │   └── include/
    │       └── valve.h
    │
    ├── decoder/
    │   ├── CMakeLists.txt
    │   ├── decoder.c
    │   └── include/
    │       └── decoder.h
    │
    ├── shift_register/
    │   ├── CMakeLists.txt
    │   ├── shift_register.c
    │   └── include/
    │       └── shift_register.h
    │
    ├── current_sensor/
    │   ├── CMakeLists.txt
    │   ├── current_sensor.c
    │   └── include/
    │       └── current_sensor.h
    │
    ├── display/
    │
    ├── touch/
    │
    ├── key/
    │
    ├── wifi/
    │
    ├── bluetooth/
    │
    ├── wifi_provisioning/
    │
    ├── mqtt/
    │
    ├── storage/
    │
    └── ...
└── README.md                  This is the file you are currently reading
```

