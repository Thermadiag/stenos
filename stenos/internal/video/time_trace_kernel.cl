R"(


#define GENERATE_NAME(name,type) name##type

#define GENERATE_FUNCTIONS(type, float_type) \
typedef struct __attribute__ ((packed)) GENERATE_NAME(CLPixelType,type)\
{\
	type value;\
	unsigned short index;\
} GENERATE_NAME(CLPixelType,type);\
\
typedef struct GENERATE_NAME(FIFO,type)\
{\
	global GENERATE_NAME(CLPixelType,type)* buffer;\
	global GENERATE_NAME(CLPixelType,type)* data;\
	int size;\
} GENERATE_NAME(FIFO,type);\
\
typedef struct GENERATE_NAME(CLPixelData,type)\
{\
	GENERATE_NAME(FIFO,type) points;\
	GENERATE_NAME(FIFO,type) last_pixels;\
	int candidate;\
	int pos;\
	int start;\
} GENERATE_NAME(CLPixelData,type);\
\
void GENERATE_NAME(init_FIFO,type)(global GENERATE_NAME(FIFO,type)* fifo, global GENERATE_NAME(CLPixelType,type) * buf, const int size)\
{\
	fifo->buffer = fifo->data = buf;\
	fifo->size = 0;\
}\
\
void GENERATE_NAME(append_FIFO,type)(global GENERATE_NAME(FIFO,type)* fifo, const GENERATE_NAME(CLPixelType,type) val)\
{\
	fifo->data[fifo->size] = val;\
	++fifo->size;\
}\
\
void GENERATE_NAME(erase_front_FIFO,type)(global GENERATE_NAME(FIFO,type)* fifo, const int count)\
{\
	fifo->size -= count;\
	fifo->data += count;\
}\
\
bool GENERATE_NAME(dcompare,type)(const float_type p1, const float_type p2)\
{\
	return (fabs(p1 - p2) * 100000.f <= fmin(fabs(p1), fabs(p2)));\
}\
bool GENERATE_NAME(inf_equal,type)(const float_type a, const float_type b)\
{\
	if (GENERATE_NAME(dcompare,type)(a, b))\
		return true;\
	return a < b;\
}\
\
float_type GENERATE_NAME(check_candidate_min_max,type)(global GENERATE_NAME(FIFO,type)* last_pixels, const int end, const float_type s, const float_type error2, global const double * times) \
{\
	float_type beta;\
	global GENERATE_NAME(CLPixelType,type) *p, *pend;\
	float_type error_max = -HUGE_VAL;\
	if (1 == end)\
		return error_max;\
\
	beta = (float_type)last_pixels->data[0].value - s * (float_type)times[last_pixels->data[0].index];\
	p = last_pixels->data + 1;\
	pend = last_pixels->data + end;\
\
	for (; p < pend; ++p) {\
		float_type theoric_y = s * (float_type)times[p->index] + beta;\
		float_type err = fabs((float_type)p->value - theoric_y);\
		error_max = fmax(error_max, err);\
	}\
	return error_max;\
}\
\
float_type GENERATE_NAME(slope,type)(const float_type l, const float_type r, const float_type dist)\
{\
	return (r - l) / dist;\
}\
\
void GENERATE_NAME(advance_min_max,type)(global GENERATE_NAME(CLPixelData,type)* d, const double _error, global const double * times)\
{\
	const float_type error = (float_type)_error;\
	float_type error2 = error * 2.00001f;\
	GENERATE_NAME(CLPixelType,type) val = d->last_pixels.data[d->pos];\
\
	/* compute GENERATE_NAME(slope,type)*/\
	float_type s = GENERATE_NAME(slope,type)((float_type)d->last_pixels.data[0].value, (float_type)val.value, (float_type)times[val.index] - (float_type)times[d->last_pixels.data[0].index]);\
\
	/* check points before*/\
	float_type error_max = HUGE_VAL;\
	if (error != 0 ) /* if error is 0, it is more efficient to just NOT try to remove points*/\
		error_max = GENERATE_NAME(check_candidate_min_max,type)(&d->last_pixels, d->pos, s, error2, times);\
\
	if (error_max > error2) {\
		/* stop here*/\
		if (d->candidate == INT_MAX)\
			d->candidate = d->pos;\
\
		GENERATE_NAME(append_FIFO,type)(&d->points, d->last_pixels.data[d->candidate]);\
		GENERATE_NAME(erase_front_FIFO,type)(&d->last_pixels, d->candidate);\
		d->pos = 0;\
		d->candidate = INT_MAX;\
	}\
	else if (GENERATE_NAME(inf_equal,type)(error_max, error)) {\
		d->candidate = d->pos;\
	}\
}\
\
void GENERATE_NAME(init_pixel_data,type)(global GENERATE_NAME(CLPixelData,type)* pixel_data, global GENERATE_NAME(CLPixelType,type)* all_pixels, global const type * img, const int _GOP)\
{\
	size_t idx = get_global_id(0);\
	size_t GOP = (size_t)_GOP;\
	global GENERATE_NAME(CLPixelData,type)* d = pixel_data + idx;\
	GENERATE_NAME(CLPixelType,type) pix;\
\
	d->candidate = INT_MAX;\
\
	GENERATE_NAME(init_FIFO,type)(&d->last_pixels, all_pixels + idx * GOP* 2, 0);\
	GENERATE_NAME(init_FIFO,type)(&d->points, all_pixels + idx * GOP * 2 +  GOP, 0);\
\
	pix.value = img[idx];\
	pix.index = 0;\
	GENERATE_NAME(append_FIFO,type)(&d->points, pix);\
	GENERATE_NAME(append_FIFO,type)(&d->last_pixels, pix);\
\
	d->pos = 1;\
	d->start = 0;\
}\
\
void GENERATE_NAME(insert_key_frame_internal,type)(global GENERATE_NAME(CLPixelData,type)* pixel_data, const int pos, const double error, global const double* times)\
{\
	global GENERATE_NAME(CLPixelData,type)* d = pixel_data + get_global_id(0);\
	if (d->pos >= d->last_pixels.size - 1) {\
		/* we reach the end*/\
		if (d->candidate != INT_MAX) {\
			/* add previous candidate*/\
			GENERATE_NAME(append_FIFO,type)(&d->points, d->last_pixels.data[d->candidate]);\
			GENERATE_NAME(erase_front_FIFO,type)(&d->last_pixels, d->candidate);\
			d->pos = 0;\
			d->candidate = INT_MAX;\
		}\
	}\
\
	for (; d->pos != d->last_pixels.size; ++d->pos) {\
		GENERATE_NAME(advance_min_max,type)(d, error,times);\
		if (d->pos >= d->last_pixels.size - 1) {\
			/* we reach the end*/\
			if (d->candidate != INT_MAX) {\
				/* add previous candidate*/\
				if (d->points.data[d->points.size - 1].index != pos - 1) {\
					GENERATE_NAME(append_FIFO,type)(&d->points, d->last_pixels.data[d->candidate]);\
					GENERATE_NAME(erase_front_FIFO,type)(&d->last_pixels, d->candidate);\
					d->pos = 0;\
					d->candidate = INT_MAX;\
				}\
			}\
		}\
	}\
\
	/* Add last point*/\
	if (d->points.data[d->points.size-1].index != pos - 1)\
		GENERATE_NAME(append_FIFO,type)(&d->points, d->last_pixels.data[d->last_pixels.size-1]);		   \
}\
\
void GENERATE_NAME(advance_pixel_data_internal,type)( global GENERATE_NAME(CLPixelData,type)* pixel_data, global GENERATE_NAME(CLPixelType,type)* all_pixels, global const type* img, const int pos, const double error, const int GOP, global const double* times)\
{\
	int idx = (int)get_global_id(0);\
	global GENERATE_NAME(CLPixelData,type)* d = pixel_data + idx;\
	GENERATE_NAME(CLPixelType,type) pix;\
\
	if (pos == 0) {\
		return GENERATE_NAME(init_pixel_data,type)(pixel_data, all_pixels, img, (size_t)GOP);\
	}\
\
	pix.value = img[idx];\
	pix.index = pos;\
\
	GENERATE_NAME(append_FIFO,type)( &d->last_pixels, pix);\
	GENERATE_NAME(advance_min_max,type)(d, error, times);\
	d->pos++;\
\
}\



