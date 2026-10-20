R"CLC(
// Slope-interval decimation, OpenCL 1.2. Paired with TimeTraceCompressOpenCL.h.
// Best-effort numerical bounds; no approximate-equality tolerance is applied.
// Do not build with -cl-fast-relaxed-math / -cl-finite-math-only: NaN and
// infinity samples must be preserved. All slope arithmetic uses double.
#if defined(cl_khr_fp64)
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
#elif defined(cl_amd_fp64)
#pragma OPENCL EXTENSION cl_amd_fp64 : enable
#endif
#pragma OPENCL FP_CONTRACT OFF

#define N(name, type) name##type
#define GENERATE_FUNCTIONS(TYPE) \
typedef struct __attribute__((packed)) N(CLPixelType, TYPE) \
{ \
    TYPE value; \
    ushort index; \
} N(CLPixelType, TYPE); \
 \
/* Element offsets, not device pointers: identical host/device representation. */ \
typedef struct N(FIFO, TYPE) \
{ \
    ulong buffer; \
    ulong data; \
    int size; \
    int reserved; \
} N(FIFO, TYPE); \
 \
typedef struct N(CLPixelData, TYPE) \
{ \
    N(FIFO, TYPE) points; \
    N(FIFO, TYPE) last_pixels; \
    int candidate; \
    int pos; \
    int finished; \
    int reserved; \
    double max_slope; \
    double min_slope; \
} N(CLPixelData, TYPE); \
 \
void N(clear_slopes, TYPE)(global N(CLPixelData, TYPE)* d) \
{ \
    d->max_slope = -(double)INFINITY; \
    d->min_slope = (double)INFINITY; \
} \
 \
void N(emit_candidate, TYPE)(global N(CLPixelData, TYPE)* d, \
    global N(CLPixelType, TYPE)* pixels) \
{ \
    pixels[d->points.data + (ulong)d->points.size++] = \
        pixels[d->last_pixels.data + (ulong)d->candidate]; \
} \
 \
void N(erase_pending, TYPE)(global N(CLPixelData, TYPE)* d, int count) \
{ \
    d->last_pixels.data += (ulong)count; \
    d->last_pixels.size -= count; \
} \
 \
/* Strictly increasing int64 nanosecond timestamps are checked on the host. \
   Unsigned subtraction avoids signed overflow, even across the signed origin. */ \
double N(elapsed, TYPE)(long later, long earlier) \
{ \
    return (double)((ulong)later - (ulong)earlier); \
} \
 \
void N(advance_min_max, TYPE)(global N(CLPixelData, TYPE)* d, \
    global N(CLPixelType, TYPE)* pixels, double error, global const long* times) \
{ \
    N(CLPixelType, TYPE) first = pixels[d->last_pixels.data]; \
    N(CLPixelType, TYPE) val = pixels[d->last_pixels.data + (ulong)d->pos]; \
    double dt = N(elapsed, TYPE)(times[val.index], times[first.index]); \
    double s = ((double)val.value - (double)first.value) / dt; \
 \
    if (d->pos == 1) { \
        d->candidate = d->pos; \
    } else if (d->min_slope < s || s < d->max_slope) { \
        /* An invalid endpoint can be followed by a valid one. Only restart \
           when the intersection of the previous error intervals is empty. */ \
        if (d->min_slope < d->max_slope) { \
            N(emit_candidate, TYPE)(d, pixels); \
            N(erase_pending, TYPE)(d, d->candidate); \
            d->pos = 0; /* The caller increments this to 1. */ \
            d->candidate = -1; \
            N(clear_slopes, TYPE)(d); \
            first = pixels[d->last_pixels.data]; \
            val = pixels[d->last_pixels.data + 1]; \
        } \
    } else { \
        d->candidate = d->pos; \
    } \
 \
    dt = N(elapsed, TYPE)(times[val.index], times[first.index]); \
    double upper = ((double)val.value + error - (double)first.value) / dt; \
    double lower = ((double)val.value - error - (double)first.value) / dt; \
    d->min_slope = fmin(d->min_slope, upper); \
    d->max_slope = fmax(d->max_slope, lower); \
} \
 \
