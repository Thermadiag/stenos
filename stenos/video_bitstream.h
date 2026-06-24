#ifndef STENOS_VIDEO_BITSTREAM_H
#define STENOS_VIDEO_BITSTREAM_H

#include <type_traits>
#include <cstdint>
#include <fstream>
#include <vector>

#include "stenos_video.h"

namespace stenos
{
    struct bitstream_parameters
    {
        unsigned mode = 0; //NotOpen
        int pixel_type = -1; // default value in read-only mode (retrieve from file)
        unsigned maxGOP = 50;
        unsigned width = 0; // default value in read-only mode (retrieve from file)
        unsigned height = 0; // default value in read-only mode (retrieve from file)
        unsigned level = 1;
        unsigned threads = 1;
        int device = stenosv_default_gpu_device();
        double error = 0;
    }

    class video_bitstream
    {
    public:
        using time_type = int64_t;
        using size_type = size_t;
        using error_type = uint64_t;
        using trace_value = std::pair<time_type,double>;
        using time_trace = std::vector<trace_value>;

        static constexpr time_type invalid_time = std::numeric_limits<time_type>::max();

        enum Mode
        {
            NotOpen,
            ReadOnly,   // Read only mode
            WriteOnly,  // Write only mode, truncate file
            ReadWrite,  // Read and write mode, append to existing file
            ReadWriteTrunc // Read and write mode, truncate file
        }

        video_bitstream();
        video_bitstream(const char * filename, const bitstream_parameters & params);
        ~video_bitstream() noexcept;

        bool open(const char * filename, const bitstream_parameters & params);
        void close() noexcept;

        bool is_open() const noexcept;
        auto paremeters() const noexcept -> bitstream_parameters;
        auto last_error() const noexcept -> error_type;
        auto count() const noexcept -> size_type;
        auto time(size_t pos) const noexcept -> time_type;
        auto timestamps() const noexcept -> std::vector<time_type>;

        bool add_images(const void * imgs, const int64_t * timestamps, size_t image_count);

        bool read_image(void * img, size_t pos);
        bool read_image_time(void * img, time_type time);

        auto extract_time_trace(const stenosv_trace_query & query, time_type first_time = invalid_time, time_type last_time = invalid_time) -> time_trace;

    private:
        class PrivateData;
        PrivateData * d_data;
    };

}

#endif
