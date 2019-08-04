AC_PREREQ(2.59)
AC_INIT([alphacode], [1.0], [ali.tavakol@gmail.com])

AC_CANONICAL_SYSTEM
dnl AM_INIT_AUTOMAKE()
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([subdir-objects])
AM_CONFIG_HEADER([src/config.h])

AC_PROG_CXX
AC_CONFIG_FILES([Makefile])

AC_ARG_WITH(
	[event-code],
	[AS_HELP_STRING(
		[--with-event-code],
		[Event code (default is 1)])
	],
	[],
	[with_event_code=1]
)
AC_DEFINE_UNQUOTED(EVENT_CODE, ${with_event_code}, [Event code])

AC_ARG_WITH(
	[symbol-size],
	[AS_HELP_STRING(
		[--with-symbol-size],
		[Count of special characters to use (default is 4)])
	],
	[],
	[with_symbol_size=4]
)
AC_DEFINE_UNQUOTED(TICKET_SYMBOL_COUNT, ${with_symbol_size}, [Count of special characters to use])

AC_ARG_WITH(
	[lines],
	[AS_HELP_STRING(
		[--with-lines],
		[Count of lines of the generated text (default is 3)])
	],
	[],
	[with_lines=3]
)
AC_DEFINE_UNQUOTED(TICKET_LINES, ${with_lines}, [Count of lines of the generated text])

AC_ARG_WITH(
	[line-length],
	[AS_HELP_STRING(
		[--with-line-length],
		[With of the generated text (default is 15)])
	],
	[],
	[with_line_length=15]
)
AC_DEFINE_UNQUOTED(TICKET_LINE_LENGTH, ${with_line_length}, [With of the generated text])

AC_ARG_WITH(
	[checksum-length],
	[AS_HELP_STRING(
		[--with-checksum-length],
		[Count of characters per line to reserve for error detection (default is 9)])
	],
	[],
	[with_checksum_length=9]
)
AC_DEFINE_UNQUOTED(TICKET_CHECKSUM_LENGTH, ${with_checksum_length}, [Checksum length])

AC_ARG_WITH(
	[correlation],
	[AS_HELP_STRING(
		[--with-correlation],
		[Correlation of ticket lines (default is 5)])
	],
	[],
	[with_correlation=5]
)
AC_DEFINE_UNQUOTED(TICKET_CORRELATION, ${with_correlation}, [Correlation of ticket lines])

AC_ARG_WITH(
	[camera-width],
	[AS_HELP_STRING(
		[--with-camera-width],
		[Width of live image capture (default is 640)])
	],
	[],
	[with_camera_width=640]
)
AC_DEFINE_UNQUOTED(INPUT_IMG_WIDTH, ${with_camera_width}, [Width of live image capture])

AC_ARG_WITH(
	[camera-height],
	[AS_HELP_STRING(
		[--with-camera-height],
		[Height of live image capture (default is 480)])
	],
	[],
	[with_camera_height=480]
)
AC_DEFINE_UNQUOTED(INPUT_IMG_HEIGHT, ${with_camera_height}, [Height of live image capture])

AX_PTHREAD([], [AC_MSG_ERROR([pthread was not found on your system])])
AX_LIB_SQLITE3
AX_BOOST_BASE([1.54],,[AC_MSG_ERROR([boost was not found on your system])])
AX_BOOST_THREAD
AX_BOOST_CHRONO

AC_OUTPUT
