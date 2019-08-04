#include <opencv/cv.h>
#if defined(GUI)
#include <opencv/highgui.h>
#endif

#define MAX_N 8

struct Image {
	Image(size_t N);

	size_t N;

#define width_margin  ((INPUT_IMG_WIDTH - PROC_IMG_WIDTH) / 2)
#define height_margin ((INPUT_IMG_HEIGHT - PROC_IMG_HEIGHT) / 2)
#define bpi (2) // bytes per pixel
#if defined(GUI)
	IplImage *src; // input image
#endif
	const unsigned short *frame; // input image supposing bpi = 2
//	IplImage *morphed, *smooth;

	bool blackOnWhite;

	inline bool getPt(int x, int y); // return point color as is
	inline bool instantGetPt(int x, int y);
	inline bool instantGetPt2(int x, int y);
#if defined(GUI)
	void setPt(int x, int y);
#endif
	void integral_image(const unsigned char *src); // compute integral of input gray image
	unsigned int integral[PROC_IMG_HEIGHT / 2 * PROC_IMG_WIDTH / 4], *int_tl, *int_tr, *int_bl, *int_br; // integral of input image
	void binarize(const unsigned char *src);
#if defined(GUI)
	IplImage *binarized; // binarized downsampled image
#endif
#define radius (4 * (int)(PROC_IMG_HEIGHT / 50 / 4)) // comparison region radius for adaptive thresholding
#define rotation_span (25) // angle span when searching for finder slash train
#if defined(USE_HOUGH)
	bool findOrientation();
	int accumulator[2 * rotation_span][(int) (1.5 * PROC_IMG_WIDTH)]; // hough transform bin for different angels
	static float Sin[180], Cos[180];
	int maxBin;
#endif

	float theta, rho; // orientation

	void initScan();
	bool find_next_slash_train(int num_slash_to_find, int scan_jump_size, int certainty);
	bool verify_slash_train(int num_slash_to_find, int train_length, float r); // verifies found slash train backward starting from trainEndX[r] and trainEndY[r]
	inline bool isGoodSlashTrain(int num_slash_to_find, int* slashBin, int slashBinSize);
	float slashBlackWidth;
#define scanJumpSize (PROC_IMG_HEIGHT / 50) // vertical jump size when scanning for slash train
	int slashBin[2 * codeFinderSlashCount + 2], slashBinSize, ibin;
	int slashBin_[2 * codeFinderSlashCount + 2];
	float slashPeriod;
	float y0; // initial scan position
	float X, Y, x, y; // current scan position
//	float incX;
	float incY; // horizontal and vertical incremental unit sizes for creating a slent scan line
	int trainLength;
	float trainEndX, trainEndY;
#define codeFinderCertainty (3)
#define rowFinderCertainty (7)

	float topLineIntercept, bottomLineIntercept, trainSlope; // slash train bounding lines
	float endLineIntercept; // upper bound of region in which check for existence of underline is performed. lower bound is bottomLineIntercept
	bool calcBoundingLines(); // finds accurate bottom bounding line of code train
	float finderEndX, finderEndY, finderTrainLength;
	float trainHeight, rowHeight;

	int ptx[PROC_IMG_WIDTH], pty[PROC_IMG_WIDTH]; // top- and right-most black points on slashes
	bool findTopBoundOfFinderTrain();

	void calcAccurateOrientation(); // calculations are performed on finder slash train

#define maxBitScanLines ((int) (2.5 * scanJumpSize))
#define maxC (40) // maximum count of alternating black and white semi-pulses
	float slashEdgeX[maxBitScanLines][maxC], slashEdgeY[maxBitScanLines][maxC];
	size_t nScanRows; // number of valid scanned rows
	bool scanBits();

	/* robust regression storage */
	static const int p = 2;
	gsl_matrix *cov;
	gsl_vector *c;
	gsl_vector *y_[PROC_IMG_WIDTH];
	gsl_matrix *X_[PROC_IMG_WIDTH];
	gsl_multifit_robust_workspace * work_[PROC_IMG_WIDTH];

	CvPoint2D32f dstAnchors[3]; // affine transformation destination anchor points
	CvPoint2D32f srcAnchors[3]; // affine transformation source anchor points
	CvMat* affineMat;
	void clip(int src_height); // fill in srcAnchors with top-left, top-right and bottom-left anchor points of source image before calling
#define CLIPPED_IMG_WIDTH (24)
#define CLIPPED_IMG_HEIGHT ((codeFinderSlashCount - 1) * CLIPPED_IMG_WIDTH)
#define CLIPPED_IMG_MAX_HEIGHT (4 * CLIPPED_IMG_HEIGHT)
	int clipped[CLIPPED_IMG_WIDTH * CLIPPED_IMG_MAX_HEIGHT]; // rotated and clipped area containing code or finder train

#define MAX_ALT_SHAPES (3)
	int *sym[MAX_N][MAX_ALT_SHAPES + 1]; // character match filters
	void filter2d(); // filter clipped with translated sym
	int response[CLIPPED_IMG_MAX_HEIGHT - CLIPPED_IMG_WIDTH + 1][MAX_N];
	int max_response_val_[CLIPPED_IMG_MAX_HEIGHT - CLIPPED_IMG_WIDTH + 1 + CLIPPED_IMG_WIDTH],
	        max_response_idx[CLIPPED_IMG_MAX_HEIGHT - CLIPPED_IMG_WIDTH + 1], *max_response_val;

	int nextMax(unsigned int *sym_idx);
#define maximWinSize (CLIPPED_IMG_WIDTH / 2) // half-size of window in which maximum value is found
	int cursor; // current position of window center
	int min_hill_height;
};

