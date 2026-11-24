/* dinput_joy.h - Windows: joysticks through DirectInput 8 (dinput_joy.c) */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DiJoy DiJoy;
int dij_init(void);                  /* 0 when DirectInput is not there */
int dij_count(void);                 /* lists what is plugged in */
const char *dij_name(int i);         /* NULL if it has none */
void dij_id(int i, char *id, int n); /* its instance GUID, {...} */
int dij_is(int i, const char *device);   /* by name, instance GUID or product GUID */
int dij_usb(int i, uint16_t *vid, uint16_t *pid);
DiJoy *dij_open(int i);
void dij_close(DiJoy *j);
void dij_close_all(void);
int dij_axes(DiJoy *j);              /* X Y Z RX RY RZ, then two sliders: up to the last it has */
int dij_buttons(DiJoy *j);
void dij_update(void);               /* reads every open one */
int dij_attached(DiJoy *j);
int dij_axis(DiJoy *j, int a);       /* -32768 to 32767; 0 for one it lacks */
int dij_button(DiJoy *j, int b);
#ifdef __cplusplus
}
#endif