GENERATE_FUNCTIONS(char,float)
kernel void insert_key_frame_char(global GENERATE_NAME(CLPixelData,char)* pixel_data, const int pos, const double error, global const double* times)
{
	GENERATE_NAME(insert_key_frame_internal,char)(pixel_data, pos, error, times);
}
kernel void advance_pixel_data_char( global GENERATE_NAME(CLPixelData,char)* pixel_data, global GENERATE_NAME(CLPixelType,char)* all_pixels, global const char* img, const int pos, const double error, const int GOP, global const double* times)
{
	GENERATE_NAME(advance_pixel_data_internal,char)(pixel_data,all_pixels,img,pos,error,GOP,times);
}


GENERATE_FUNCTIONS(uchar,float)
kernel void insert_key_frame_uchar(global GENERATE_NAME(CLPixelData,uchar)* pixel_data, const int pos, const double error, global const double* times)
{
	GENERATE_NAME(insert_key_frame_internal,uchar)(pixel_data, pos, error, times);
}
kernel void advance_pixel_data_uchar( global GENERATE_NAME(CLPixelData,uchar)* pixel_data, global GENERATE_NAME(CLPixelType,uchar)* all_pixels, global const uchar* img, const int pos, const double error, const int GOP, global const double* times)
{
	GENERATE_NAME(advance_pixel_data_internal,uchar)(pixel_data,all_pixels,img,pos,error,GOP,times);
}


