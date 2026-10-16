#pragma once

// Called once by main.c after resolving the app.so path, so
// graceloader_get_install_basepath() can report it to the guest.
void bsp_shim_set_install_basepath(char const *path);
