#ifndef __febird_config_h__
#define __febird_config_h__

#if defined(_MSC_VER)

# pragma once

#ifndef _CRT_SECURE_NO_WARNINGS
# define _CRT_SECURE_NO_WARNINGS
#endif

#  if defined(FEBIRD_CREATE_DLL)
#    pragma warning(disable: 4251)
#    define FEBIRD_DLL_EXPORT __declspec(dllexport)      // creator of dll
#  elif defined(FEBIRD_USE_DLL)
#    pragma warning(disable: 4251)
#    define FEBIRD_DLL_EXPORT __declspec(dllimport)      // user of dll
#    ifdef _DEBUG
#	   pragma comment(lib, "febird-d.lib")
#    else
#	   pragma comment(lib, "febird.lib")
#    endif
#  else
#    define FEBIRD_DLL_EXPORT                            // static lib creator or user
#  endif

#else /* _MSC_VER */

#  define FEBIRD_DLL_EXPORT

#endif /* _MSC_VER */


#if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__clang__)

#  define febird_likely(x)    __builtin_expect(x, 1)
#  define febird_unlikely(x)  __builtin_expect(x, 0)

#else

#  define febird_likely(x)    x
#  define febird_unlikely(x)  x

#endif

/* The ISO C99 standard specifies that in C++ implementations these
 *    should only be defined if explicitly requested __STDC_CONSTANT_MACROS
 */
#define __STDC_CONSTANT_MACROS

#define DATA_IO_ALLOW_DEFAULT_SERIALIZE

#endif // __febird_config_h__
