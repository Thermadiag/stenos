# Compressed std::vector like container

`stenos::cvector` is a is a random-access container with an interface similar to `std::vector` but storing its element in a compressed way. 
Its goal is to reduce the memory footprint of the container while providing performances as close as possible to `std::vector`. 


## Internals

By default, `cvector` stores its elements by chunks of 256 values. Whenever a chunk is filled (due for instance to calls to push_back()), it is compressed
and the chunk itself is either kept internally (for further decompression) or deallocated. This means that `cvector` NEVER ensure reference stability,
as a stored object might only exist in its compressed form.

When accessing a value using iterators or operator[], the corresponding chunk is first located. If this chunk was already decompressed, a reference wrapper
to the corresponding element is returned. Otherwise, the memory chunk is decompressed first. If the accessed element is modified, the chunk is mark as dirty,
meaning that it will require recompression at some point.

To avoid compressing/decompressing lots of chunks when performing heavy random-access operations, `cvector` allows multiple chunks to store their elements
in their decompressed form, called **decompression contexts**. The maximum number of decompression contexts is defined using
`cvector::set_max_contexts()`, and is either a fixed number or a fraction of the total number of chunks. By default, the number of decompression contexts is
limited to 12.5% of the number of chunks. This means that the `cvector` memory footprint is at most 1.125 times higher than `cvector::size()*sizeof(value_type)`.

Whenever a chunk must be decompressed to access one of its element, it allocates a new decompression context if the maximum number of allowed contexts has not been
reach yet, and this context is added to an internal list of all available contexts. Otherwise, it try to reuse an existing decompression context 
from this internal list. Note that the chunk might reuse a context already used by another chunk. In this case, this other chunk is recompressed if marked dirty, 
the context is detached from this chunk and attached to the new one which decompress its elements inside. This means that accessing an element 
(even with const members) might invalidate any other reference to elements within the container. 
 

## Element access

Individual elements can be accessed using `cvector::operator[]`, `cvector::at`, `cvector::front`, `cvector::back` or iterators. As seen in previous section, accessing
an element might invalidate all other references to the container elements (in fact, it might invalidate all elements that do not belong to the corresponding chunk).

That's why access members return a reference wrapper instead of a plain reference (types `cvector::ref_type` and `cvector::const_ref_type`). A reference wrapper basically
stores a pointer to `cvector` internal data and the coordinate of the corresponding element. When casting this wrapper to **value_type**, the corresponding chunk is
decompressed (if needed) and the value at given location is returned. A reference wrapper can be casted to **value_type** or **const value_type&**, in which case the 
reference should not be used after accessing another element.

Reference wrappers are ref counted. While a reference wrapper on a compressed block exists, the corresponding decompression context cannot be stolen by another chunk. This behavior is mandatory to avoid UB with some custom/STL algorithms.

Example:

```cpp

// Fill cvector
stenos::cvector<int> vec;
for(int i=0; i < 1000; ++i)
		vec.push_back(i);

{
// a is of type cvector<int>::ref_type
auto a = vec[0];

// copy element pointed by a to b
int b = a;
}

// Store a const reference of element 0
const int & c = vec[0];

// WARNING: accessing element at position 600 might invalidate reference c!
const int & d = vec[600];

``` 

In order for cvector to work with all STL algorithms, some latitudes with C++ standard were taken:
-	`std::swap` is overloaded for reference wrapper types. Overloading `std::swap` is forbidden by the standard, but works in practice with latest versions of msvc, gcc and clang.
-	`std::move` is overloaded for reference wrapper types. This was mandatory for algorithms like `std::move(first,last,dst)` to work on move-only types.

Thanks to this, it is possible to call `std::sort` or std::random_shuffle on a `cvector`. For instance, the following code snippet successively:
-	Call `cvector::push_back` to fill the cvector with sorted data. In this case the compression ratio (compressed size/raw size) is very low due to high values correlation.
-	Call `std::random_shuffle` to shuffle the cvector: the compression ratio becomes very high as compressing random data is basically impossible.
-	Sort again the cvector with `std::sort` to get back the initial compression ratio.

```cpp

#include <stenos/cvector.hpp>
#include <stenos/timer.hpp>	

#include <iostream>
#include <algorithm>
#include <random>


int main(int, char** const)
{
	// The goal of this example is to keep track of the program memory footprint will performing some operations on a compressed vector

	using namespace stenos;
	cvector<int> w;

	// fill with consecutive values
	for (size_t i = 0; i < 10000000; ++i)
		w.push_back((int)i);

	// very good compression ratio as data are sorted
	std::cout << "push_back: " << w.current_compression_ratio() << std::endl;

	// shuffle the cvector
	timer t;
	t.tick();
	std::mt19937 rng(0);
	std::shuffle(w.begin(), w.end(), rng);
	auto elapsed_ms = t.tock() * 1e-6;

	// Bad compression ratio on random values (but still better than 1)
	std::cout << "random_shuffle: " << w.current_compression_ratio() << " in " << elapsed_ms << " ms" << std::endl;

	// sort the cvector
	t.tick();
	std::sort(w.begin(), w.end());
	elapsed_ms = t.tock() * 1e-6;
	// Go back to original ratio
	std::cout << "sort: " << w.current_compression_ratio() << " in " << elapsed_ms << " ms" << std::endl;

	return 0;
}
``` 

