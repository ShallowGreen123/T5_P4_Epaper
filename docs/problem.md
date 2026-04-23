1. examples/factory 程序使用 espressif/esp_hosted: 2.12.3 更高的版本时，会导致屏幕刷新特别慢，点击进去页面会出现下面这样的错误，所以 esp_hosted 只能固定为 2.12.3 版本；至于为什么会这样目前还不知道
~~~log
E (87471) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
E (87471) task_wdt:  - IDLE0 (CPU 0)
E (87471) task_wdt: Tasks currently running:
E (87471) task_wdt: CPU 0: main
E (87471) task_wdt: CPU 1: IDLE1
E (87471) task_wdt: Print CPU 0 (current core) backtrace
esp_backtrace_print: Print CPU 0 (current core) registers
Core  0 register dump:
MEPC    : 0x4ff0b4f2  RA      : 0x4ff015e4  SP      : 0x5010f050  GP      : 0x4ff14100  
TP      : 0x5010f350  T0      : 0x4fc10cc4  T1      : 0xfffffff0  T2      : 0x00a00000  
S0/FP   : 0x4ff3b3dc  S1      : 0x4ff14000  A0      : 0x4ff13930  A1      : 0x500d6090  
A2      : 0x00000000  A3      : 0x00000000  A4      : 0x20800008  A5      : 0x1f000000  
A6      : 0x00000004  A7      : 0x4fc10a5c  S2      : 0x4ff3b828  S3      : 0x48000f14  
S4      : 0x00000001  S5      : 0x00000006  S6      : 0x00000001  S7      : 0x00000000  
S8      : 0x00000177  S9      : 0x00000168  S10     : 0x00000000  S11     : 0x00000001  
T3      : 0x00000000  T4      : 0x00000000  T5      : 0x00000000  T6      : 0x00000000  
MSTATUS : 0x00001888  MTVEC   : 0x4ff00003  MCAUSE  : 0xdeadc0de  MTVAL   : 0xdeadc0de  
MHARTID : 0x00000000
~~~