GENERATE_FUNCTIONS(short,float)
kernel void insert_key_frame_short(global GENERATE_NAME(CLPixelData,short)* pixel_data, const int pos, const double error, global const double* times)
{
	GENERATE_NAME(insert_key_frame_internal,short)(pixel_data, pos, error, times);
}
kernel void advance_pixel_data_short( global GENERATE_NAME(CLPixelData,short)* pixel_data, global GENERATE_NAME(CLPixelType,short)* all_pixels, global const short* img, const int pos, const double error, const int GOP, global const double* times)
{
	GENERATE_NAME(advance_pixel_data_internal,short)(pixel_data,all_pixels,img,pos,error,GOP,times);
}


GENERATE_FUNCTIONS(ushort,float)
kernel void insert_key_frame_ushort(global GENERATE_NAME(CLPixelData,ushort)* pixel_data, const int pos, const double error, global const double* times)
{
	GENERATE_NAME(insert_key_frame_internal,ushort)(pixel_data, pos, error, times);
}
kernel void advance_pixel_data_ushort( global GENERATE_NAME(CLPixelData,ushort)* pixel_data, global GENERATE_NAME(CLPixelType,ushort)* all_pixels, global const ushort* img, const int pos, const double error, const int GOP, global const double* times)
{
	GENERATE_NAME(advance_pixel_data_internal,ushort)(pixel_data,all_pixels,img,pos,error,GOP,times);
}


