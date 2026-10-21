// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ankit Dagar
//
// brother-statusd — polls a Brother laser printer over USB and writes its PJL
// status to a small key=value file that the CUPS filter reads.
//
//   brother-statusd [usb-product-name] [interval-seconds]
//
// Runs as root from a LaunchDaemon. Opens the printer interface only for the
// few hundred milliseconds a poll takes, so it does not hold the device while
// cupsd needs it: if the interface is busy, the poll is skipped and the status
// file simply goes stale.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USBSpec.h>

#define PRINTER_CLASS 7
#define STATE_PATH "/etc/cups/brother-status"

static volatile sig_atomic_t running = 1;

struct device {
  io_service_t service;
  IOCFPlugInInterface **plug;
  IOUSBDeviceInterface **dev;
  io_service_t intf_service;
  IOCFPlugInInterface **iplug;
  IOUSBInterfaceInterface **intf;
  UInt8 out_pipe;
  UInt8 in_pipe;
};

static void on_signal(int sig) {
  (void)sig;
  running = 0;
}

static io_service_t find_device(const char *product) {
  CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
  if (!match) return IO_OBJECT_NULL;

  io_iterator_t iter = IO_OBJECT_NULL;
  if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter) != KERN_SUCCESS)
    return IO_OBJECT_NULL;

  io_service_t service, found = IO_OBJECT_NULL;
  while ((service = IOIteratorNext(iter))) {
    CFTypeRef name = IORegistryEntryCreateCFProperty(
        service, CFSTR(kUSBProductString), kCFAllocatorDefault, 0);
    if (name) {
      char buf[256] = {0};
      if (CFGetTypeID(name) == CFStringGetTypeID())
        CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
      CFRelease(name);
      if (strstr(buf, product) != NULL) { found = service; break; }
    }
    IOObjectRelease(service);
  }
  IOObjectRelease(iter);
  return found;
}

static void *iface_from(void *plugin, CFUUIDRef uuid) {
  IOCFPlugInInterface **plug = plugin;
  void *out = NULL;
  if (!plug) return NULL;
  (*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(uuid), (LPVOID *)&out);
  return out;
}

static void device_close(struct device *d) {
  if (d->intf) {
    (*d->intf)->USBInterfaceClose(d->intf);
    (*d->intf)->Release(d->intf);
  }
  if (d->iplug) (*d->iplug)->Release(d->iplug);
  if (d->intf_service) IOObjectRelease(d->intf_service);
  if (d->dev) {
    (*d->dev)->USBDeviceClose(d->dev);
    (*d->dev)->Release(d->dev);
  }
  if (d->plug) (*d->plug)->Release(d->plug);
  if (d->service) IOObjectRelease(d->service);
  memset(d, 0, sizeof(*d));
}

static int device_open(struct device *d, const char *product) {
  memset(d, 0, sizeof(*d));
  d->service = find_device(product);
  if (!d->service) return 0;

  SInt32 score = 0;
  if (IOCreatePlugInInterfaceForService(d->service, kIOUSBDeviceUserClientTypeID,
                                        kIOCFPlugInInterfaceID, &d->plug, &score) != KERN_SUCCESS)
    goto fail;
  d->dev = iface_from(d->plug, kIOUSBDeviceInterfaceID);
  if (!d->dev) goto fail;

  if ((*d->dev)->USBDeviceOpen(d->dev) != kIOReturnSuccess) goto fail;

  UInt8 config = 0;
  if ((*d->dev)->GetConfiguration(d->dev, &config) == kIOReturnSuccess && config != 1) {
    if ((*d->dev)->SetConfiguration(d->dev, 1) != kIOReturnSuccess) goto fail;
  }

  io_iterator_t children = IO_OBJECT_NULL;
  if (IORegistryEntryGetChildIterator(d->service, kIOServicePlane, &children) != KERN_SUCCESS)
    goto fail;

  int ok = 0;
  io_service_t child;
  while ((child = IOIteratorNext(children))) {
    io_name_t cls;
    memset(cls, 0, sizeof(cls));
    if (IOObjectGetClass(child, cls) != KERN_SUCCESS) { IOObjectRelease(child); continue; }
    if (strcmp(cls, "IOUSBInterface") != 0 && strcmp(cls, "IOUSBHostInterface") != 0) {
      IOObjectRelease(child);
      continue;
    }

    d->intf_service = child;
    if (IOCreatePlugInInterfaceForService(child, kIOUSBInterfaceUserClientTypeID,
                                          kIOCFPlugInInterfaceID, &d->iplug, &score) != KERN_SUCCESS)
      { d->intf_service = 0; IOObjectRelease(child); continue; }
    d->intf = iface_from(d->iplug, kIOUSBInterfaceInterfaceID);
    if (!d->intf) { d->intf_service = 0; IOObjectRelease(child); continue; }

    UInt8 cls_code = 0;
    (*d->intf)->GetInterfaceClass(d->intf, &cls_code);
    if (cls_code != PRINTER_CLASS) {
      (*d->intf)->Release(d->intf); d->intf = NULL;
      (*d->iplug)->Release(d->iplug); d->iplug = NULL;
      d->intf_service = 0;
      IOObjectRelease(child);
      continue;
    }

    // Busy means cupsd is mid-job. Skip rather than fight for the interface.
    if ((*d->intf)->USBInterfaceOpen(d->intf) != kIOReturnSuccess) {
      (*d->intf)->Release(d->intf); d->intf = NULL;
      (*d->iplug)->Release(d->iplug); d->iplug = NULL;
      d->intf_service = 0;
      IOObjectRelease(child);
      break;
    }

    UInt8 n = 0;
    (*d->intf)->GetNumEndpoints(d->intf, &n);
    for (UInt8 p = 1; p <= n; p++) {
      UInt8 dir = 0, num = 0, type = 0, interval = 0;
      UInt16 mps = 0;
      if ((*d->intf)->GetPipeProperties(d->intf, p, &dir, &num, &type, &mps, &interval) != kIOReturnSuccess)
        continue;
      if (type == kIOUSBEndpointTypeBulk) {
        if (dir == kUSBOut) d->out_pipe = p; else d->in_pipe = p;
      }
    }
    ok = (d->out_pipe && d->in_pipe);
    break;
  }
  IOObjectRelease(children);

  if (!ok) goto fail;
  return 1;

fail:
  device_close(d);
  return 0;
}

