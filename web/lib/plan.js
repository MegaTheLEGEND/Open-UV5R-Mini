// What `make` would run for a set of feature switches: the port of the Makefile's source list, its ENABLE_ filters
// and features.mk's FEATURE_FLAGS, for the in-browser build. tests/web_plan_host.py checks it against `make -n`
// for many switch sets; change both together.
//
// features: {ENABLE_CHIRP: 1, ..., REGION: 0} (missing names take their features.mk default from `defaults`).
// The site always carries JieLi's libraries, so the FM and BT_LE "library missing" fallbacks never apply.

const SRC = `src/main.c src/board.c src/app/app.c src/radio/radio.c src/radio/txlock.c src/store/calib.c src/store/channels.c src/radio/tones.c src/store/settings.c src/app/battery.c src/radio/scan.c src/radio/dualwatch.c src/radio/vox.c src/radio/dtmfseq.c src/app/keytest.c src/app/program.c src/app/version.c src/update/update.c src/update/update_radio.c
src/driver/uart.c src/bt/bt_none.c src/driver/gpio.c src/driver/system.c src/driver/bk4819.c src/driver/st7735s.c src/driver/keyboard.c
src/driver/flash.c src/driver/io.c src/driver/adc.c src/driver/mem.c src/driver/extflash.c src/driver/spi1.c
src/driver/irq.c
src/ui/font.c src/ui/helper.c src/ui/textentry.c src/ui/icons.c src/ui/screen_main.c src/ui/screen_menu.c
src/app/apps.c src/app/spectrum.c src/app/meter.c src/app/chbook.c src/app/sysinfo.c src/app/snake.c
src/app/fmradio.c src/platform/jl_sdk/fmrx.c src/platform/jl_sdk/fm_glue.c src/radio/rds.c
src/ui/screen_apps.c src/ui/status.c
src/app/monitor.c src/app/aprs.c src/radio/ax25.c src/radio/aprs.c src/radio/fskrx.c
src/app/subghz.c src/radio/ook.c
src/bt/le_adv.c src/app/btscan.c src/app/btinfo.c src/app/btbeacon.c src/app/btserial.c src/app/btchirp.c src/app/btpair.c`.split(/\s+/);
const ASM = ['src/start.S', 'src/update/handover.S'];

// ENABLE_x = 0 leaves these files out
const DROP_IF_OFF = {
  ENABLE_KEYTEST: ['src/app/keytest.c'],
  ENABLE_APP_SPECTRUM: ['src/app/spectrum.c'],
  ENABLE_APP_METER: ['src/app/meter.c'],
  ENABLE_APP_CHANNELS: ['src/app/chbook.c'],
  ENABLE_APP_SYSINFO: ['src/app/sysinfo.c'],
  ENABLE_APP_APRS: ['src/app/aprs.c', 'src/radio/aprs.c', 'src/radio/fskrx.c'],
  ENABLE_APP_SUBGHZ: ['src/app/subghz.c', 'src/radio/ook.c'],
  ENABLE_APP_BTSCAN: ['src/app/btscan.c'],
  ENABLE_APP_BTINFO: ['src/app/btinfo.c'],
  ENABLE_APP_BTBEACON: ['src/app/btbeacon.c'],
  ENABLE_APP_BTSERIAL: ['src/app/btserial.c'],
  ENABLE_APP_BTCHIRP: ['src/app/btchirp.c'],
  ENABLE_APP_SNAKE: ['src/app/snake.c'],
};

