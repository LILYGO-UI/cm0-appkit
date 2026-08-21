#include <cm0/system_status.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <ifaddrs.h>
#include <limits.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool make_path(char *path, size_t path_size, const char *root,
                      const char *entry, const char *attribute) {
  int length =
      attribute ? snprintf(path, path_size, "%s/%s/%s", root, entry, attribute)
                : snprintf(path, path_size, "%s/%s", root, entry);
  return length >= 0 && static_cast<size_t>(length) < path_size;
}

static bool read_text(const char *root, const char *entry,
                      const char *attribute, char *text, size_t text_size) {
  char path[PATH_MAX];
  if (!text || text_size == 0 ||
      !make_path(path, sizeof(path), root, entry, attribute))
    return false;

  FILE *file = fopen(path, "r");
  if (!file)
    return false;
  bool success = fgets(text, static_cast<int>(text_size), file) != nullptr;
  fclose(file);
  if (!success)
    return false;

  size_t length = strlen(text);
  while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r' ||
                        text[length - 1] == ' ' || text[length - 1] == '\t'))
    text[--length] = '\0';
  return length > 0;
}

static bool read_number(const char *root, const char *entry,
                        const char *attribute, int *value) {
  char text[32];
  if (!read_text(root, entry, attribute, text, sizeof(text)))
    return false;

  errno = 0;
  char *end = nullptr;
  long parsed = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || parsed < INT_MIN ||
      parsed > INT_MAX)
    return false;
  *value = static_cast<int>(parsed);
  return true;
}

static bool path_is_directory(const char *root, const char *entry,
                              const char *child) {
  char path[PATH_MAX];
  struct stat info{};
  return make_path(path, sizeof(path), root, entry, child) &&
         stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool is_wireless_interface(const char *root, const char *entry) {
  if (path_is_directory(root, entry, "wireless"))
    return true;

  char uevent[256];
  return read_text(root, entry, "uevent", uevent, sizeof(uevent)) &&
         strstr(uevent, "DEVTYPE=wlan") != nullptr;
}

static bool interface_has_ip(const struct ifaddrs *addresses,
                             const char *interface_name) {
  for (const struct ifaddrs *address = addresses; address;
       address = address->ifa_next) {
    if (!address->ifa_addr || !address->ifa_name ||
        strcmp(address->ifa_name, interface_name) != 0 ||
        !(address->ifa_flags & IFF_UP) || (address->ifa_flags & IFF_LOOPBACK))
      continue;

    if (address->ifa_addr->sa_family == AF_INET) {
      const auto *ipv4 =
          reinterpret_cast<const struct sockaddr_in *>(address->ifa_addr);
      if (ipv4->sin_addr.s_addr != htonl(INADDR_ANY))
        return true;
    } else if (address->ifa_addr->sa_family == AF_INET6) {
      const auto *ipv6 =
          reinterpret_cast<const struct sockaddr_in6 *>(address->ifa_addr);
      if (!IN6_IS_ADDR_UNSPECIFIED(&ipv6->sin6_addr))
        return true;
    }
  }
  return false;
}

static bool is_ethernet_interface(const char *network_dir,
                                  const char *interface_name) {
  int type = 0;
  return strcmp(interface_name, "lo") != 0 &&
         !is_wireless_interface(network_dir, interface_name) &&
         read_number(network_dir, interface_name, "type", &type) && type == 1;
}

static void read_ethernet_status(const char *network_dir,
                                 cm0_system_status_t *status) {
  if (!network_dir)
    return;

  struct ifaddrs *addresses = nullptr;
  if (getifaddrs(&addresses) != 0)
    return;

  DIR *directory = opendir(network_dir);
  if (!directory) {
    freeifaddrs(addresses);
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(directory)) != nullptr) {
    if (entry->d_name[0] == '.' ||
        !is_ethernet_interface(network_dir, entry->d_name))
      continue;
    if (interface_has_ip(addresses, entry->d_name)) {
      status->ethernet_has_ip = true;
      break;
    }
  }
  closedir(directory);
  freeifaddrs(addresses);
}

static int hex_digit_value(char character) {
  if (character >= '0' && character <= '9')
    return character - '0';
  character = static_cast<char>(tolower(static_cast<unsigned char>(character)));
  return character >= 'a' && character <= 'f' ? character - 'a' + 10 : -1;
}

