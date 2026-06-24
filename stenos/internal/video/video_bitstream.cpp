#include "../../video_bitstream.h"

#include <mutex>
#include <atomic>

namespace stenos
{
    struct Stream
    {
        std::istream * iss;
        std::mutex * lock;

        std::unique_lock<std::mutex> guard()
        {
            if(lock)
                return  std::unique_lock<std::mutex>(*lock);
            return std::unique_lock<std::mutex>();
        }
    }
    int64_t read_stream(char* p, int64_t size, void* o)
    {
        Stream * s = (Stream*)o;
        auto guard = s->guard();

        s->iss->read(p,size);
        if(*s->iss)
            return s->iss->gcount();
        return -1;
    }
	int64_t seek_stream(int64_t pos, int w, void* o)
    {
        Stream * s = (Stream*)o;
        auto guard = s->guard();
        if(w == SEEK_SET)
            s->iss->seekg(pos);
        else if(w == SEEK_CUR)
            s->iss->seekg(pos, std::ios::cur);
        else
            s->iss->seekg(pos, std::ios::end);
        if(*s->iss)
            return s->iss->tellg();
        return -1;
    }
	int64_t tell_stream(void* o)
    {
        Stream * s = (Stream*)o;
        auto guard = s->guard();
        return s->iss->tellg();
    }

    stenos_input make_input(Stream *in)
    {
        stenos_input r;
        r.read = read_stream;
        r.seek = seek_stream;
        r.tell = tell_stream;
        r.opaque = in;
        return r;
    }

    using lock_type = std::recursive_mutex;

    class video_bitstream::PrivateData
    {
    public:
        lock_type lock;
        std::vector<int64_t, uint64_t> timestamps; // time -> block position in file
        std::fstream file;
        stenosv_compress * comp = nullptr;
        stenosv_decompress *dec = nullptr;
        int64_t dec_block_pos = 0;
        bitstream_parameters params;
        std::atomic<uint64_t> error{0};

        void sort_times()
        {
             // Sort timestamps
            std::sort(timestamps.begin(),timestamps.end(),[](const auto &l, const auto &r){return l.first < r.first;});
        }
    };

    #define RETURN_ERROR(error_code, ...) \
        do {d_data->error.store(error_code); return __VA_ARGS__;} while(0)

    video_bitstream::video_bitstream()
    {
        d_data = new PrivateData();
    }
    video_bitstream::video_bitstream(const char * filename, const bitstream_parameters & params)
    :video_bitstream()
    {
        open(filename, params);
    }
    video_bitstream::~video_bitstream() noexcept
    {
        close();
        delete d_data;
    }

    bool video_bitstream::open(const char * filename, const bitstream_parameters & params)
    {
        close();

        d_data->params = params;
        if(params.mode < ReadOnly || params.mode > ReadWriteTrunc) {
            close();
            RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER, false);
        }

        // open file
        if(params.mode == ReadOnly)
            d_data->file.open(filename, std::ios::in|std::ios::binary);
        if(params.mode == WriteOnly)
            d_data->file.open(filename, std::ios::out|std::ios::binary);
        if(params.mode == ReadWrite)
            d_data->file.open(filename, std::ios::in|std::ios::out|std::ios::binary|std::ios::ate);
        else
             d_data->file.open(filename, std::ios::in|std::ios::out|std::ios::binary|std::ios::trunc);

        if(!d_data->file) {
            close();
            RETURN_ERROR(STENOS_ERROR_INVALID_FILENAME, false);
        }

