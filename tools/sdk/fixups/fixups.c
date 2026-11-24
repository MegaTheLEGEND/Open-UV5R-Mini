/* Open-UV5R-Mini: link fix-ups for building JieLi's BR23 SDK with pi32v2-llvm (tools/sdk/README.md, fix 5).
 *
 * JieLi's prebuilt start-up code (cpu.a, startup.S) runs from RAM and calls exception_analyze (cpu.a, debug.c, in
 * flash) with a short call, which reaches +-2 MB; RAM is at 0 and flash at 0x1E00000. The link is made with
 * --wrap=exception_analyze, so that call comes here, in RAM code, and this makes the long call on to flash. */

void __real_exception_analyze(void);

__attribute__((section(".volatile_ram_code"), used)) void __wrap_exception_analyze(void)
{
    __real_exception_analyze();
}