Below is a curve representing the program memory footprint during previous operations (extracted with Visual Studio diagnostic tools):

![cvector memory](images/cvector_memory.png)

## Restrictions

cvector only works with relocatable value types (relocation in terms of move plus destroy).
`stenos::is_relocatable` type trait will be used to detect invalid data types. You can specialize `stenos::is_relocatable` for your custom
types if you are certain they are indeed relocatable.


## Parameters

cvector provides the following additional template parameters:
-	**BlockSize** (unsigned, 0 by default): defines the chunk size. cvector uses a chunk size of (256 << BlockSize) elements. 
	Increasing the chunk size might provide better compression ratio and faster linear access, but will reduce random access performances.
-	**Level** (int, 1 by default): compression level, from 0 (no compression) to 9 (maximum compression).
	The compression level 1 will only use the SIMD based block compression of the Stenos library. Additional levels will add Zstd layers.


## Multithreading

By default, cvector does not support multi-threaded access, even on read-only mode. Indeed, retrieving an element might trigger a block decompression, which in
turn might trigger a recompression of another block in order to steal its decompression context.

That's why cvector provides the members `for_each()`, `const_for_each()`, `for_each_backward()` and `const_for_each_backward()`.
These functions apply a functor to a sub-range of the cvector and support parallel access: `for_each()` can for instance be called in parallel to `const_for_each_backward()` (but not other cvector members).

Basic usage:

```cpp

#include <stenos/cvector.hpp>
#include <random>
#include <cmath>

int  main  (int , char** )
{
using namespace stenos;

	using namespace stenos;

	// fill a cvector with random values
	std::srand(0);
	cvector<float> random_vals(1000000);
	for (auto v : random_vals)
		v = (float)std::rand();

	// Apply std::cos to all values
	random_vals.for_each(0, random_vals.size(), [](float& v) { v = std::cos(v); });

return 0;
}

```


## Serialization

cvector provides serialization/deserialization functions working on compressed blocks. Use `cvector::serialize` to save the cvector content in
a `std::ostream` object or in a buffer, and `cvector::deserialize` to read back the cvector from a `std::istream` object or a buffer. When deserializing a cvector object with
cvector::deserialize, the cvector template parameters must be the same as the ones used for serialization, except for the **Level** parameter and the allocator type.

Note that the serialized content can be decompress with `stenos_decompress()` and `stenos_decompress_generic()`.

Example:

```cpp

#include <stenos/cvector.hpp>

#include <string>
#include <iostream>
#include <vector>
#include <sstream>
#include <algorithm>


int  main  (int , char** )
{
	using namespace stenos;

	// Create values we want to serialize
	std::vector<int> content(10000000);
	for (size_t i = 0; i < content.size(); ++i)
		content[i] = i;


	std::string saved;
	{
		// Create a cvector, fill it
		cvector<int> vec;
		std::copy(content.begin(), content.end(), std::back_inserter(vec));

		// Save cvector in 'saved' string
		std::ostringstream oss;
		vec.serialize(oss);
		saved = oss.str();

		// print the compression ratio based on 'saved'
		std::cout << "serialize compression ratio: " << saved.size() / (double)(sizeof(int) * vec.size()) << std::endl;
	}

	// Deserialize 'saved' string
	std::istringstream iss(saved);
	cvector<int> vec;
	vec.deserialize(iss);

	// Make sure the deserialized cvector is equal to the original vector
	std::cout << "deserialization valid: " << std::equal(vec.begin(), vec.end(), content.begin(), content.end()) << std::endl;

	return 0;
}

```



## Text compression

The level 1 block compressor provided with cvector works great for arbitrary structures, but compresses poorly raw ascii text.
Using a `stenos::cvector<char>` in order to compress raw text is a bad idea as the resulting compression ratio will be very low.

Instead, you should increase the BlockSize as well as the compression level to enable Zstd based comrpession.

Example:

```cpp

#include <stenos/cvector.hpp>

#include <fstream>
#include <iostream>

using namespace seq;

int  main  (int , char** )
{
	std::ifstream fin("my_text_file.txt");
	
	// create a cvector of char and fill it with provided file's content
	cvector<char, 2, 5> vec;
	vec.assign(std::istreambuf_iterator<char>(fin), std::istreambuf_iterator<char>());

	std::cout <<vec.size() << " "<< vec.current_compression_ratio() << std::endl;
	return 0;
}
```