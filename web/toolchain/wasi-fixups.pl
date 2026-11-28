# Fix-ups after YoWASP's WASI patch for LLVM 23 (perl -0pi, see build-wasm.sh)
s/(AddSignalHandler\(sys::SignalHandlerCallback FnPtr,\s*void \*Cookie)\) \{\}/$1, bool) {}/;
# CrashRecoveryContext.cpp: the WASI stubs need LLVM 23's argument
s/static void installExceptionOrSignalHandlers\(\) \{\}/static void installExceptionOrSignalHandlers(bool) {}/;
# Unix/Path.inc: randomAccessImpl (new since 22) has no madvise on WASI either
s/#if defined\(__MVS__\) \|\| defined\(_AIX\)\n(  \/\/ If we don't have madvise, or it isn't beneficial, treat this as a no-op.\n#elif defined\(POSIX_MADV_RANDOM\))/#if defined(__MVS__) || defined(_AIX) || defined(__wasi__)\n$1/;
# Unix/Process.inc: the WASI SafelyCloseFileDescriptor returned errno even when close() worked, so a stale ENOENT
# from an include search made clang abort with "IO failure on output stream"
s/  ::close\(FD\);\n  return errnoAsErrorCode\(\);/  if (::close(FD) < 0)\n    return errnoAsErrorCode();\n  return std::error_code();/;
# Unix/Path.inc: WASI has no file permissions (wasi-libc's fchmod is ENOSYS); llvm-objcopy copies them to its output
s/(std::error_code setPermissions\(int FD, perms Permissions\) \{\n)(  if \(::fchmod)/$1#if defined(__wasi__)\n  return std::error_code();\n#endif\n$2/;
