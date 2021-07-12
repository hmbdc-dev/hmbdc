#include "hmbdc/Copyright.hpp"
#pragma once

#ifndef hmbdc_likely
#define hmbdc_likely(x)       __builtin_expect(!!(x),1)
#endif

#ifndef hmbdc_unlikely
#define hmbdc_unlikely(x)     __builtin_expect(!!(x),0)
#endif

#ifndef HMBDC_RESTRICT 
#ifdef __clang__
#define HMBDC_RESTRICT 
#else
#define HMBDC_RESTRICT __restrict__
#endif
#endif

#ifdef _QNX_SOURCE
#define __GNUC_PREREQ(x, y) 0
#endif
