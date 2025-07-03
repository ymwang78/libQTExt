#pragma once

#include <QtCore/qglobal.h>

#ifndef BUILD_STATIC
# if defined(LIBQTEXT_LIB)
#  define LIBQTEXT_EXPORT Q_DECL_EXPORT
# else
#  define LIBQTEXT_EXPORT Q_DECL_IMPORT
# endif
#else
# define LIBQTEXT_EXPORT
#endif
