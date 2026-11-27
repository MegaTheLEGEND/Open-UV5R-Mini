/* Open-UV5R-Mini linker script (JieLi AC695N, UV-5R Mini).
 *
 * Code: the app.bin slot of the stock image, mapped at 0x01E00100 (flash window at 0x01E00000; the stock
 *   app.bin is 0x77AC8 bytes).
 * RAM code (.ramfunc): code that must not run from the flash (the flash driver). It is copied by start.S to RAM
 *   0x1000 and runs at 0x02001000, the instruction alias of RAM, as the stock app runs its own RAM code
 *   (docs/RE-NOTES.md, "First boot").
 * RAM:  our .data and .bss at 0x14000..0x22000, inside the range the stock app keeps for its own variables, so
 *   the low RAM stays as the bootloader left it. The main stack is at 0xD910 (start.S), as stock;
 *   the interrupt stack is in .bss (start.S).
 * RAM2: 0x22000..0x2BF00, also the stock app's and free under ours: the FM radio's heap and DAC buffer (.ram2,
 *   not cleared at start). 0x2BF00 is the interrupt vector table (fixed in hardware; driver/irq.h).
 * BTRAM: 0x3000..0xC000, low RAM free under our firmware (stock's RAM code ran there; ours is 0x1000..0x3000, and
 *   the main stack grows down from 0xD910). JieLi's Bluetooth controller (ENABLE_BT_LE) keeps its variables there
 *   (.btram_data, .btram_bss) with the rest as the Bluetooth heap; platform/jl_sdk/bt_le.c sets it up at every
 *   Bluetooth start (not start.S), so it costs nothing while Bluetooth is off.
 * RAM2 ends at 0x2BDC0: the mask ROM keeps its own variables at 0x2BDC0..0x2BEEC (JieLi's public BR23 SDK,
 *   maskrom_stubs.ld _MASK_MEM_BEGIN / _MASK_MEM_SIZE).
 * JieLi's FM and DAC libraries (ENABLE_APP_FM) bring sections of their own names: their code goes with .text,
 *   their data with .data / .bss, except the FM interrupt and demodulator (.fm_code), which run from RAM as
 *   stock's do (2026-11-25: from the flash the interrupt took nearly all the CPU time).
 */
ENTRY(_start)
/* JieLi's libraries read the bootloader's record (chip id, trims) as boot_info: start.S copies it to 0xD920 in
 * the same layout, as stock does */
boot_info = 0xD920;
MEMORY {
  flash (rx) : ORIGIN = 0x01E00100, LENGTH = 0x77B00
  ramx (rx)  : ORIGIN = 0x02001000, LENGTH = 0x2000
  ram (rwx)  : ORIGIN = 0x00014000, LENGTH = 0xE000
  ram2 (rw)  : ORIGIN = 0x00022000, LENGTH = 0x9DC0
  btram (rw) : ORIGIN = 0x00003000, LENGTH = 0x9000
}
SECTIONS {
  .entry : { KEEP(*(.entry)) } > flash
  .text : { *(.text*) *(.rodata*) *(.*.text*) *(.[!fv]*_code*) *(.f[!m]*_code*) *(.vendor_manager_code*) *(.*.rodata*) *(.*_const*) *(.LOG_TAG_CONST*)
            *(.srodata*) . = ALIGN(4); } > flash
  .ramfunc : ALIGN(4) { __ramfunc_start = .; *(.ramfunc*) *(.fm_code*) *(.volatile_ram_code*) . = ALIGN(4); __ramfunc_end = .; } > ramx AT> flash
  __ramfunc_load = LOADADDR(.ramfunc);
  /* JieLi's Bluetooth controller: its own section names (SDK btctler_lib_data.ld / _bss.ld) */
  .btram_data : ALIGN(4) { __btram_data_start = .; *(.bt_rf_data*) *(.hci_controller_data*) *(.hci_interface_data*)
            *(.device_manager_data*) *(.vendor_manager_data*) *(.ble_*_data*) *(.classic_*_data*) *(.lmp_*_data*)
            . = ALIGN(4); __btram_data_end = .; } > btram AT> flash
  __btram_data_load = LOADADDR(.btram_data);
  .btram_bss (NOLOAD) : ALIGN(4) { __btram_bss_start = .; *(.btram.bss*) *(.bd_base*) *(.bredr_*bulk*) *(.bt_rf_bss*)
            *(.hci_controller_bss*) *(.hci_interface_bss*) *(.device_manager_bss*) *(.vendor_manager_bss*)
            *(.ble_*_bss*) *(.classic_*_bss*) *(.lmp_*_bss*) . = ALIGN(8); __btram_bss_end = .; } > btram
  /* classic Bluetooth's pools and the TWS lists the library names: empty (BLE only) */
  acl_tx_pool = __btram_bss_end;  acl_tx_pool_end = __btram_bss_end;
  acl_rx_pool = __btram_bss_end;  acl_rx_pool_end = __btram_bss_end;
  tws_bulk_pool = __btram_bss_end; tws_bulk_pool_end = __btram_bss_end;
  tws_func_stub_begin = __btram_bss_end; tws_func_stub_end = __btram_bss_end;
  tws_sync_channel_begin = __btram_bss_end; tws_sync_channel_end = __btram_bss_end;
  __VERSION_BEGIN = __btram_bss_end;
  __btram_heap_start = __btram_bss_end;
  __btram_heap_end = ORIGIN(btram) + LENGTH(btram);
  .data : ALIGN(4) { __data_start = .; *(.data*) *(.*.data*) *(.sdata*) . = ALIGN(4); __data_end = .; } > ram AT> flash
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : ALIGN(4) { __bss_start = .; *(.bss*) *(.*.bss*) *(.sbss*) *(.audio_dac_data*) *(COMMON) . = ALIGN(4); __bss_end = .; } > ram
  .ram2 (NOLOAD) : ALIGN(8) { *(.ram2*) *(.fm_mem*) } > ram2   /* .fm_mem: JieLi's FM sample buffer (zeros) */
}
