/* Minimal sdkconfig for building loadable ELFs against firmware headers.
 * Enables the termios declarations in the local sys/termios.h stub; the real
 * termios functions are resolved from the firmware symbol table at load. */
#pragma once
#define CONFIG_VFS_SUPPORT_TERMIOS 1
