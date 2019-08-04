ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = $(BOOST_CPPFLAGS)

bin_PROGRAMS = generator scanner

generator_SOURCES = src/Image.cpp src/alphacode.cpp src/manipulator.cpp
generator_CXXFLAGS = -DGENERATE -DspaceBeforeRow=2 -DcodeFinderSlashCount=10 -DrowFinderSlashCount=3 -ULIVE -UGUI -USCAN -UDURATION -UIMAGE_PATH -UVIDEO_PATH
# generator_LDFLAGS = $(BOOST_LDFLAGS)
generator_LDADD = -lopencv_core -lopencv_highgui -lopencv_imgproc -lgsl -lgslcblas -lsqlite3 $(BOOST_THREAD_LIB) $(BOOST_CHRONO_LIB)

scanner_SOURCES = src/Image.cpp src/alphacode.cpp src/capturer_mmap.cpp src/manipulator.cpp
scanner_CXXFLAGS = -DLIVE -DSCAN -DspaceBeforeRow=2 -DcodeFinderSlashCount=10 -DrowFinderSlashCount=3 -UUSE_HOUGH -UGENERATE
scanner_LDFLAGS = $(BOOST_LDFLAGS)
scanner_LDADD = -lopencv_core -lopencv_highgui -lopencv_imgproc -lgsl -lgslcblas -lsqlite3 $(BOOST_THREAD_LIB) $(BOOST_CHRONO_LIB)