        if(params.mode == ReadOnly || params.mode == ReadWrite) {
            // Get file size
            d_data->file.seekg(0,std::ios::end);
            auto size = d_data->file.tellg();
            d_data->file.seekg(0);
            if(size) {
                // Read blocks
                std::vector<time_type> times(1000);
                Stream str{&d_data->file, nullptr};
                auto in = make_input(&str);

                uint64_t bs;
                auto h = stenosv_read_block_header_stream(&in,&bs);
                if(h.version == 0) {
                    close();
                    RETURN_ERROR(STENOS_ERROR_INVALID_INPUT,false);
                }

                while(d_data->file.tellg() < size) {
                    uint64_t pos = (uint64_t)d_data->file.tellg();
                    auto s = stenosv_extract_timestamps(&in, times.data(),times.size());
                    if(stenos_has_error(s)){
                        close();
                        RETURN_ERROR(STENOS_ERROR_INVALID_INPUT,false);
                    }

                    for(size_t i = 0; i < s; ++i) {
                        d_data->timestamps.push_back({times[i],pos});
                    }
                }

                // Check end of file
                if(d_data->file.tellg() != size){
                    close();
                    RETURN_ERROR(STENOS_ERROR_INVALID_INPUT,false);
                }

                // Sort timestamps
                d_data->sort_times();

                d_data->params.width = h.width;
                d_data->params.height = h.height;
                d_data->params.pixel_type = h.pixel_type;
            }
            else {
                // Empty file : Check parameters
                if(params.mode == ReadOnly ){
                    close(); // empty file
                    RETURN_ERROR(STENOS_ERROR_INVALID_INPUT,false);
                }

                if(d_data->params.width == 0 || d_data->params.height == 0 || d_data->params.pixel_type < 0) {
                    close()
                    RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER,false); // Invalid input parameters
                }
            }
        }
        else {
            if(d_data->params.width == 0 || d_data->params.height == 0 || d_data->params.pixel_type < 0) {
                close();
                RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER,false); // Invalid input parameters
            }
        }

        if(d_data->params.threads < 1)
            d_data->params.threads = 1;
        if(d_data->params.pixel_type < 0 || d_data->params.pixel_type > StenosFloat64) {
            close();
            RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER,false); // Invalid input parameters
        }

        return true;
    }

    void video_bitstream::close() noexcept
    {
        std::lock_guard<lock_type> lock(d_data->lock);
        d_data->file.close();
        d_data->timestamps.clear();
        if(d_data->comp) {
            stenosv_compress_destroy(d_data->comp);
            d_data->comp = nullptr;
        }
         if(d_data->dec) {
            stenosv_decompress_destroy(d_data->dec);
            d_data->dec = nullptr;
        }
        d_data->params = bitstream_parameters{};
        d_data->error.store(0);
    }

    bool video_bitstream::is_open() const noexcept
    {
        std::lock_guard<lock_type> lock(d_data->lock);
        return d_data->params.mode != NotOpen;
    }

    auto video_bitstream::paremeters() const noexcept -> bitstream_parameters
    {
        std::lock_guard<lock_type> lock(d_data->lock);
        return d_data->params;
    }
    auto video_bitstream::last_error() const noexcept -> error_type
    {
        return d_data->error.load();  
    }
    auto video_bitstream::count() const noexcept -> size_type
    {
        std::lock_guard<lock_type> lock(d_data->lock);
        return d_data->timestamps.size();
    }
    auto video_bitstream::time(size_t pos) const noexcept -> time_type
    {
        std::lock_guard<lock_type> lock(d_data->lock);
        if(pos >= d_data->timestamps.size()) 
            return invalid_time;
        return d_data->timestamps[pos].first;
    }
    auto video_bitstream::timestamps() const noexcept -> std::vector<time_type>
    {
        std::lock_guard<lock_type> lock(d_data->lock);
        std::vector<time_type> ret(d_data->timestamps.size());
        for(size_t i = 0; i < d_data->timestamps.size(); ++i)
            ret[i] = d_data->timestamps[i].first;
        return ret;
    }

    bool video_bitstream::add_images(const void * imgs, const int64_t *timestamps, size_t image_count)
    {
        std::lock_guard<lock_type> lock(d_data->lock);

        if(d_data->params.mode != WriteOnly &&  d_data->params.mode != ReadWrite && d_data->params.mode != ReadWriteTrunc)
            RETURN_ERROR(STENOS_ERROR_INVALID_IO,false);
        
        if(!d_data->comp) {
            d_data->comp = stenosv_compress_make((stenosv_pixel_type)d_data->params.pixel_type,d_data->params.width, d_data->params.height, d_data->params.maxGOP, d_data->params.device );
            if(!d_data->comp)
                RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER,false);

            stenosv_compress_set_threads(d_data->comp, (int)d_data->threads);
            stenosv_compress_set_clevel(d_data->comp, (int)d_data->level);
            stenosv_compress_set_max_error(d_data->comp, d_data->error);
        }

        uint8_t * pimg = (uint8_t*)img;
        for(size_t i = 0; i < image_count; ++i){

            auto r = stenosv_compress_add_image(d_data->comp, pimg, timestamps[i]);
            if(stenos_has_error(r))
                RETURN_ERROR(r,false);

            if(r == 1) {
                stenosv_payload p = stenosv_compress_payload(d_data->comp);
                d_data->file.seekp(0,std::ios::end);
                d_data->file.write((char*)p.data, p.size);
                if(!d_data->file)
                    RETURN_ERROR(STENOS_ERROR_INVALID_IO,false);
            }

            pimg += d_data->params.width * d_data->params.height * stenosv_sizeof_pixel_type((stenosv_pixel_type)d_data->params.pixel_type);
        }

        auto r = stenosv_compress_stop(d_data->comp);
        if(stenos_has_error(r))
                RETURN_ERROR(r,false);
            
        stenosv_payload p = stenosv_compress_payload(d_data->comp);
        if(p.size) {
            d_data->file.write((char*)p.data, p.size);
            if(!d_data->file)
                RETURN_ERROR(STENOS_ERROR_INVALID_IO,false);
        }

        d_data->sort_times();

        return true;
    }

    bool video_bitstream::read_image(void * img, size_t pos)
    {
        std::lock_guard<lock_type> lock(d_data->lock);

        if(d_data->params.mode != ReadOnly &&  d_data->params.mode != ReadWrite && d_data->params.mode != ReadWriteTrunc)
            RETURN_ERROR(STENOS_ERROR_INVALID_IO,false);

        if(pos >= d_data->timestamps.size())
            RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER,false);

        auto file_pos = d_data->timestamps[pos].second;
        if(!d_data->dec || d_data->dec_block_pos != file_pos) {

            if(d_data->dec ){
                stenosv_decompress_destroy(d_data->dec);
                d_data->dec = nullptr;
            } 
            // Open block
            d_data->file.seekg(file_pos);
            Stream stream{&d_data->file,nullptr};
            auto in = make_input(&stream);
            
            d_data->dec = stenosv_decompress_make_stream(in, d_data->thread);
            if(!d_data->dec)
                RETURN_ERROR(STENOS_ERROR_INVALID_INPUT,false);
            d_data->dec_block_pos = file_pos;
        }

        // get position in block;
        size_t start ;
        for(start = (int64_t)pos; start >= 0; --start){
            if(d_data->timestamps[start].second !=  file_pos)
                break;
        } 
        ++start;

        auto r = stenosv_decompress_read_image(d_data->dec, pos - start, 1, img  )
        if(stanos_has_error(r)){
            d_data->error.store(r);
            return false;
        } 
        return true;
    }

    bool video_bitstream::read_image_time(void * img, time_type time)
    {
        auto it = std::lower_boud(d_data->timestamps.begin(), d_data->timestamps.end(), time,[](const auto & l, const auto & r){return l.first < r;}  );
        if(it == d_data->timestamps.end()|| it->first != time )
            RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER,false);
        return read_image(void * img, (size_t)(it - d_data->timestamps.begin()));
    } 

    auto video_bitstream::extract_time_trace(const stenosv_trace_query & query, time_type first_time , time_type last_time ) -> time_trace
    {
        return{}; 
    } 

}