// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// gd05 test tool (Linux): a kernel input device (uinput) that is the owner's
// Harmonix Rock Band Guitar as the kernel shows it (name, USB 1bad:0002,
// version 0x0103, its 11 buttons, 6 axes and hat with their ranges, read from
// his /dev/input/event9 with EVIOCGBIT / EVIOCGABS), so SDL sees it exactly
// as the real one: the same evdev path, the same GUID
// 03006f16ad1b00000200000003010000 (GD05-061). Unlike SDL's virtual
// joysticks it goes through the launcher's and the game's real SDL device
// code. Needs write access to /dev/uinput (logind's uaccess ACL).
//
//   cc -O2 -o uinput_guitar uinput_guitar.c
//   uinput_guitar SCRIPT        # lines "ms kind index value", ms from start:
//                               #   1000 key green 1   (green red yellow blue orange start back guide)
//                               #   1000 hat y 1       (strum down; -1 up, 0 rest)
//                               #   1000 abs 3 0       (an axis: 3 = whammy, 4 = tilt)
//                               #   9000 end           (the device goes away)
//   uinput_guitar SCRIPT /dev/input/eventN
//                               # no new device: the script's events written
//                               # into that one (evdev injects them as if the
//                               # device sent them: the real guitar's own node)
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static long long nowMs(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void emit(int fd, int type, int code, int value) {
    struct input_event e;
    memset(&e, 0, sizeof e);
    e.type = (unsigned short)type, e.code = (unsigned short)code, e.value = value;
    if (write(fd, &e, sizeof e) != sizeof e) perror("uinput_guitar: write");
}

static int keyCode(const char* n) {
    // The xpad driver's buttons of this guitar (SDL mapping a:b0 b:b1 x:b2 y:b3 ...).
    if (!strcmp(n, "green")) return BTN_A;
    if (!strcmp(n, "red")) return BTN_B;
    if (!strcmp(n, "blue")) return BTN_X;
    if (!strcmp(n, "yellow")) return BTN_Y;
    if (!strcmp(n, "orange")) return BTN_TL;
    if (!strcmp(n, "back")) return BTN_SELECT;
    if (!strcmp(n, "start")) return BTN_START;
    if (!strcmp(n, "guide")) return BTN_MODE;
    return atoi(n);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: uinput_guitar SCRIPT\n");
        return 2;
    }
    FILE* script = fopen(argv[1], "r");
    if (!script) {
        perror(argv[1]);
        return 2;
    }
    if (argc > 2) {
        int fd = open(argv[2], O_WRONLY);
        if (fd < 0) {
            perror(argv[2]);
            return 3;
        }
        const long long t0 = nowMs();
        char line[256];
        int sent = 0;
        while (fgets(line, sizeof line, script)) {
            long long at = 0;
            char kind[32] = "", index[32] = "";
            int value = 0;
            if (line[0] == '#' || sscanf(line, "%lld %31s %31s %d", &at, kind, index, &value) < 2) continue;
            const long long wait = t0 + at - nowMs();
            if (wait > 0) usleep((useconds_t)(wait * 1000));
            if (!strcmp(kind, "end")) break;
            if (!strcmp(kind, "key")) emit(fd, EV_KEY, keyCode(index), value);
            else if (!strcmp(kind, "hat")) emit(fd, EV_ABS, index[0] == 'x' ? ABS_HAT0X : ABS_HAT0Y, value);
            else if (!strcmp(kind, "abs")) emit(fd, EV_ABS, atoi(index), value);
            else continue;
            emit(fd, EV_SYN, SYN_REPORT, 0);
            ++sent;
        }
        fprintf(stderr, "uinput_guitar: %d events written into %s\n", sent, argv[2]);
        close(fd);
        return 0;
    }
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("uinput_guitar: /dev/uinput");
        return 3;
    }
    static const int keys[] = {BTN_A, BTN_B, BTN_X, BTN_Y, BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; ++i) ioctl(fd, UI_SET_KEYBIT, keys[i]);
    struct uinput_abs_setup abs[] = {
        {ABS_X, {0, -32768, 32767, 16, 128, 0}},  {ABS_Y, {0, -32768, 32767, 16, 128, 0}},
        {ABS_Z, {229, 0, 255, 0, 0, 0}},          {ABS_RX, {-32768, -32768, 32767, 16, 128, 0}},
        {ABS_RY, {-32768, -32768, 32767, 16, 128, 0}}, {ABS_RZ, {0, 0, 255, 0, 0, 0}},
        {ABS_HAT0X, {0, -1, 1, 0, 0, 0}},         {ABS_HAT0Y, {0, -1, 1, 0, 0, 0}},
    };
    for (size_t i = 0; i < sizeof abs / sizeof abs[0]; ++i) {
        ioctl(fd, UI_SET_ABSBIT, abs[i].code);
        if (ioctl(fd, UI_ABS_SETUP, &abs[i]) < 0) perror("uinput_guitar: UI_ABS_SETUP");
    }
    struct uinput_setup setup;
    memset(&setup, 0, sizeof setup);
    setup.id.bustype = BUS_USB, setup.id.vendor = 0x1bad, setup.id.product = 0x0002, setup.id.version = 0x0103;
    snprintf(setup.name, sizeof setup.name, "Harmonix Rock Band Guitar");
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("uinput_guitar: create");
        return 3;
    }
    char sys[64] = "";
    ioctl(fd, UI_GET_SYSNAME(sizeof sys), sys);
    fprintf(stderr, "uinput_guitar: /sys/devices/virtual/input/%s created\n", sys);
    const long long t0 = nowMs();
    char line[256];
    int sent = 0;
    while (fgets(line, sizeof line, script)) {
        long long at = 0;
        char kind[32] = "", index[32] = "";
        int value = 0;
        if (line[0] == '#' || sscanf(line, "%lld %31s %31s %d", &at, kind, index, &value) < 2) continue;
        const long long wait = t0 + at - nowMs();
        if (wait > 0) usleep((useconds_t)(wait * 1000));
        if (!strcmp(kind, "end")) break;
        if (!strcmp(kind, "key")) emit(fd, EV_KEY, keyCode(index), value);
        else if (!strcmp(kind, "hat")) emit(fd, EV_ABS, index[0] == 'x' ? ABS_HAT0X : ABS_HAT0Y, value);
        else if (!strcmp(kind, "abs")) emit(fd, EV_ABS, atoi(index), value);
        else continue;
        emit(fd, EV_SYN, SYN_REPORT, 0);
        ++sent;
    }
    fprintf(stderr, "uinput_guitar: %d events sent\n", sent);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
