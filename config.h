#ifndef VOIDNSRUN_CONFIG_H
#define VOIDNSRUN_CONFIG_H

#define PROG_VERSION "1.3"

#define USER_LISTS_MAX 50

#define CONTAINER_DIR_VAR "VOIDNSRUN_DIR"
#define UNDO_BIN_VAR "VOIDNSUNDO_BIN"
#define VOIDNSUNDO_NAME "voidnsundo"
#define OLDROOT "/oldroot"

/* This path has not been made configurable and is hardcoded
 * here for security purposes. If you want to change it, change it
 * here and recompile and reinstall both utilities. */
#define SOCK_PATH "/run/voidnsrun/sock"

#endif //VOIDNSRUN_CONFIG_H
