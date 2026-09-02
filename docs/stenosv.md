# Stenos video codec

Stenosv is video codec designed to compress arithmetic images with or without controlled losses. It is heavily used at [CEA/IRFM](https://irfm.cea.fr/en/home/) to compress and archive infrared videos from the [WEST tokamak](https://irfm.cea.fr/en/presentation-of-west/), and successfully compressed terabytes of video data.

## Principle

As opposed to most video codecs specialized for natural colors, Stenosv works for videos containing mono-channel arithmetic images of integer or floating point pixels.
Note that Stenosv could compress colors by splitting color components (like RGB or YUV) into multiple video streams. However obtained compression ratio and visual quality would be lower than standard video codecs as it was not designed for such use case.
Instead, Stenosv lossy compression is not tuned to retain the best image visual quality, but to 