# Stenos video codec

`Stenosv` is a video codec designed to compress arithmetic images with or without controlled losses. It is heavily used at [CEA/IRFM](https://irfm.cea.fr/en/home/) to compress and archive infrared videos from the [WEST tokamak](https://irfm.cea.fr/en/presentation-of-west/), and successfully compressed terabytes of video data.

## Principle

As opposed to most video codecs specialized for natural colors, Stenosv works for videos containing mono-channel arithmetic images of integer or floating point pixels.
Note that Stenosv could compress colors by splitting color components (like RGB or YUV) into multiple video streams. However obtained compression ratio and visual quality would be lower than standard video codecs as it was not designed for such use case.
Instead, Stenosv lossy compression is tuned to retain the best image visual quality for a given maximum pixel error (controlled losses).

For an independant Group Of Picture (GOP), Stenosv compression scheme follows these steps:
-	The time trace of each pixel is extracted in order to have (width*height) temporal curves.
-	Each time trace is then decimated based on provided maximum error. The decimation does not create non existing points, but only keeps enough points to keep the final curve withing the envelope [original_curve - maximum_error, original_curve + maximum_error].
-	Decimated time traces are stacked together and compressde using the stenos library.

Stenosv is implemented in such way that it does not wait for the full GOP to be available to start these steps. The decimation process is incrementally computed at each new image, and only the compression stage is applied at GOP boundary.

Stenosv provides a GPU accelerated version using OpenCL to fasten the decimation stage. This acceleration makes real-time compression possible in some use-case, like WEST infrared videos (512*640 16 bits pixels at 50Hz).

Use the build option *STENOS_WITH_OPENCL=ON* to enable OpenCL support.

## Usage

Stenosv provides a C interface to compress/decompress independant GOPs. All functions are available in `stenos/stenos_video.h` header file.
Basic usage (C++):

```cpp


#include <iostream>
#include <cstddef>
#include <vector>
#include <stenos/stenos_video.h>

int main(int, char*[]) 
{
	////////////////////////////////
	// Video compression
	////////////////////////////////

	// List available GPU and select the first one (if any)
	int gpu_count = 0;
	int select_GPU = -1; // Compression device
	auto devices = stenosv_list_gpu_devices(&gpu_count);
	for (int i = 0; i < gpu_count; ++i) {
		auto& d = devices[i];
		std::cout << d.name << " " << d.vendor_id << " " << d.compute_units << " " << d.max_memory << std::endl;
		if(select_GPU == -1)
			select_GPU = 0;
	}

	// Create synthetic images
	using pixel_type = std::uint16_t;
	using image_type = std::vector<pixel_type>;
	using image_stack = std::vector<image_type>;
	
	size_t width = 640; //image width
	size_t height = 512; // image height
	double error = 0.; // maximum error
	
	// Create a stack of 100 images
	image_stack images(100);
	for(size_t i = 0; i < images.size(); ++i)
		images[i] = image_type(width * height, (std::uint16_t)i);
		
	// Output bytestream
	std::vector<char> compressed;
		
	// Build codec context with a GOP of 16 images
	auto codec = stenosv_compress_make(StenosUInt16, width, height, error, 16, select_GPU);
	stenosv_compress_set_clevel(codec,9); // Set the compression level, from 0 (no compression) to 9 (maximum compression)
	stenosv_compress_set_threads(codec,4); // Set the number of threads

	// Write frames images
	for (size_t i = 0; i < images.size(); ++i) {
		// Add image to context
		auto r = stenosv_compress_add_image(codec, images[i].data(), (int64_t)i);
		// Check fo error
		if(stenos_has_error(r)) 
			return -1;
		if (r == 1) {
			// A compressed output is available
			auto buffer = stenosv_compress_payload(codec);
			char * data = (char*)buffer.data;
			compressed.insert(compressed.end(), data, data + buffer.size);
		}
		std::cout << "Finished image "<<i<<" compression" << std::endl;
	}
	// Finish compression and destroy codec context
	stenosv_compress_stop(codec);
	auto buffer = stenosv_compress_payload(codec);
	char * data = (char*)buffer.data;
	compressed.insert(compressed.end(), data, data + buffer.size);
	stenosv_compress_destroy(codec);
	
	std::cout << std::endl << "Start video decompression"<<std::endl;
	
	
	////////////////////////////////
	// Video decompression
	////////////////////////////////
	
	stenosv_payload bytes{compressed.data(), compressed.size()};
	size_t im_count = 0;
	while(true) {
		// Create decompression context with 4 threads
		auto decomp = stenosv_decompress_make_buffer(bytes, 4);
		assert(decomp);
		
		uint64_t GOP_bytes = 0;
		stenosv_block_header h = stenosv_read_block_header_buffer(bytes, &GOP_bytes);
		assert(h.pixel_type == StenosUInt16);
		assert(h.width == width);
		assert(h.height == height);
		assert(h.count <= 16);
		
		// Decompress images in this GOP
		for(size_t i = 0; i < h.count; ++i, ++im_count) {
			image_type img(width*height);
			auto r = stenosv_decompress_read_image(decomp, i, 1, img.data());
			assert(!stenos_has_error(r));
			std::cout << "Image " << im_count <<" pixel: " << img[0] << std::endl;
		}
		
		stenosv_decompress_destroy(decomp);
		
		bytes.data = (char*)bytes.data + GOP_bytes;
		bytes.size -= GOP_bytes;
		if(bytes.size == 0)
			break;
	}
	
	return 0;
}

```

Stenosv also provides a simplified C API to read bytestreams (succession of GOPs).

## Time trace extraction

A strength of Stenov codec is the ability to extract time traces inside Regions Of Interest (ROIs) in a very fast way. Indeed, the compressed bytestream contains a stack of independant decimated pixel time traces. 
Therefore, extracting a ROI time trace is *just* a matter of isolating corresponding pixel time traces and decompress them.

Stenosv provides a C API to extract temporal statistics (minimum, maximum, mean, variance) inside a ROI from a compressed GOP.
For more details, see the documentation for functions `stenosv_init_trace_query`, `stenosv_init_trace_result`, `stenosv_extract_time_trace` and `stenosv_bytestream_extract_time_trace`.






