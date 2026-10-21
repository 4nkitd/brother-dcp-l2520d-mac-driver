// Minimal CUPS raster generator for tests: writes one 1-bit page with a
// chosen size name and geometry so page-size detection can be exercised.
//
//   mkraster out.ras <PageSizeName> <width_pt> <height_pt> [dpi] [--int-only]
//
// Pass an empty name ("") to simulate rasters where the application supplied
// geometry but no recognised size name. --int-only leaves the float
// cupsPageSize fields at zero so the integer PageSize fallback is exercised.
#include <cups/raster.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_POINTS 2000.0    /* 27.7 in, larger than any supported media */
#define MAX_PIXELS 40000.0   /* guard against overflow at 1200 dpi */

static int parse_double(const char *s, double *out) {
  char *end = NULL;
  errno = 0;
  double v = strtod(s, &end);
  if (errno != 0 || end == s || *end != '\0' || !isfinite(v)) return 0;
  *out = v;
  return 1;
}

int main(int argc, char **argv) {
  if (argc < 5) {
    fprintf(stderr, "usage: %s out.ras name width_pt height_pt [dpi]\n", argv[0]);
    return 2;
  }

  const char *path = argv[1];
  const char *name = argv[2];
  if (strlen(name) >= 64) { fprintf(stderr, "size name too long\n"); return 2; }

  double width_pt = 0, height_pt = 0, dpi_arg = 600;
  if (!parse_double(argv[3], &width_pt) || !parse_double(argv[4], &height_pt)) {
    fprintf(stderr, "width and height must be numbers\n");
    return 2;
  }

  int int_only = 0;
  for (int i = 5; i < argc; i++) {
    if (strcmp(argv[i], "--int-only") == 0) int_only = 1;
    else if (!parse_double(argv[i], &dpi_arg)) {
      fprintf(stderr, "dpi must be a number\n");
      return 2;
    }
  }
  if (width_pt <= 0 || height_pt <= 0 || width_pt > MAX_POINTS || height_pt > MAX_POINTS) {
    fprintf(stderr, "width and height must be > 0 and <= %.0f points\n", MAX_POINTS);
    return 2;
  }
  if (dpi_arg < 72 || dpi_arg > 1200) {
    fprintf(stderr, "dpi must be between 72 and 1200\n");
    return 2;
  }

  double px_w = width_pt * dpi_arg / 72.0;
  double px_h = height_pt * dpi_arg / 72.0;
  if (px_w > MAX_PIXELS || px_h > MAX_PIXELS) {
    fprintf(stderr, "page too large at this resolution\n");
    return 2;
  }

  cups_page_header2_t h;
  memset(&h, 0, sizeof(h));
  h.HWResolution[0] = (unsigned)dpi_arg;
  h.HWResolution[1] = (unsigned)dpi_arg;
  h.PageSize[0] = (unsigned)(width_pt + 0.5);
  h.PageSize[1] = (unsigned)(height_pt + 0.5);
  h.cupsPageSize[0] = int_only ? 0.0f : (float)width_pt;
  h.cupsPageSize[1] = int_only ? 0.0f : (float)height_pt;
  h.cupsWidth = (unsigned)px_w;
  h.cupsHeight = (unsigned)px_h;
  h.cupsBitsPerColor = 1;
  h.cupsBitsPerPixel = 1;
  h.cupsBytesPerLine = (h.cupsWidth + 7) / 8;
  h.cupsColorSpace = CUPS_CSPACE_K;
  h.cupsColorOrder = CUPS_ORDER_CHUNKED;
  h.cupsCompression = 0;
  h.NumCopies = 1;
  strncpy(h.MediaType, "PLAIN", sizeof(h.MediaType) - 1);
  strncpy(h.cupsPageSizeName, name, sizeof(h.cupsPageSizeName) - 1);

  int rc = 1;
  FILE *fp = NULL;
  cups_raster_t *ras = NULL;
  unsigned char *line = NULL;

  fp = fopen(path, "wb");
  if (!fp) { perror(path); goto done; }

  ras = cupsRasterOpen(fileno(fp), CUPS_RASTER_WRITE);
  if (!ras) { fprintf(stderr, "cannot open raster writer\n"); goto done; }

  if (!cupsRasterWriteHeader2(ras, &h)) {
    fprintf(stderr, "header write failed\n");
    goto done;
  }

  line = calloc(1, h.cupsBytesPerLine);
  if (!line) { fprintf(stderr, "out of memory\n"); goto done; }

  for (unsigned y = 0; y < h.cupsHeight; y++) {
    // A solid black band across the middle so the page is never empty.
    memset(line, 0, h.cupsBytesPerLine);
    if (y > h.cupsHeight / 2 && y < h.cupsHeight / 2 + 40)
      memset(line, 0xff, h.cupsBytesPerLine);
    if (!cupsRasterWritePixels(ras, line, h.cupsBytesPerLine)) {
      fprintf(stderr, "pixel write failed at row %u\n", y);
      goto done;
    }
  }

  rc = 0;

done:
  free(line);
  if (ras) cupsRasterClose(ras);
  if (fp && fclose(fp) != 0) {
    perror("close");
    rc = 1;
  }
  return rc;
}