static bool key_bitmap_has(const char *bitmap, unsigned int key_code) {
  if (!bitmap)
    return false;

  const size_t target_digit = key_code / 4;
  const unsigned int target_bit = key_code % 4;
  size_t digit = 0;
  for (const char *cursor = bitmap + strlen(bitmap); cursor != bitmap;) {
    const int value = hex_digit_value(*--cursor);
    if (value < 0)
      continue;
    if (digit == target_digit)
      return (value & (1 << target_bit)) != 0;
    ++digit;
  }
  return false;
}

static bool is_keyboard(const char *input_dir, const char *event_name) {
  enum {
    KEY_ENTER = 28,
    KEY_A = 30,
    KEY_Z = 44,
    KEY_SPACE = 57,
  };
  char bitmap[2048];
  return read_text(input_dir, event_name, "device/capabilities/key", bitmap,
                   sizeof(bitmap)) &&
         key_bitmap_has(bitmap, KEY_ENTER) && key_bitmap_has(bitmap, KEY_A) &&
         key_bitmap_has(bitmap, KEY_Z) && key_bitmap_has(bitmap, KEY_SPACE);
}

static void read_keyboard_status(const char *input_dir,
                                 cm0_system_status_t *status) {
  if (!input_dir)
    return;
  DIR *directory = opendir(input_dir);
  if (!directory)
    return;

  struct dirent *entry;
  while ((entry = readdir(directory)) != nullptr) {
    if (strncmp(entry->d_name, "event", 5) != 0 ||
        !is_keyboard(input_dir, entry->d_name))
      continue;
    status->keyboard_present = true;
    break;
  }
  closedir(directory);
}

static void read_wifi_status(const char *network_dir,
                             cm0_system_status_t *status) {
  if (!network_dir)
    return;
  DIR *directory = opendir(network_dir);
  if (!directory)
    return;

  struct dirent *entry;
  while ((entry = readdir(directory)) != nullptr) {
    if (entry->d_name[0] == '.' ||
        !is_wireless_interface(network_dir, entry->d_name))
      continue;

    status->wifi_present = true;
    int carrier = 0;
    char operstate[32];
    if ((read_number(network_dir, entry->d_name, "carrier", &carrier) &&
         carrier == 1) ||
        (read_text(network_dir, entry->d_name, "operstate", operstate,
                   sizeof(operstate)) &&
         strcmp(operstate, "up") == 0)) {
      status->wifi_connected = true;
      break;
    }
  }
  closedir(directory);
}

static bool is_present_battery(const char *root, const char *entry) {
  char type[32];
  if (!read_text(root, entry, "type", type, sizeof(type)) ||
      strcmp(type, "Battery") != 0)
    return false;
  int present = 1;
  return !read_number(root, entry, "present", &present) || present != 0;
}

static void read_battery_status(const char *power_supply_dir,
                                cm0_system_status_t *status) {
  if (!power_supply_dir)
    return;
  DIR *directory = opendir(power_supply_dir);
  if (!directory)
    return;

  struct dirent *entry;
  while ((entry = readdir(directory)) != nullptr) {
    if (entry->d_name[0] == '.' ||
        !is_present_battery(power_supply_dir, entry->d_name))
      continue;

    status->battery_present = true;
    int capacity = -1;
    if (read_number(power_supply_dir, entry->d_name, "capacity", &capacity) &&
        capacity >= 0 && capacity <= 100)
      status->battery_capacity = capacity;

    char charging[32];
    if (read_text(power_supply_dir, entry->d_name, "status", charging,
                  sizeof(charging)))
      status->battery_charging =
          strcmp(charging, "Charging") == 0 || strcmp(charging, "Full") == 0;
    break;
  }
  closedir(directory);
}

void cm0_system_status_read(const char *network_dir,
                            const char *power_supply_dir,
                            cm0_system_status_t *status) {
  cm0_system_status_read_with_input(network_dir, power_supply_dir,
                                    "/sys/class/input", status);
}

void cm0_system_status_read_with_input(const char *network_dir,
                                       const char *power_supply_dir,
                                       const char *input_dir,
                                       cm0_system_status_t *status) {
  if (!status)
    return;
  memset(status, 0, sizeof(*status));
  status->battery_capacity = -1;
  read_wifi_status(network_dir, status);
  read_ethernet_status(network_dir, status);
  read_battery_status(power_supply_dir, status);
  read_keyboard_status(input_dir, status);
}