// features.mk's FEATURE_FLAGS, in its order: [define, switch]
const DEFINES = [
  ['PROGRAM_ENABLE', 'ENABLE_CHIRP'], ['PROGRAM_SCREEN_TEST', 'ENABLE_SCREEN_TEST'],
  ['SETTINGS_ENABLE_SAVE', 'ENABLE_SAVE'], ['RADIO_ENABLE_TX', 'ENABLE_TX'], ['LCD_HW_SPI', 'ENABLE_LCD_SPI'],
  ['TXLOCK_REGIONS', 'ENABLE_REGIONS'], ['TXLOCK_REGION', 'REGION'], ['TXLOCK_UNLOCK', 'ENABLE_TX_UNLOCK'],
  ['KEYTEST_ENABLE', 'ENABLE_KEYTEST'],
  ['APP_SPECTRUM_ENABLE', 'ENABLE_APP_SPECTRUM'], ['APP_METER_ENABLE', 'ENABLE_APP_METER'],
  ['APP_CHANNELS_ENABLE', 'ENABLE_APP_CHANNELS'], ['APP_SYSINFO_ENABLE', 'ENABLE_APP_SYSINFO'],
  ['APP_SNAKE_ENABLE', 'ENABLE_APP_SNAKE'], ['APP_FM_ENABLE', 'ENABLE_APP_FM'],
  ['APP_APRS_ENABLE', 'ENABLE_APP_APRS'], ['APP_SUBGHZ_ENABLE', 'ENABLE_APP_SUBGHZ'],
  ['APP_BTSCAN_ENABLE', 'ENABLE_APP_BTSCAN'], ['APP_BTINFO_ENABLE', 'ENABLE_APP_BTINFO'],
  ['APP_BTBEACON_ENABLE', 'ENABLE_APP_BTBEACON'], ['APP_BTSERIAL_ENABLE', 'ENABLE_APP_BTSERIAL'],
  ['APP_BTCHIRP_ENABLE', 'ENABLE_APP_BTCHIRP'], ['BT_LE_ENABLE', 'ENABLE_BT_LE'],
  ['BT_PAIRED_ONLY', 'ENABLE_BT_PAIRED_ONLY'], ['DEBUGGING', 'ENABLE_DEBUGGING'],
];

export const SDK = 'sdk', BUILTINS = 'lib/libclang_rt.builtins.a';

// -> {features (after the Makefile's overrides), notes, compile: [{src, obj, args}], link: args, objcopy: args}
export function makePlan(features, defaults, fwBuild = 'web') {
  const f = { ...defaults, ...features };
  const notes = [];
  for (const k of Object.keys(f)) f[k] = String(f[k]);
  if (f.ENABLE_BT_LE === '1' && f.ENABLE_APP_FM !== '1') {
    notes.push('ENABLE_BT_LE needs ENABLE_APP_FM (its glue and libraries); Bluetooth is left out');
    f.ENABLE_BT_LE = '0';
  }
  let src = SRC.slice();
  for (const [sw, files] of Object.entries(DROP_IF_OFF)) if (f[sw] === '0') src = src.filter(s => !files.includes(s));
  if (f.ENABLE_BT_LE === '1') {
    src = src.filter(s => s !== 'src/bt/bt_none.c');
    src.push('src/platform/jl_sdk/bt_le.c', 'src/platform/jl_sdk/bt_glue.c', 'src/bt/gatt.c', 'src/bt/smp.c');
  }
  let libs = [];
  if (f.ENABLE_APP_FM === '1') {
    libs = ['--start-group', `${SDK}/cpu.a`, `${SDK}/media.a`, ...(f.ENABLE_BT_LE === '1' ? [`${SDK}/btctrler.a`] : []),
      '--end-group', BUILTINS];
  } else {
    const fm = ['src/app/fmradio.c', 'src/platform/jl_sdk/fmrx.c', 'src/platform/jl_sdk/fm_glue.c', 'src/radio/rds.c'];
    src = src.filter(s => !fm.includes(s));
  }
  const defs = DEFINES.map(([d, sw]) => `-D${d}=${f[sw]}`);
  if (f.ENABLE_BT_LE + f.ENABLE_APP_BTCHIRP + f.ENABLE_CHIRP === '111') defs.push('-DPROGRAM_BT=1');
  const cflags = ['--target=pi32v2', '-Os', '-ffreestanding', '-fno-builtin', '-ffunction-sections', '-fdata-sections',
    '-std=c11', '-Wall', '-Wextra', '-Isrc', ...defs];
  const obj = s => 'build/fw/' + s.replace(/\.[cS]$/, '.o');
  const compile = src.map(s => {
    const extra = [];
    if (s === 'src/app/version.c') extra.push(`-DFW_BUILD="${fwBuild}"`);
    if (s === 'src/platform/jl_sdk/bt_glue.c') extra.push('-flto');
    return { src: s, obj: obj(s), args: [...cflags, ...extra, '-c', s, '-o', obj(s)] };
  });
  for (const s of ASM) compile.push({ src: s, obj: obj(s), args: ['--target=pi32v2', '-c', s, '-o', obj(s)] });
  const objs = [...src, ...ASM].map(obj);
  const link = ['-T', 'src/firmware.ld', '--gc-sections', ...objs, ...libs, '-o', 'build/fw/firmware.elf'];
  const objcopy = ['-O', 'binary', 'build/fw/firmware.elf', 'build/fw/firmware.bin'];
  return { features: f, notes, compile, link, objcopy };
}