/* Flush exactly one finite run. Repeated calls are harmless. */ \
void N(finish_run, TYPE)(global N(CLPixelData, TYPE)* d, \
    global N(CLPixelType, TYPE)* pixels, double error, global const long* times) \
{ \
    if (d->finished) return; \
    if (d->pos >= d->last_pixels.size - 1 && d->candidate != -1) { \
        N(emit_candidate, TYPE)(d, pixels); \
        N(erase_pending, TYPE)(d, d->candidate); \
        d->pos = 1; \
        d->candidate = -1; \
        N(clear_slopes, TYPE)(d); \
    } \
    for (; d->pos < d->last_pixels.size; ++d->pos) { \
        N(advance_min_max, TYPE)(d, pixels, error, times); \
        if (d->pos == d->last_pixels.size - 1 && d->candidate != -1) { \
            N(emit_candidate, TYPE)(d, pixels); \
            N(erase_pending, TYPE)(d, d->candidate); \
            d->pos = 0; \
            d->candidate = -1; \
            N(clear_slopes, TYPE)(d); \
        } \
    } \
    N(erase_pending, TYPE)(d, d->last_pixels.size); \
    d->pos = 0; \
    d->candidate = -1; \
    d->finished = 1; \
    N(clear_slopes, TYPE)(d); \
} \
 \
kernel void N(advance_pixel_data_, TYPE)(global N(CLPixelData, TYPE)* states, \
    global N(CLPixelType, TYPE)* pixels, global const TYPE* img, \
    int pos, double error, int GOP, global const long* times) \
{ \
    const size_t idx = get_global_id(0); \
    global N(CLPixelData, TYPE)* d = states + idx; \
    const ulong base = (ulong)idx * (ulong)GOP * 2; \
    if (pos == 0) { \
        d->points.buffer = d->points.data = base + (ulong)GOP; \
        d->points.size = d->points.reserved = 0; \
        d->last_pixels.buffer = d->last_pixels.data = base; \
        d->last_pixels.size = d->last_pixels.reserved = 0; \
        d->candidate = -1; \
        d->pos = 0; \
        d->finished = 1; \
        d->reserved = 0; \
        N(clear_slopes, TYPE)(d); \
    } \
 \
    N(CLPixelType, TYPE) pix; \
    pix.value = img[idx]; \
    pix.index = (ushort)pos; \
    /* Preserve the entire raw trace, including non-finite samples. */ \
    pixels[base + (ulong)pos] = pix; \
 \
    if (!isfinite((double)pix.value)) { \
        N(finish_run, TYPE)(d, pixels, error, times); \
        pixels[d->points.data + (ulong)d->points.size++] = pix; \
        return; \
    } \
    if (d->finished) { \
        d->last_pixels.data = base + (ulong)pos; \
        d->last_pixels.size = 1; \
        pixels[d->points.data + (ulong)d->points.size++] = pix; \
        d->candidate = -1; \
        d->pos = 1; \
        d->finished = 0; \
    } else { \
        ++d->last_pixels.size; \
        N(advance_min_max, TYPE)(d, pixels, error, times); \
        ++d->pos; \
    } \
} \
 \
kernel void N(insert_key_frame_, TYPE)(global N(CLPixelData, TYPE)* states, \
    global N(CLPixelType, TYPE)* pixels, int pos, double error, \
    global const long* times) \
{ \
    if (pos > 0) N(finish_run, TYPE)(states + get_global_id(0), pixels, error, times); \
}

GENERATE_FUNCTIONS(char)
GENERATE_FUNCTIONS(uchar)
GENERATE_FUNCTIONS(short)
GENERATE_FUNCTIONS(ushort)
GENERATE_FUNCTIONS(int)
GENERATE_FUNCTIONS(uint)
GENERATE_FUNCTIONS(long)
GENERATE_FUNCTIONS(ulong)
GENERATE_FUNCTIONS(float)
GENERATE_FUNCTIONS(double)

#undef GENERATE_FUNCTIONS
#undef N
)CLC"
