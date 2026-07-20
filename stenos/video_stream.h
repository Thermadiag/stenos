#ifndef STENOS_VIDEO_STREAM_H
#define STENOS_VIDEO_STREAM_H

#include <type_traits>
#include <cstdint>
#include <fstream>
#include <vector>
#include <limits>
#include <utility>
#include <atomic>

#include "stenos_video.h"

namespace stenos
{
	struct stream_parameters
	{
		unsigned mode = 0;   // NotOpen
		int pixel_type = -1; // default value in read-only mode (retrieve from file)
		unsigned maxGOP = 50;
		unsigned width = 0;  // default value in read-only mode (retrieve from file)
		unsigned height = 0; // default value in read-only mode (retrieve from file)
		unsigned level = 1;
		unsigned threads = 1;
		int device = -1; // stenosv_default_gpu_device();
		double error = 0;
	};

    class STENOS_EXPORT video_stream
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
        };

        video_stream();
        video_stream(const char * filename, const stream_parameters & params);
        virtual ~video_stream() noexcept;

        bool open(const char * filename, const stream_parameters & params);
        void close() noexcept;

        bool is_open() const noexcept;
        auto paremeters() const noexcept -> stream_parameters;
        auto last_error() const noexcept -> error_type;
        auto count() const noexcept -> size_type;
        auto block_count() const noexcept -> size_type;
        auto time(size_t pos) const noexcept -> time_type;
	    bool has_time(time_type) const noexcept;
	    bool has_times(time_type* start, time_type* end) const noexcept;
        auto timestamps() const noexcept -> std::vector<time_type>;

        bool add_images(const void * imgs, const int64_t * timestamps, size_t image_count);

        bool read_image(size_t pos, void* img);
	    bool read_image_time(time_type time, void* img);
         
        auto extract_time_trace(const stenosv_trace_query& query, stenosv_trace_result& out, time_type first_time = invalid_time, time_type last_time = invalid_time, std::atomic<size_t> * progress = nullptr) -> size_t;

    protected:
	    virtual bool write_file_header(std::ostream& out) { return true; }
	    virtual bool read_file_header(std::istream& in) { return true; }
	    virtual stenosv_block_header read_block_header(stenos_input* in) { return stenosv_read_block_header_stream(in, nullptr); } 

    private:
	    void closeNoLock();
	    void resetError();
	    bool readImageNoLock(size_t pos, void* img);

        class PrivateData;
        PrivateData * d_data;
    };

}

#endif
