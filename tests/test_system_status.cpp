#include <cm0/system_status.h>

#include <assert.h>
#include <ifaddrs.h>
#include <limits.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void make_directory(const char *path) { assert(mkdir(path, 0700) == 0); }

static void write_attribute(const char *directory, const char *name,
                            const char *value) {
  char path[PATH_MAX];
  assert(snprintf(path, sizeof(path), "%s/%s", directory, name) > 0);
  FILE *file = fopen(path, "w");
  assert(file);
  assert(fputs(value, file) >= 0);
  assert(fclose(file) == 0);
}

static bool find_addressed_interface(char *name, size_t name_size) {
  struct ifaddrs *addresses = nullptr;
  if (getifaddrs(&addresses) != 0)
    return false;

  bool found = false;
  for (const struct ifaddrs *address = addresses; address;
       address = address->ifa_next) {
    if (!address->ifa_addr || !address->ifa_name ||
        !(address->ifa_flags & IFF_UP) || (address->ifa_flags & IFF_LOOPBACK) ||
        (address->ifa_addr->sa_family != AF_INET &&
         address->ifa_addr->sa_family != AF_INET6))
      continue;
    found = snprintf(name, name_size, "%s", address->ifa_name) > 0;
    break;
  }
  freeifaddrs(addresses);
  return found;
}

int main() {
  char root_template[] = "/tmp/launcher-system-status-XXXXXX";
  char *root = mkdtemp(root_template);
  assert(root);

  char network[PATH_MAX];
  char power[PATH_MAX];
  char input[PATH_MAX];
  char wlan[PATH_MAX];
  char wireless[PATH_MAX];
  char battery[PATH_MAX];
  char event[PATH_MAX];
  char input_device[PATH_MAX];
  char capabilities[PATH_MAX];
  assert(snprintf(network, sizeof(network), "%s/net", root) > 0);
  assert(snprintf(power, sizeof(power), "%s/power", root) > 0);
  assert(snprintf(input, sizeof(input), "%s/input", root) > 0);
  assert(snprintf(wlan, sizeof(wlan), "%s/test-wlan0", network) > 0);
  assert(snprintf(wireless, sizeof(wireless), "%s/wireless", wlan) > 0);
  assert(snprintf(battery, sizeof(battery), "%s/BAT0", power) > 0);
  assert(snprintf(event, sizeof(event), "%s/event0", input) > 0);
  assert(snprintf(input_device, sizeof(input_device), "%s/device", event) > 0);
  assert(snprintf(capabilities, sizeof(capabilities), "%s/capabilities",
                  input_device) > 0);
  make_directory(network);
  make_directory(power);
  make_directory(input);
  make_directory(wlan);
  make_directory(wireless);
  make_directory(battery);
  make_directory(event);
  make_directory(input_device);
  make_directory(capabilities);
  write_attribute(wlan, "carrier", "0\n");
  write_attribute(wlan, "operstate", "down\n");
  write_attribute(battery, "type", "Battery\n");
  write_attribute(battery, "present", "1\n");
  write_attribute(battery, "capacity", "72\n");
  write_attribute(battery, "status", "Discharging\n");
  write_attribute(capabilities, "key", "0\n");

  cm0_system_status_t status;
  cm0_system_status_read_with_input(network, power, input, &status);
  assert(status.wifi_present);
  assert(!status.wifi_connected);
  assert(!status.ethernet_has_ip);
  assert(!status.keyboard_present);
  assert(status.battery_present);
  assert(status.battery_capacity == 72);
  assert(!status.battery_charging);

  write_attribute(wlan, "carrier", "1\n");
  write_attribute(battery, "capacity", "41\n");
  write_attribute(battery, "status", "Charging\n");
  const unsigned long long keyboard_keys =
      (1ULL << 28) | (1ULL << 30) | (1ULL << 44) | (1ULL << 57);
  char keyboard_bitmap[32];
  assert(snprintf(keyboard_bitmap, sizeof(keyboard_bitmap), "%llx\n",
                  keyboard_keys) > 0);
  write_attribute(capabilities, "key", keyboard_bitmap);
  cm0_system_status_read_with_input(network, power, input, &status);
  assert(status.wifi_connected);
  assert(status.keyboard_present);
  assert(status.battery_capacity == 41);
  assert(status.battery_charging);

  char interface_name[IFNAMSIZ];
  if (find_addressed_interface(interface_name, sizeof(interface_name))) {
    char ethernet[PATH_MAX];
    assert(snprintf(ethernet, sizeof(ethernet), "%s/%s", network,
                    interface_name) > 0);
    make_directory(ethernet);
    write_attribute(ethernet, "type", "1\n");
    cm0_system_status_read_with_input(network, power, input, &status);
    assert(status.ethernet_has_ip);
  }

  cm0_system_status_read_with_input("/does/not/exist", "/does/not/exist",
                                    "/does/not/exist", &status);
  assert(!status.wifi_present);
  assert(!status.wifi_connected);
  assert(!status.ethernet_has_ip);
  assert(!status.keyboard_present);
  assert(!status.battery_present);
  assert(status.battery_capacity == -1);

  return 0;
}
