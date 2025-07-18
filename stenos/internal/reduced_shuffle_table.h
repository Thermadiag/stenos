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

#ifndef STENOS_REDUCED_SHUFFLE_TABLE_H
#define STENOS_REDUCED_SHUFFLE_TABLE_H

#include "../bits.hpp"

namespace stenos
{
	namespace detail
	{
		// Reduced table for simd based RLE compression
		static STENOS_ALWAYS_INLINE const std::uint64_t* get_reduced_shuffle_table() noexcept
		{
			static const std::uint64_t reduced_shuffle_table[] = { 506097522914230528ull,
									       1976943448883713ull,
									       1976943448883712ull,
									       7722435347202ull,
									       1976943448883456ull,
									       7722435347201ull,
									       7722435347200ull,
									       30165763075ull,
									       1976943448817920ull,
									       7722435346945ull,
									       7722435346944ull,
									       30165763074ull,
									       7722435346688ull,
									       30165763073ull,
									       30165763072ull,
									       117835012ull,
									       1976943432040704ull,
									       7722435281409ull,
									       7722435281408ull,
									       30165762818ull,
									       7722435281152ull,
									       30165762817ull,
									       30165762816ull,
									       117835011ull,
									       7722435215616ull,
									       30165762561ull,
									       30165762560ull,
									       117835010ull,
									       30165762304ull,
									       117835009ull,
									       117835008ull,
									       460293ull,
									       1976939137073408ull,
									       7722418504193ull,
									       7722418504192ull,
									       30165697282ull,
									       7722418503936ull,
									       30165697281ull,
									       30165697280ull,
									       117834755ull,
									       7722418438400ull,
									       30165697025ull,
									       30165697024ull,
									       117834754ull,
									       30165696768ull,
									       117834753ull,
									       117834752ull,
									       460292ull,
									       7722401661184ull,
									       30165631489ull,
									       30165631488ull,
									       117834498ull,
									       30165631232ull,
									       117834497ull,
									       117834496ull,
									       460291ull,
									       30165565696ull,
									       117834241ull,
									       117834240ull,
									       460290ull,
									       117833984ull,
									       460289ull,
									       460288ull,
									       1798ull,
									       1975839625445632ull,
									       7718123536897ull,
									       7718123536896ull,
									       30148920066ull,
									       7718123536640ull,
									       30148920065ull,
									       30148920064ull,
									       117769219ull,
									       7718123471104ull,
									       30148919809ull,
									       30148919808ull,
									       117769218ull,
									       30148919552ull,
									       117769217ull,
									       117769216ull,
									       460036ull,
									       7718106693888ull,
									       30148854273ull,
									       30148854272ull,
									       117768962ull,
									       30148854016ull,
									       117768961ull,
									       117768960ull,
									       460035ull,
									       30148788480ull,
									       117768705ull,
									       117768704ull,
									       460034ull,
									       117768448ull,
									       460033ull,
									       460032ull,
									       1797ull,
									       7713811726592ull,
									       30132077057ull,
									       30132077056ull,
									       117703426ull,
									       30132076800ull,
									       117703425ull,
									       117703424ull,
									       459779ull,
									       30132011264ull,
									       117703169ull,
									       117703168ull,
									       459778ull,
									       117702912ull,
									       459777ull,
									       459776ull,
									       1796ull,
									       30115234048ull,
									       117637633ull,
									       117637632ull,
									       459522ull,
									       117637376ull,
									       459521ull,
									       459520ull,
									       1795ull,
									       117571840ull,
									       459265ull,
									       459264ull,
									       1794ull,
									       459008ull,
									       1793ull,
									       1792ull,
									       7ull,
									       1694364648734976ull,
									       6618611909121ull,
									       6618611909120ull,
									       25853952770ull,
									       6618611908864ull,
									       25853952769ull,
									       25853952768ull,
									       100992003ull,
									       6618611843328ull,
									       25853952513ull,
									       25853952512ull,
									       100992002ull,
									       25853952256ull,
									       100992001ull,
									       100992000ull,
									       394500ull,
									       6618595066112ull,
									       25853886977ull,
									       25853886976ull,
									       100991746ull,
									       25853886720ull,
									       100991745ull,
									       100991744ull,
									       394499ull,
									       25853821184ull,
									       100991489ull,
									       100991488ull,
									       394498ull,
									       100991232ull,
									       394497ull,
									       394496ull,
									       1541ull,
									       6614300098816ull,
									       25837109761ull,
									       25837109760ull,
									       100926210ull,
									       25837109504ull,
									       100926209ull,
									       100926208ull,
									       394243ull,
									       25837043968ull,
									       100925953ull,
									       100925952ull,
									       394242ull,
									       100925696ull,
									       394241ull,
									       394240ull,
									       1540ull,
									       25820266752ull,
									       100860417ull,
									       100860416ull,
									       393986ull,
									       100860160ull,
									       393985ull,
									       393984ull,
									       1539ull,
									       100794624ull,
									       393729ull,
									       393728ull,
									       1538ull,
									       393472ull,
									       1537ull,
									       1536ull,
									       6ull,
									       5514788471040ull,
									       21542142465ull,
									       21542142464ull,
									       84148994ull,
									       21542142208ull,
									       84148993ull,
									       84148992ull,
									       328707ull,
									       21542076672ull,
									       84148737ull,
									       84148736ull,
									       328706ull,
									       84148480ull,
									       328705ull,
									       328704ull,
									       1284ull,
									       21525299456ull,
									       84083201ull,
									       84083200ull,
									       328450ull,
									       84082944ull,
									       328449ull,
									       328448ull,
									       1283ull,
									       84017408ull,
									       328193ull,
									       328192ull,
									       1282ull,
									       327936ull,
									       1281ull,
									       1280ull,
									       5ull,
									       17230332160ull,
									       67305985ull,
									       67305984ull,
									       262914ull,
									       67305728ull,
									       262913ull,
									       262912ull,
									       1027ull,
									       67240192ull,
									       262657ull,
									       262656ull,
									       1026ull,
									       262400ull,
									       1025ull,
									       1024ull,
									       4ull,
									       50462976ull,
									       197121ull,
									       197120ull,
									       770ull,
									       196864ull,
									       769ull,
									       768ull,
									       3ull,
									       131328ull,
									       513ull,
									       512ull,
									       2ull,
									       256ull,
									       1ull,
									       0ull,
									       0ull };
			return reduced_shuffle_table;
		}
	}
}
#endif