// Sends one PJL command and collects the reply. Returns bytes read, 0 on none.
static int pjl_query(struct device *d, const char *command, char *out, size_t outlen) {
  char request[512];
  int len = snprintf(request, sizeof(request), "\033%%-12345X%s\r\n\033%%-12345X", command);
  if (len < 0 || (size_t)len >= sizeof(request)) return 0;

  UInt8 junk[1024];
  for (int i = 0; i < 6; i++) {
    UInt32 jg = sizeof(junk);
    if ((*d->intf)->ReadPipeTO(d->intf, d->in_pipe, junk, &jg, 150, 150) != kIOReturnSuccess || jg == 0)
      break;
  }

  if ((*d->intf)->WritePipeTO(d->intf, d->out_pipe, request, (UInt32)len, 2000, 2000) != kIOReturnSuccess)
    return 0;

  usleep(300000);

  size_t used = 0;
  for (int attempt = 0; attempt < 8; attempt++) {
    UInt8 buf[1024];
    UInt32 got = sizeof(buf);
    if ((*d->intf)->ReadPipeTO(d->intf, d->in_pipe, buf, &got, 1500, 1500) != kIOReturnSuccess || got == 0)
      continue;
    size_t room = outlen - 1 - used;
    size_t take = (got < room) ? got : room;
    memcpy(out + used, buf, take);
    used += take;
    if (used >= outlen - 1) break;
  }
  out[used] = '\0';
  return (int)used;
}

static long parse_code(const char *reply) {
  const char *p = strstr(reply, "CODE=");
  if (!p) return -1;
  return strtol(p + 5, NULL, 10);
}

static int parse_online(const char *reply) {
  const char *p = strstr(reply, "ONLINE=");
  if (!p) return -1;
  return strncmp(p + 7, "TRUE", 4) == 0;
}

// The reply echoes the command, then the value on its own line.
static void parse_paper(const char *reply, char *out, size_t outlen) {
  out[0] = '\0';
  const char *p = reply;
  while (*p) {
    const char *end = strchr(p, '\n');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    char line[128];
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = '\0';

    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' '))
      line[--len] = '\0';

    if (len > 0 && line[0] != '@') {
      size_t i = 0, j = 0;
      while (i < len && j + 1 < outlen) {
        if (line[i] != '"' && line[i] != '\033') out[j++] = line[i];
        i++;
      }
      out[j] = '\0';
      if (j > 0) return;
    }
    if (!end) break;
    p = end + 1;
  }
}

static int write_state(long code, int online, const char *paper) {
  char tmp[256];
  snprintf(tmp, sizeof(tmp), STATE_PATH ".tmp");

  FILE *fp = fopen(tmp, "w");
  if (!fp) return 0;
  fprintf(fp, "code=%ld\nonline=%d\npaper=%s\nupdated=%ld\n",
          code, online ? 1 : 0, paper, (long)time(NULL));
  if (fclose(fp) != 0) { unlink(tmp); return 0; }

  if (rename(tmp, STATE_PATH) != 0) { unlink(tmp); return 0; }
  return 1;
}

int main(int argc, char **argv) {
  const char *product = (argc > 1) ? argv[1] : "DCP-L2520D";
  int interval = (argc > 2) ? atoi(argv[2]) : 10;
  if (interval < 2) interval = 2;

  signal(SIGTERM, on_signal);
  signal(SIGINT, on_signal);

  while (running) {
    struct device d;
    if (device_open(&d, product)) {
      char status_reply[2048] = {0};
      char paper_reply[512] = {0};

      long code = -1;
      int online = 1;
      char paper[64] = {0};

      if (pjl_query(&d, "@PJL INFO STATUS", status_reply, sizeof(status_reply)) > 0) {
        code = parse_code(status_reply);
        int parsed_online = parse_online(status_reply);
        if (parsed_online >= 0) online = parsed_online;
      }
      if (pjl_query(&d, "@PJL DINQUIRE PAPER", paper_reply, sizeof(paper_reply)) > 0)
        parse_paper(paper_reply, paper, sizeof(paper));

      device_close(&d);

      if (code >= 0) {
        if (!write_state(code, online, paper))
          fprintf(stderr, "brother-statusd: cannot write %s\n", STATE_PATH);
      }
    }

    for (int slept = 0; slept < interval * 10 && running; slept++)
      usleep(100000);
  }
  return 0;
}
