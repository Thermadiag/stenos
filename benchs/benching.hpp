/**
 * MIT License
 *
 * Copyright (c) 2025 Victor Moncada <vtr.moncada@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef STENOS_BENCHING_HPP
#define STENOS_BENCHING_HPP

#include <vector>
#include <fstream>
#include <array>

/// @brief Read input binary file
template<size_t N>
static std::vector<std::array<char, N>> read_binary(const char* filename)
{
	std::vector<std::array<char, N>> res;
	std::ifstream iss(filename, std::ios::binary);

	while (iss) {
		std::array<char, N> ar;
		iss.read((char*)&ar, sizeof(ar));
		if (iss)
			res.push_back(ar);
	}
	return res;
}

/// @brief Read input ascii file
template<class T>
static std::vector<T> read_text(const char* filename)
{
	std::vector<T> res;
	std::ifstream iss(filename, std::ios::binary);

	while (iss) {
		T val;
		iss >> val;
		if (iss)
			res.push_back(val);
	}
	return res;
}

/// @brief Find a suitable number of iteration for given vector
template<class T>
static int iteration_count(const std::vector<T>& vec)
{
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	if (bytes < 500000)
		return 100;
	if (bytes < 2000000)
		return 50;
	if (bytes < 5000000)
		return 10;
	return 1;
}

/// @brief Extract the filename part of given full file path
static inline std::string file_name(const std::string& full_name)
{
	auto idx = full_name.find_last_of('/');
	if (idx == std::string::npos)
		return std::string();
	return full_name.substr(idx + 1);
}

#endif
