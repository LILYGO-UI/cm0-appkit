#include <cm0/input.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <dirent.h>
#include <cstdio>
#include <cstring>

#if defined(CM0_APP_SIMULATOR)
#include <src/drivers/sdl/lv_sdl_keyboard.h>
#else
#include <linux/input.h>
#include <src/drivers/evdev/lv_evdev.h>
#endif

namespace {

#if !defined(CM0_APP_SIMULATOR)
bool has_key(const unsigned long *bits, size_t count, unsigned int key)
{
    const size_t word = key / (sizeof(unsigned long) * 8U);
    const unsigned int bit = key % (sizeof(unsigned long) * 8U);
    return word < count && (bits[word] & (1UL << bit)) != 0;
}

bool fd_is_keyboard(int fd)
{
    unsigned long bits[(KEY_MAX / (sizeof(unsigned long) * 8U)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) return false;
    return has_key(bits, sizeof(bits) / sizeof(bits[0]), KEY_ENTER) &&
           has_key(bits, sizeof(bits) / sizeof(bits[0]), KEY_A) &&
           has_key(bits, sizeof(bits) / sizeof(bits[0]), KEY_Z) &&
           has_key(bits, sizeof(bits) / sizeof(bits[0]), KEY_SPACE);
}

int open_keyboard_fd()
{
    DIR *dir = opendir("/dev/input");
    if (!dir) return -1;

    int result = -1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;
        char path[128];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        const int fd = open(path, O_RDONLY | O_NOCTTY | O_CLOEXEC);
        if (fd < 0) continue;
        if (fd_is_keyboard(fd)) {
            result = fd;
            break;
        }
        close(fd);
    }
    closedir(dir);
    return result;
}
#endif

}  // namespace

extern "C" lv_indev_t *cm0_input_create_pointer(const char *path)
{
    if (!path || !path[0]) return nullptr;
#if defined(CM0_APP_SIMULATOR)
    (void)path;
    return lv_sdl_mouse_create();
#else
    return lv_evdev_create(LV_INDEV_TYPE_POINTER, path);
#endif
}

extern "C" bool cm0_input_keyboard_present(void)
{
#if defined(CM0_APP_SIMULATOR)
    return true;
#else
    const int fd = open_keyboard_fd();
    if (fd < 0) return false;
    close(fd);
    return true;
#endif
}

extern "C" bool cm0_input_hardware_keyboard_present(void)
{
#if defined(CM0_APP_SIMULATOR)
    return false;
#else
    return cm0_input_keyboard_present();
#endif
}

extern "C" lv_indev_t *cm0_input_create_keyboard(bool *present)
{
    if (present) *present = false;
#if defined(CM0_APP_SIMULATOR)
    lv_indev_t *keyboard = lv_sdl_keyboard_create();
    if (keyboard && present) *present = true;
    return keyboard;
#else
    const int fd = open_keyboard_fd();
    if (fd < 0) return nullptr;
    lv_indev_t *keyboard = lv_evdev_create_fd(LV_INDEV_TYPE_KEYPAD, fd);
    if (keyboard && present) *present = true;
    return keyboard;
#endif
}
