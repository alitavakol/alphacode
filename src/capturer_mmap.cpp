#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(LIVE)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>             /* getopt_long() */
#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <asm/types.h>          /* for videodev2.h */
#include <linux/videodev2.h>
#include <signal.h>

#define CLEAR(x) memset (&(x), 0, sizeof (x))

extern void init();
extern void finalize();
extern void process(const char *frame);

int exit_loop = 0;
bool writeFrames = false;

// info needed to store one video frame in memory
struct buffer {
	void * start;
	size_t length;
};

#if defined(LIVE) && !defined(GUI)
#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
#define RESET "\033[0m"
#else
#define KNRM ""
#define KRED ""
#define KGRN ""
#define KYEL ""
#define KBLU ""
#define KMAG ""
#define KCYN ""
#define KWHT ""
#define RESET ""
#endif

static void errno_exit(const char *s)
{
	fprintf(stderr, KRED"%s error %d, %s"RESET"\n", s, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

// a blocking wrapper of the ioctl function
static int xioctl(int fd, int request, void *arg)
{
	int r;

	do
		r = ioctl(fd, request, arg);
	while (-1 == r && EINTR == errno);

	return r;
}

//static const int width = 1280, height = 720;

// read one frame from memory and throws the data to standard output
static int read_frame(int * fd, unsigned int * n_buffers, struct buffer * buffers, int pixel_format)
{
	struct v4l2_buffer buf; // needed for memory mapping

	CLEAR(buf);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(*fd, VIDIOC_DQBUF, &buf)) {
		switch (errno)
		{
		case EAGAIN:
			return 0;

		case EIO: // EIO ignored

		default:
			errno_exit("VIDIOC_DQBUF");
		}
	}

	assert(buf.index < *n_buffers);

	process((const char*) buffers[buf.index].start);

	if (-1 == xioctl(*fd, VIDIOC_QBUF, &buf))
		errno_exit("VIDIOC_QBUF");

	return 1;
}

// just the main loop of this program
static void main_loop(int * fd, unsigned int * n_buffers, struct buffer * buffers, int pixel_format)
{
	while (!exit_loop) {
		fd_set fds;
		struct timeval tv;
		int r;

		FD_ZERO(&fds);
		FD_SET(*fd, &fds);

		/* Select Timeout */
		tv.tv_sec = 2;
		tv.tv_usec = 0;

		// the classic select function, who allows to wait up to 2 seconds,
		// until we have captured data,
		r = select(*fd + 1, &fds, NULL, NULL, &tv);

		if (-1 == r) {
			if (EINTR == errno)
				continue;

			errno_exit("select");
		}

		if (0 == r) {
			fprintf(stderr, KRED"select timeout"RESET"\n");
			exit(EXIT_FAILURE);
		}

		// read one frame from the device and put on the buffer
		read_frame(fd, n_buffers, buffers, pixel_format);
	}

	fprintf(stdout, KYEL"exiting main loop"RESET"\n");
}

static void stop_capturing(int * fd)
{
	enum v4l2_buf_type type;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	// this call to xioctl allows to stop the stream from the capture device
	if (-1 == xioctl(*fd, VIDIOC_STREAMOFF, &type))
		errno_exit("VIDIOC_STREAMOFF");
}

static void start_capturing(int * fd, unsigned int * n_buffers)
{
	unsigned int i;
	enum v4l2_buf_type type;

	for (i = 0; i < *n_buffers; ++i) {
		struct v4l2_buffer buf;

		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (-1 == xioctl(*fd, VIDIOC_QBUF, &buf))
			errno_exit("VIDIOC_QBUF");
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	// start the capture from the device
	if (-1 == xioctl(*fd, VIDIOC_STREAMON, &type))
		errno_exit("VIDIOC_STREAMON");
}

// free the shared memory area
static void uninit_device(unsigned int * n_buffers, struct buffer * buffers)
{
	unsigned int i;

	for (i = 0; i < *n_buffers; ++i)
		if (-1 == munmap(buffers[i].start, buffers[i].length))
			errno_exit("munmap");
	free(buffers);
}

// alloc buffers and configure the shared memory area
static struct buffer *init_mmap(int * fd, const char * dev_name, unsigned int * n_buffers)
{
	struct v4l2_requestbuffers req;
	// buffers is an array of n_buffers length, and every element store a frame
	struct buffer *buffers = NULL;
	CLEAR(req);

	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(*fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, KRED"%s does not support "
					"memory mapping"RESET"\n", dev_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_REQBUFS");
		}
	}

	if (req.count < 2) {
		fprintf(stderr, KRED"insufficient buffer memory on %s"RESET"\n", dev_name);
		exit(EXIT_FAILURE);
	}
	buffers = (buffer*) calloc(req.count, sizeof(*buffers));
	if (!buffers) {
		fprintf(stderr, KRED"out of memory"RESET"\n");
		exit(EXIT_FAILURE);
	}
	// map every element of the array buffers to the shared memory
	for (*n_buffers = 0; *n_buffers < req.count; ++*n_buffers) {
		struct v4l2_buffer buf;

		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = *n_buffers;

		if (-1 == xioctl(*fd, VIDIOC_QUERYBUF, &buf))
			errno_exit("VIDIOC_QUERYBUF");

		buffers[*n_buffers].length = buf.length;
		buffers[*n_buffers].start = mmap(NULL /* start anywhere */, buf.length,
		PROT_READ | PROT_WRITE /* required */,
		MAP_SHARED /* recommended */, *fd, buf.m.offset);

		if (MAP_FAILED == buffers[*n_buffers].start)
			errno_exit("mmap");
	}
	return buffers;
}

// configure and initialize the hardware device
static struct buffer *init_device(int * fd, const char * dev_name, unsigned int * n_buffers, int pixel_format)
{
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format fmt;
	struct buffer * buffers = NULL;
	unsigned int min;

	if (-1 == xioctl(*fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			fprintf(stderr, KRED"%s is no V4L2 device"RESET"\n", dev_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, KRED"%s is no video capture device"RESET"\n", dev_name);
		exit(EXIT_FAILURE);
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		fprintf(stderr, KRED"%s does not support streaming i/o"RESET"\n", dev_name);
		exit(EXIT_FAILURE);
	}

	/* Select video input, video standard and tune here. */
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == xioctl(*fd, VIDIOC_CROPCAP, &cropcap)) {
		/* Errors ignored. */
	}

	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	crop.c = cropcap.defrect; /* reset to default */

	if (-1 == xioctl(*fd, VIDIOC_S_CROP, &crop)) {
		switch (errno)
		{
		case EINVAL:
			/* Cropping not supported. */
			break;
		default:
			/* Errors ignored. */
			break;
		}
	}

	CLEAR(fmt);
	// set image properties
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = INPUT_IMG_WIDTH;
	fmt.fmt.pix.height = INPUT_IMG_HEIGHT;

	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

	if (-1 == xioctl(*fd, VIDIOC_S_FMT, &fmt))
		errno_exit("\nError: pixel format not supported\n");

	/* Note VIDIOC_S_FMT may change width and height. */

	// check the configuration data
	min = fmt.fmt.pix.width * 2;
	if (fmt.fmt.pix.bytesperline < min)
		fmt.fmt.pix.bytesperline = min;
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	if (fmt.fmt.pix.sizeimage < min)
		fmt.fmt.pix.sizeimage = min;

	fprintf(stdout, KYEL"video bytes per line = %d"RESET"\n", fmt.fmt.pix.bytesperline);

	buffers = init_mmap(fd, dev_name, n_buffers);

	return buffers;
}

static void close_device(int * fd)
{
	if (-1 == close(*fd))
		errno_exit("close");

	*fd = -1;
}

static void open_device(int * fd, const char * dev_name)
{
	struct stat st;

	if (-1 == stat(dev_name, &st)) {
		fprintf(stderr, KRED"cannot identify '%s': %d, %s"RESET"\n", dev_name, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!S_ISCHR (st.st_mode)) {
		fprintf(stderr, KRED"%s is no device"RESET"\n", dev_name);
		exit(EXIT_FAILURE);
	}

	*fd = open(dev_name, O_RDWR /* required */| O_NONBLOCK, 0);

	if (-1 == *fd) {
		fprintf(stderr, KRED"cannot open '%s': %d, %s"RESET"\n", dev_name, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

//// show the usage
//static void usage(FILE *fp, int argc, char **argv)
//{
//	fprintf(fp,
//			"Usage: %s [options]\n\n"
//					"Options:\n"
//					"-w | --window-size  <1280*720|        Video size\n"
//					"                      640*480|\n"
//					"                      320*240>\n"
//					"-v | --write                          Write frames to standard output\n"
//					"-h | --help                           Print this message\n"
//					"\n", argv[0]);
//}
//
//// used by getopt_long to know the possible inputs
//static const char short_options[] = "w:h:v";
//
//// long version of the previous function
//static const struct option long_options[] =
//{
//{ "window-size", required_argument, NULL, 'w' },
//{ "help", no_argument, NULL, 'h' },
//{ "write", no_argument, NULL, 'v' },
//{ 0, 0, 0, 0 } };

static void signalHandler_sigint(int signum)
{
//	fprintf(stderr, "Got SIGINT in capture app\n");
	exit_loop = 1;
}

void init_signals(void)
{
	struct sigaction sigAction;

	sigAction.sa_flags = 0;
	sigemptyset(&sigAction.sa_mask);
	sigaddset(&sigAction.sa_mask, SIGINT);

	sigAction.sa_handler = signalHandler_sigint;
	sigaction(SIGINT, &sigAction, NULL);
}

int main_(int argc, char ** argv)
{
	const char *dev_name = "/dev/video0";
	int fd = -1;
	unsigned int n_buffers;
	struct buffer *buffers = NULL;
//	int index;
//	int c;
	int pixel_format = 0;

	init_signals();

//	// process all the command line arguments
//	for (;;)
//	{
//		c = getopt_long(argc, argv, short_options, long_options, &index);
//
//		if (-1 == c)
//			break;	// no more arguments (quit from for)
//
//		switch (c)
//		{
//		case 0: // getopt_long() flag
//			break;
//
//		case 'w':
//			if (strcmp(optarg, "1280*720") == 0)
//			{
//				fprintf(stdout, "window size 1280*720\n");
//				width = 1280;
//				height = 720;
//			}
//			else if (strcmp(optarg, "640*480") == 0)
//			{
//				fprintf(stdout, "window size 640*480\n");
//				width = 640;
//				height = 480;
//			}
//			else if (strcmp(optarg, "320*240") == 0)
//			{
//				fprintf(stdout, "window size 320*240\n");
//				width = 320;
//				height = 240;
//			}
//			else
//			{
//				fprintf(stderr, "window size not supported\n");
//				exit(EXIT_FAILURE);
//			}
//			break;
//
//		case 'h':
//			usage(stdout, argc, argv);
//			exit(EXIT_SUCCESS);
//
//		case 'v':
//			writeFrames = true;
//			break;
//
//		default:
//			usage(stdout, argc, argv);
//			exit(EXIT_FAILURE);
//		}
//	}
//
	open_device(&fd, dev_name);

	init();

	fprintf(stdout, KYEL"init_device"RESET"\n");
	buffers = init_device(&fd, dev_name, &n_buffers, pixel_format);

	fprintf(stdout, KYEL"start_capturing"RESET"\n");
	start_capturing(&fd, &n_buffers);

	fprintf(stdout, KYEL"main_loop"RESET"\n");
	main_loop(&fd, &n_buffers, buffers, pixel_format);

	fprintf(stdout, KYEL"stop_capturing"RESET"\n");
	stop_capturing(&fd);

	fprintf(stdout, KYEL"uninit_device"RESET"\n");
	uninit_device(&n_buffers, buffers);

	finalize();

	close_device(&fd);
	return 0;
}

#endif // SCAN