GENERATE_FUNCTIONS(int,double)
kernel void insert_key_frame_int(global GENERATE_NAME(CLPixelData,int)* pixel_data, const int pos, const double error, global const double* times)
{
	GENERATE_NAME(insert_key_frame_internal,int)(pixel_data, pos, error, times);
}
kernel void advance_pixel_data_int( global GENERATE_NAME(CLPixelData,int)* pixel_data, global GENERATE_NAME(CLPixelType,int)* all_pixels, global const int* img, const int pos, const double error, const int GOP, global const double* times)
{
	GENERATE_NAME(advance_pixel_data_internal,int)(pixel_data,all_pixels,img,pos,error,GOP,times);
}


GENERATE_FUNCTIONS(uint,double)
kernel void insert_key_frame_uint(global GENERATE_NAME(CLPixelData,uint)* pixel_data, const int pos, const double error, global const double* times)
{
	GENERATE_NAME(insert_key_frame_internal,uint)(pixel_data, pos, error, times);
}
kernel void advance_pixel_data_uint( global GENERATE_NAME(CLPixelData,uint)* pixel_data, global GENERATE_NAME(CLPixelType,uint)* all_pixels, global const uint* img, const int pos, const double error, const int GOP, global const double* times)
{
	GENERATE_NAME(advance_pixel_data_internal,uint)(pixel_data,all_pixels,img,pos,error,GOP,times);
}


GENERATE_FUNCTIONS(long,double)
kernel void insert_key_frame_long(global GENERATE_NAME(CLPixelData,long)* pixel_data, const int pos, const double error, global const double* times)
{
	GENERATE_NAME(insert_key_frame_internal,long)(pixel_data, pos, error, times);
}
kernel void advance_pixel_data_long( global GENERATE_NAME(CLPixelData,long)* pixel_data, global GENERATE_NAME(CLPixelType,long)* all_pixels, global const long* img, const int pos, const double error, const int GOP, global const double* times)
{
	GENERATE_NAME(advance_pixel_data_internal,long)(pixel_data,all_pixels,img,pos,error,GOP,times);
}


GENERATE_FUNCTIONS(ulong,double)
kernel void insert_key_frame_ulong(global GENERATE_NAME(CLPixelData,ulong)* pixel_data, const int pos, const double error, global const double* times)
{
	GENERATE_NAME(insert_key_frame_internal,ulong)(pixel_data, pos, error, times);
}
kernel void advance_pixel_data_ulong( global GENERATE_NAME(CLPixelData,ulong)* pixel_data, global GENERATE_NAME(CLPixelType,ulong)* all_pixels, global const ulong* img, const int pos, const double error, const int GOP, global const double* times)
{
	GENERATE_NAME(advance_pixel_data_internal,ulong)(pixel_data,all_pixels,img,pos,error,GOP,times);
}


GENERATE_FUNCTIONS(float,float)
kernel void insert_key_frame_float(global GENERATE_NAME(CLPixelData,float)* pixel_data, const int pos, const double error, global const double* times)
{
	GENERATE_NAME(insert_key_frame_internal,float)(pixel_data, pos, error, times);
}
kernel void advance_pixel_data_float( global GENERATE_NAME(CLPixelData,float)* pixel_data, global GENERATE_NAME(CLPixelType,float)* all_pixels, global const float* img, const int pos, const double error, const int GOP, global const double* times)
{
	GENERATE_NAME(advance_pixel_data_internal,float)(pixel_data,all_pixels,img,pos,error,GOP,times);
}


GENERATE_FUNCTIONS(double,double)
kernel void insert_key_frame_double(global GENERATE_NAME(CLPixelData,double)* pixel_data, const int pos, const double error, global const double* times)
{
	GENERATE_NAME(insert_key_frame_internal,double)(pixel_data, pos, error, times);
}
kernel void advance_pixel_data_double( global GENERATE_NAME(CLPixelData,double)* pixel_data, global GENERATE_NAME(CLPixelType,double)* all_pixels, global const double* img, const int pos, const double error, const int GOP, global const double* times)
{
	GENERATE_NAME(advance_pixel_data_internal,double)(pixel_data,all_pixels,img,pos,error,GOP,times);
}

)"