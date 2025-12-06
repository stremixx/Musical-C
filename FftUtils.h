#ifndef FFT_UTILS_H
#define FFT_UTILS_H

#include <complex>
#include <valarray>

using Complex = std::complex<float>;
using CArray = std::valarray<Complex>;

const float PI = 3.141592653589793238460f;

void fft(CArray &x);

#endif // FFT_UTILS_H
