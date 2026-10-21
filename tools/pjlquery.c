// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ankit Dagar
// Diagnostic: send one PJL query over the printer's USB interface and print the
// reply, so we can see what the device reports about itself.
//
//   pjlquery "<USB Product Name>" "<pjl command>" [-v]
//
// Opens only the printer-class interface (class 7), writes the request to its
// bulk OUT pipe and reads the bulk IN pipe with a timeout. Whatever PJL you
// pass is sent verbatim, so a printing command would print.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USBSpec.h>

#define PRINTER_CLASS 7
#define MAX_PJL 512

static int verbose = 0;

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
      if (strcmp(buf, product) == 0) { found = service; break; }
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

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <product> <pjl> [-v]\n", argv[0]);
    return 2;
  }
  if (argc > 3 && strcmp(argv[3], "-v") == 0) verbose = 1;

  char request[MAX_PJL];
  int len = snprintf(request, sizeof(request), "\033%%-12345X%s\r\n\033%%-12345X", argv[2]);
  if (len < 0 || (size_t)len >= sizeof(request)) {
    fprintf(stderr, "PJL command too long (max %d bytes)\n", MAX_PJL - 16);
    return 2;
  }

  io_service_t device = IO_OBJECT_NULL;
  IOCFPlugInInterface **plug = NULL;
  IOUSBDeviceInterface **dev = NULL;
  io_iterator_t children = IO_OBJECT_NULL;
  int rc = 1;

  device = find_device(argv[1]);
  if (!device) { fprintf(stderr, "device '%s' not found\n", argv[1]); return 1; }

  SInt32 score = 0;
  if (IOCreatePlugInInterfaceForService(device, kIOUSBDeviceUserClientTypeID,
                                        kIOCFPlugInInterfaceID, &plug, &score) != KERN_SUCCESS) {
    fprintf(stderr, "cannot create device plugin\n");
    goto cleanup;
  }
  dev = iface_from(plug, kIOUSBDeviceInterfaceID);
  if (!dev) { fprintf(stderr, "no device interface\n"); goto cleanup; }

  IOReturn r = (*dev)->USBDeviceOpen(dev);
  if (r != kIOReturnSuccess) { fprintf(stderr, "open failed: 0x%x\n", r); goto cleanup; }

  UInt8 config = 0;
  r = (*dev)->GetConfiguration(dev, &config);
  if (r == kIOReturnSuccess && config != 1) {
    r = (*dev)->SetConfiguration(dev, 1);
    if (r != kIOReturnSuccess) { fprintf(stderr, "set config failed: 0x%x\n", r); goto cleanup_dev; }
  }

  if (IORegistryEntryGetChildIterator(device, kIOServicePlane, &children) != KERN_SUCCESS) {
    fprintf(stderr, "cannot enumerate interfaces\n");
    goto cleanup_dev;
  }

  int opened = 0;
  io_service_t child = IO_OBJECT_NULL;
  while ((child = IOIteratorNext(children))) {
    io_name_t cls;
    memset(cls, 0, sizeof(cls));
    if (IOObjectGetClass(child, cls) != KERN_SUCCESS) { IOObjectRelease(child); continue; }
    if (strcmp(cls, "IOUSBInterface") != 0 && strcmp(cls, "IOUSBHostInterface") != 0) {
      IOObjectRelease(child);
      continue;
    }

    IOCFPlugInInterface **iplug = NULL;
    IOUSBInterfaceInterface **intf = NULL;
    if (IOCreatePlugInInterfaceForService(child, kIOUSBInterfaceUserClientTypeID,
                                          kIOCFPlugInInterfaceID, &iplug, &score) != KERN_SUCCESS) {
      IOObjectRelease(child);
      continue;
    }
    intf = iface_from(iplug, kIOUSBInterfaceInterfaceID);
    if (!intf) {
      (*iplug)->Release(iplug);
      IOObjectRelease(child);
      continue;
    }

    // Check the class before opening: never grab the scanner side of an MFP.
    UInt8 cls_code = 0;
    (*intf)->GetInterfaceClass(intf, &cls_code);
    if (cls_code != PRINTER_CLASS) {
      if (verbose) fprintf(stderr, "skip interface class %u\n", cls_code);
      (*intf)->Release(intf);
      (*iplug)->Release(iplug);
      IOObjectRelease(child);
      continue;
    }

    r = (*intf)->USBInterfaceOpen(intf);
    if (r != kIOReturnSuccess) {
      fprintf(stderr, "interface open failed: 0x%x\n", r);
      (*intf)->Release(intf);
      (*iplug)->Release(iplug);
      IOObjectRelease(child);
      continue;
    }
    opened++;

    UInt8 n = 0, outPipe = 0, inPipe = 0;
    (*intf)->GetNumEndpoints(intf, &n);
    for (UInt8 p = 1; p <= n; p++) {
      UInt8 dir = 0, num = 0, type = 0, interval = 0;
      UInt16 mps = 0;
      if ((*intf)->GetPipeProperties(intf, p, &dir, &num, &type, &mps, &interval) != kIOReturnSuccess)
        continue;
      if (type == kIOUSBEndpointTypeBulk) {
        if (dir == kUSBOut) outPipe = p; else inPipe = p;
      }
    }

    if (outPipe && inPipe) {
      // Drop anything left over from a previous query.
      UInt8 junk[4096];
      for (int i = 0; i < 8; i++) {
        UInt32 jg = sizeof(junk);
        if ((*intf)->ReadPipeTO(intf, inPipe, junk, &jg, 200, 200) != kIOReturnSuccess || jg == 0)
          break;
      }

      r = (*intf)->WritePipeTO(intf, outPipe, request, (UInt32)len, 3000, 3000);
      if (r != kIOReturnSuccess) {
        fprintf(stderr, "write failed: 0x%x\n", r);
      } else {
        usleep(400000);
        int got_reply = 0;
        for (int attempt = 0; attempt < 16; attempt++) {
          UInt8 buf[4096];
          UInt32 got = sizeof(buf);
          r = (*intf)->ReadPipeTO(intf, inPipe, buf, &got, 4000, 4000);
          if (r == kIOReturnSuccess && got > 0) {
            fwrite(buf, 1, got, stdout);
            got_reply = 1;
          } else if (verbose) {
            fprintf(stderr, "read %d: 0x%x got=%u\n", attempt + 1, r, got);
          }
        }
        if (got_reply) rc = 0;
        else fprintf(stderr, "request sent but no reply received\n");
      }
    } else {
      fprintf(stderr, "printer interface has no bulk in/out pair\n");
    }

    (*intf)->USBInterfaceClose(intf);
    (*intf)->Release(intf);
    (*iplug)->Release(iplug);
    IOObjectRelease(child);
    break;
  }

  if (!opened) fprintf(stderr, "no printer-class interface opened\n");

cleanup_dev:
  if (dev) {
    (*dev)->USBDeviceClose(dev);
    (*dev)->Release(dev);
  }
cleanup:
  if (children != IO_OBJECT_NULL) IOObjectRelease(children);
  if (plug) (*plug)->Release(plug);
  if (device != IO_OBJECT_NULL) IOObjectRelease(device);
  return rc;
}
