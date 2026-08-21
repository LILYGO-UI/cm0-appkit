#ifndef CM0_SYSTEM_STATUS_H
#define CM0_SYSTEM_STATUS_H

#include <stdbool.h>

typedef struct cm0_system_status {
  bool wifi_present;
  bool wifi_connected;
  bool battery_present;
  bool battery_charging;
  int battery_capacity;
  bool ethernet_has_ip;
  bool keyboard_present;
} cm0_system_status_t;

#ifdef __cplusplus
extern "C" {
#endif

void cm0_system_status_read(const char *network_dir,
                            const char *power_supply_dir,
                            cm0_system_status_t *status);

void cm0_system_status_read_with_input(const char *network_dir,
                                       const char *power_supply_dir,
                                       const char *input_dir,
                                       cm0_system_status_t *status);

#ifdef __cplusplus
}
#endif

#endif
